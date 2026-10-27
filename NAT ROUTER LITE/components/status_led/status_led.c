#include "status_led.h"
#include "sdkconfig.h"

#if !CONFIG_NAT_STATUS_LED_NONE
#include "driver/gpio.h"
#include "esp_log.h"

#if CONFIG_NAT_STATUS_LED_RGB
#include "driver/rmt_tx.h"
#include "soc/soc_caps.h"

static rmt_channel_handle_t led_channel;
static rmt_encoder_handle_t led_encoder;
static bool channel_enabled;
// La trama completa cabe en los 48 símbolos del canal RMT del ESP32-S3.
// El buffer permanece válido incluso si una transmisión agota su timeout.
static rmt_symbol_word_t led_symbols[25];

static void rgb_cleanup(void)
{
    if (channel_enabled)
    {
        if (rmt_disable(led_channel) != ESP_OK)
        {
            return;
        }
        channel_enabled = false;
    }
    if (led_encoder)
    {
        rmt_del_encoder(led_encoder);
        led_encoder = NULL;
    }
    if (led_channel)
    {
        rmt_del_channel(led_channel);
        led_channel = NULL;
    }
}

static esp_err_t rgb_write(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint32_t grb = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
    for (int bit = 0; bit < 24; ++bit)
    {
        const bool one = (grb & (1UL << (23 - bit))) != 0;
        led_symbols[bit] = (rmt_symbol_word_t){
            .level0 = 1,
            .duration0 = one ? 7 : 3,
            .level1 = 0,
            .duration1 = one ? 6 : 9,
        };
    }
    // 300 us en nivel bajo; compatible con el reset de WS2812 de >=280 us.
    led_symbols[24] = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = 1500,
        .level1 = 0,
        .duration1 = 1500,
    };
    const rmt_transmit_config_t tx_config = {.loop_count = 0};
    esp_err_t err = rmt_transmit(led_channel, led_encoder, led_symbols,
                               sizeof(led_symbols), &tx_config);
    if (err == ESP_OK)
    {
        err = rmt_tx_wait_all_done(led_channel, 100);
    }
    return err;
}
#endif

static const char *TAG = "STATUS_LED";
static bool led_ready;
static bool blink_on;
#if CONFIG_NAT_STATUS_LED_GPIO
#ifdef CONFIG_NAT_STATUS_LED_ACTIVE_LOW
static const bool active_low = true;
#else
static const bool active_low = false;
#endif
#endif
#endif

void status_led_init(void)
{
#if !CONFIG_NAT_STATUS_LED_NONE
    const gpio_num_t pin = CONFIG_NAT_STATUS_LED_GPIO_NUM;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin))
    {
        ESP_LOGW(TAG, "GPIO %d no válido para el LED; indicador desactivado", pin);
        return;
    }

#if CONFIG_NAT_STATUS_LED_RGB
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 1,
        .flags.with_dma = false,
    };
    const rmt_copy_encoder_config_t encoder_config = {};
    esp_err_t err = rmt_new_tx_channel(&channel_config, &led_channel);
    if (err == ESP_OK)
    {
        err = rmt_new_copy_encoder(&encoder_config, &led_encoder);
    }
    if (err == ESP_OK)
    {
        err = rmt_enable(led_channel);
        channel_enabled = err == ESP_OK;
    }
#else
    esp_err_t err = gpio_reset_pin(pin);
    if (err == ESP_OK)
    {
        err = gpio_set_level(pin, active_low ? 1 : 0);
    }
    if (err == ESP_OK)
    {
        err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    }
#endif
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "No se pudo iniciar el LED: %s; indicador desactivado", esp_err_to_name(err));
#if CONFIG_NAT_STATUS_LED_RGB
        rgb_cleanup();
#endif
        return;
    }
    led_ready = true;
    status_led_update(false);
#endif
}

void status_led_update(bool connected)
{
#if !CONFIG_NAT_STATUS_LED_NONE
    if (!led_ready)
    {
        return;
    }
    blink_on = !blink_on;
    const bool on = connected || blink_on;
#if CONFIG_NAT_STATUS_LED_RGB
    const uint8_t brightness = CONFIG_NAT_STATUS_LED_BRIGHTNESS;
    esp_err_t err = rgb_write(on && !connected ? brightness : 0,
                             on ? (connected ? brightness : (brightness + 1) / 2) : 0,
                             0);
#else
    const int level = active_low ? !on : on;
    esp_err_t err = gpio_set_level(CONFIG_NAT_STATUS_LED_GPIO_NUM, level);
#endif
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Error al actualizar el LED: %s; indicador desactivado", esp_err_to_name(err));
        led_ready = false;
#if CONFIG_NAT_STATUS_LED_RGB
        rgb_cleanup();
#endif
    }
#else
    (void)connected;
#endif
}
