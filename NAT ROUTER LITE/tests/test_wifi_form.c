#include "wifi_form.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(bool condition, const char *description)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", description);
        exit(EXIT_FAILURE);
    }
}

static void accepted(const char *form, const char *expected_ssid, const char *expected_password)
{
    char ssid[33];
    char password[65];
    expect(wifi_form_parse(form, ssid, password), form);
    expect(strcmp(ssid, expected_ssid) == 0, "decoded SSID");
    expect(strcmp(password, expected_password) == 0, "decoded password");
}

static void rejected(const char *form)
{
    char ssid[33] = "unchanged";
    char password[65] = "unchanged";
    expect(!wifi_form_parse(form, ssid, password), form);
    expect(strcmp(ssid, "unchanged") == 0 && strcmp(password, "unchanged") == 0,
           "failed form leaves outputs unchanged");
}

int main(void)
{
    accepted("ssid=Casa&password=12345678", "Casa", "12345678");
    accepted("password=a%26b%3Dc%2B%25d&ssid=Casa+%26+Oficina%3D1%2B",
             "Casa & Oficina=1+", "a&b=c+%d");
    accepted("ssid=Red+Ni%C3%B1o&password=12345678", "Red Ni\xC3\xB1o", "12345678");
    accepted("%73sid=Casa&pass%77ord=12345678&submit=Conectar", "Casa", "12345678");
    accepted("ssid=Casa&&ignored_long_key=some%26value&password=12345678&", "Casa", "12345678");

    char ssid32[34];
    memset(ssid32, 's', 32);
    ssid32[32] = '\0';
    char pass63[66];
    memset(pass63, 'p', 63);
    pass63[63] = '\0';
    char form[400];
    snprintf(form, sizeof(form), "ssid=%s&password=%s", ssid32, pass63);
    accepted(form, ssid32, pass63);

    char hex64[65];
    memset(hex64, 'a', 64);
    hex64[64] = '\0';
    snprintf(form, sizeof(form), "ssid=Casa&password=%s", hex64);
    accepted(form, "Casa", hex64);
    hex64[0] = 'G';
    snprintf(form, sizeof(form), "ssid=Casa&password=%s", hex64);
    rejected(form);
    ssid32[32] = 's';
    ssid32[33] = '\0';
    snprintf(form, sizeof(form), "ssid=%s&password=12345678", ssid32);
    rejected(form);
    memset(pass63, 'a', 65);
    pass63[65] = '\0';
    snprintf(form, sizeof(form), "ssid=Casa&password=%s", pass63);
    rejected(form);

    const char *invalid[] = {
        "", "ssid=Casa", "password=12345678", "ssid=&password=12345678",
        "ssid=Casa&password=", "ssid=Casa&password=1234567",
        "ssid=Casa&password=12345678&ssid=Otra",
        "ssid=Casa&password=12345678&%73sid=Otra",
        "ssid=Casa&password=12345678&password=87654321",
        "ssid=Ca%00sa&password=12345678", "ssid=Casa&password=1234%005678",
        "ssid=Casa%&password=12345678", "ssid=Casa%2&password=12345678",
        "ssid=Casa%GG&password=12345678", "ssid=Casa&password=12345678%",
        "ss%00id=Casa&password=12345678", "ssid=Casa&password=12345678&x=%ZZ",
        "ssid=Casa&password=12345678&x=%00", "ssid=Casa&password=12345678&invalid"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) rejected(invalid[i]);
    expect(!wifi_form_parse(NULL, ssid32, hex64), "NULL form");
    expect(!wifi_form_parse("", NULL, hex64), "NULL SSID");
    expect(!wifi_form_parse("", ssid32, NULL), "NULL password");
    puts("wifi_form: all tests passed");
    return EXIT_SUCCESS;
}
