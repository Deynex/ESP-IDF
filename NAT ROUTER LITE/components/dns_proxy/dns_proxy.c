#include "dns_proxy.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_HEADER_SIZE 12
#define DNS_QUESTION_MAX 259 // A 255-byte wire name plus QTYPE and QCLASS.
#define DNS_PACKET_MAX 4096
#define DNS_PENDING_MAX 6
#define DNS_TIMEOUT_MS 2000
#define DNS_RETRY_MS 1000

typedef struct
{
    bool active;
    uint16_t forwarded_id;
    size_t query_size;
    TickType_t started;
    struct sockaddr_in client;
    uint8_t query[DNS_HEADER_SIZE + DNS_QUESTION_MAX];
} pending_query_t;

static const char *TAG = "DNS_PROXY";
static atomic_uint upstream_address;
static atomic_uint upstream_generation;
static atomic_bool proxy_started;
static atomic_bool upstream_connected;
static atomic_uint connected_generation;
static uint32_t ap_address;
static bool answer_logged;
// These buffers belong exclusively to the DNS task, not its stack.
static uint8_t packet[DNS_PACKET_MAX + 1];
static pending_query_t pending[DNS_PENDING_MAX];

static uint16_t read_u16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = value >> 8;
    bytes[1] = value & 0xff;
}

static bool usable_upstream(uint32_t address)
{
    const uint32_t host = ntohl(address);
    return address != 0 && address != ap_address &&
           (host >> 24) != 0 && (host >> 24) < 224;
}

// Normal DNS requests contain one uncompressed question. Validate its bounds
// before saving it for a timeout/error reply. Additional records pass through.
static size_t question_end(const uint8_t *query, size_t length)
{
    if (length < DNS_HEADER_SIZE || (query[2] & 0xf8) != 0 ||
        read_u16(query + 4) != 1)
    {
        return 0;
    }
    size_t position = DNS_HEADER_SIZE;
    size_t name_size = 0;
    while (position < length)
    {
        const uint8_t label = query[position++];
        ++name_size;
        if (label == 0)
        {
            if (name_size > 255 || length - position < 4)
            {
                return 0;
            }
            return position + 4;
        }
        if (label > 63 || label > length - position || name_size + label > 254)
        {
            return 0;
        }
        position += label;
        name_size += label;
    }
    return 0;
}

// Keep the question and RD/CD flags, but never cache an invented NXDOMAIN.
static void send_error(int listener, const struct sockaddr_in *client,
                       const uint8_t *query, size_t query_size,
                       uint8_t rcode, bool truncated)
{
    uint8_t reply[DNS_HEADER_SIZE + DNS_QUESTION_MAX] = {0};
    if (query_size < DNS_HEADER_SIZE)
    {
        return;
    }
    const size_t end = question_end(query, query_size);
    size_t size = DNS_HEADER_SIZE;
    reply[0] = query[0];
    reply[1] = query[1];
    reply[2] = 0x80 | (query[2] & 0x79) | (truncated ? 0x02 : 0);
    reply[3] = 0x80 | (query[3] & 0x10) | rcode;
    if (end != 0)
    {
        reply[5] = 1;
        memcpy(reply + DNS_HEADER_SIZE, query + DNS_HEADER_SIZE, end - DNS_HEADER_SIZE);
        size = end;
    }
    sendto(listener, reply, size, 0, (const struct sockaddr *)client, sizeof(*client));
}

static void fail_pending(int listener)
{
    for (size_t i = 0; i < DNS_PENDING_MAX; ++i)
    {
        if (pending[i].active)
        {
            send_error(listener, &pending[i].client, pending[i].query,
                       pending[i].query_size, 2, false);
            pending[i].active = false;
        }
    }
}

static uint16_t allocate_id(uint16_t *next_id)
{
    bool used;
    do
    {
        ++*next_id;
        used = false;
        for (size_t i = 0; i < DNS_PENDING_MAX; ++i)
        {
            used |= pending[i].active && pending[i].forwarded_id == *next_id;
        }
    } while (used);
    return *next_id;
}

static void receive_query(int listener, int upstream, uint16_t *next_id)
{
    struct sockaddr_in client = {0};
    socklen_t client_size = sizeof(client);
    const int length = recvfrom(listener, packet, sizeof(packet), 0,
                                (struct sockaddr *)&client, &client_size);
    const uint32_t subnet_mask = htonl(0xffffff00U);
    if (length < DNS_HEADER_SIZE || client.sin_family != AF_INET ||
        (client.sin_addr.s_addr & subnet_mask) != (ap_address & subnet_mask) ||
        (packet[2] & 0x80) != 0)
    {
        return;
    }
    const size_t end = question_end(packet, length);
    if (end == 0 || length > DNS_PACKET_MAX)
    {
        send_error(listener, &client, packet, length, 1, false);
        return;
    }
    if (upstream < 0 || !dns_proxy_is_ready())
    {
        send_error(listener, &client, packet, length, 2, false);
        return;
    }
    pending_query_t *slot = NULL;
    for (size_t i = 0; i < DNS_PENDING_MAX; ++i)
    {
        if (!pending[i].active)
        {
            slot = &pending[i];
            break;
        }
    }
    if (!slot)
    {
        send_error(listener, &client, packet, length, 2, false);
        return;
    }
    slot->client = client;
    slot->query_size = end;
    memcpy(slot->query, packet, end);
    slot->forwarded_id = allocate_id(next_id);
    slot->started = xTaskGetTickCount();
    slot->active = true;
    write_u16(packet, slot->forwarded_id);
    if (send(upstream, packet, length, 0) != length)
    {
        send_error(listener, &slot->client, slot->query, slot->query_size, 2, false);
        slot->active = false;
    }
}

static bool receive_answer(int listener, int upstream)
{
    const int length = recv(upstream, packet, sizeof(packet), 0);
    if (length < 0)
    {
        return false;
    }
    if (length < DNS_HEADER_SIZE || !(packet[2] & 0x80))
    {
        return true;
    }
    const uint16_t id = read_u16(packet);
    for (size_t i = 0; i < DNS_PENDING_MAX; ++i)
    {
        pending_query_t *slot = &pending[i];
        if (slot->active && slot->forwarded_id == id)
        {
            if (!answer_logged)
            {
                ESP_LOGI(TAG, "El servidor DNS de origen respondió (RCODE=%u)", (unsigned)(packet[3] & 0x0f));
                answer_logged = true;
            }
            if (length > DNS_PACKET_MAX)
            {
                // Signal truncation explicitly instead of forwarding corrupt data.
                send_error(listener, &slot->client, slot->query, slot->query_size, 0, true);
            }
            else
            {
                packet[0] = slot->query[0];
                packet[1] = slot->query[1];
                sendto(listener, packet, length, 0,
                       (const struct sockaddr *)&slot->client, sizeof(slot->client));
            }
            slot->active = false;
            break;
        }
    }
    return true;
}

static void dns_task(void *arg)
{
    const int listener = (int)(intptr_t)arg;
    int upstream = -1;
    unsigned generation = atomic_load(&upstream_generation);
    uint32_t destination = atomic_load(&upstream_address);
    uint16_t next_id = (uint16_t)esp_random();
    TickType_t last_attempt = xTaskGetTickCount() - pdMS_TO_TICKS(DNS_RETRY_MS);

    for (;;)
    {
        const TickType_t now = xTaskGetTickCount();
        const unsigned current_generation = atomic_load(&upstream_generation);
        if (generation != current_generation)
        {
            atomic_store(&upstream_connected, false);
            if (upstream >= 0)
            {
                close(upstream);
                upstream = -1;
            }
            fail_pending(listener);
            generation = current_generation;
            destination = atomic_load(&upstream_address);
            answer_logged = false;
            last_attempt = now - pdMS_TO_TICKS(DNS_RETRY_MS);
        }
        if (upstream < 0 && destination != 0 &&
            now - last_attempt >= pdMS_TO_TICKS(DNS_RETRY_MS))
        {
            last_attempt = now;
            const struct sockaddr_in remote = {
                .sin_family = AF_INET,
                .sin_port = htons(DNS_PORT),
                .sin_addr.s_addr = destination,
            };
            upstream = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            // This socket is never bound to the AP address: route via the STA.
            if (upstream >= 0 && connect(upstream, (const struct sockaddr *)&remote, sizeof(remote)) == 0)
            {
                atomic_store(&connected_generation, generation);
                atomic_store(&upstream_connected, true);
            }
            else
            {
                if (upstream >= 0)
                {
                    close(upstream);
                    upstream = -1;
                }
            }
        }
        for (size_t i = 0; i < DNS_PENDING_MAX; ++i)
        {
            if (pending[i].active && now - pending[i].started >= pdMS_TO_TICKS(DNS_TIMEOUT_MS))
            {
                send_error(listener, &pending[i].client, pending[i].query,
                           pending[i].query_size, 2, false);
                pending[i].active = false;
            }
        }

        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(listener, &reads);
        int max_fd = listener;
        if (upstream >= 0)
        {
            FD_SET(upstream, &reads);
            if (upstream > max_fd)
            {
                max_fd = upstream;
            }
        }
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        const int ready = select(max_fd + 1, &reads, NULL, NULL, &timeout);
        if (ready < 0)
        {
            if (errno != EINTR)
            {
                ESP_LOGW(TAG, "Error de socket DNS: %d", errno);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        if (upstream >= 0 && FD_ISSET(upstream, &reads) && !receive_answer(listener, upstream))
        {
            atomic_store(&upstream_connected, false);
            close(upstream);
            upstream = -1;
            fail_pending(listener);
        }
        if (FD_ISSET(listener, &reads))
        {
            receive_query(listener, upstream, &next_id);
        }
    }
}

esp_err_t dns_proxy_start(uint32_t listen_addr)
{
    if (atomic_load(&proxy_started))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (listen_addr == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const int listener = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listener < 0)
    {
        return ESP_FAIL;
    }
    const struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = listen_addr,
    };
    if (bind(listener, (const struct sockaddr *)&local, sizeof(local)) != 0)
    {
        close(listener);
        return ESP_FAIL;
    }
    ap_address = listen_addr;
    atomic_store(&proxy_started, true);
    if (xTaskCreate(dns_task, "dns_proxy", 4096, (void *)(intptr_t)listener, 5, NULL) != pdPASS)
    {
        atomic_store(&proxy_started, false);
        close(listener);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "DNS local listo en el punto de acceso (UDP/53)");
    return ESP_OK;
}

void dns_proxy_set_upstream(uint32_t address)
{
    if (!usable_upstream(address))
    {
        address = 0;
    }
    if (atomic_exchange(&upstream_address, address) != address)
    {
        atomic_store(&upstream_connected, false);
        atomic_fetch_add(&upstream_generation, 1);
    }
}

bool dns_proxy_is_ready(void)
{
    return atomic_load(&proxy_started) && atomic_load(&upstream_connected) &&
           atomic_load(&upstream_address) != 0 &&
           atomic_load(&connected_generation) == atomic_load(&upstream_generation);
}
