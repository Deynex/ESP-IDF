#include "wifi_form.h"

#include <stddef.h>
#include <string.h>

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A NULL destination validates and counts a component without storing it. */
static bool decode_component(const char *src, size_t length, char *dst,
                             size_t capacity, size_t *decoded_length)
{
    size_t written = 0;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)src[i];
        if (c == '%')
        {
            if (length - i < 3) return false;
            int high = hex_value((unsigned char)src[i + 1]);
            int low = hex_value((unsigned char)src[i + 2]);
            if (high < 0 || low < 0) return false;
            c = (unsigned char)((high << 4) | low);
            i += 2;
        }
        else if (c == '+')
        {
            c = ' ';
        }

        if (c == '\0') return false;
        if (dst != NULL)
        {
            if (written + 1 >= capacity) return false;
            dst[written] = (char)c;
        }
        ++written;
    }
    if (dst != NULL) dst[written] = '\0';
    if (decoded_length != NULL) *decoded_length = written;
    return true;
}

bool wifi_form_parse(const char *form, char ssid[33], char password[65])
{
    if (form == NULL || ssid == NULL || password == NULL) return false;

    char parsed_ssid[33] = {0};
    char parsed_password[65] = {0};
    bool have_ssid = false;
    bool have_password = false;
    const char *field = form;

    while (*field != '\0')
    {
        const char *end = strchr(field, '&');
        if (end == NULL) end = field + strlen(field);
        if (end == field)
        {
            field = end + 1;
            continue;
        }
        const char *equals = memchr(field, '=', (size_t)(end - field));
        if (equals == NULL) return false;

        size_t key_length;
        if (!decode_component(field, (size_t)(equals - field), NULL, 0, &key_length)) return false;
        char key[9] = {0};
        if (key_length == 4 || key_length == 8)
        {
            if (!decode_component(field, (size_t)(equals - field), key, sizeof(key), NULL)) return false;
        }

        const char *value = equals + 1;
        size_t length = (size_t)(end - value);
        if (strcmp(key, "ssid") == 0)
        {
            if (have_ssid || !decode_component(value, length, parsed_ssid, sizeof(parsed_ssid), NULL)) return false;
            have_ssid = true;
        }
        else if (strcmp(key, "password") == 0)
        {
            if (have_password || !decode_component(value, length, parsed_password, sizeof(parsed_password), NULL)) return false;
            have_password = true;
        }
        else if (!decode_component(value, length, NULL, 0, NULL))
        {
            return false;
        }

        field = (*end == '&') ? end + 1 : end;
    }

    size_t password_length = strlen(parsed_password);
    if (!have_ssid || !have_password || parsed_ssid[0] == '\0' || password_length < 8) return false;
    if (password_length == 64)
    {
        for (size_t i = 0; i < password_length; ++i)
        {
            if (hex_value((unsigned char)parsed_password[i]) < 0) return false;
        }
    }

    memcpy(ssid, parsed_ssid, sizeof(parsed_ssid));
    memcpy(password, parsed_password, sizeof(parsed_password));
    return true;
}
