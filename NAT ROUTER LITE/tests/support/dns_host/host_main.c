#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <string.h>
#include "dns_proxy.h"

int main(void)
{
    WSADATA startup;
    if (WSAStartup(MAKEWORD(2,2), &startup) != 0) return 1;
    if (dns_proxy_start(inet_addr("127.0.0.1")) != ESP_OK) return 2;
    puts("READY"); fflush(stdout);
    char line[128];
    while (fgets(line, sizeof(line), stdin))
    {
        if (strncmp(line, "quit", 4) == 0) break;
        if (strncmp(line, "offline", 7) == 0) dns_proxy_set_upstream(0);
        else if (strncmp(line, "online ", 7) == 0)
        {
            char *end = strchr(line + 7, ' ');
            if (!end) end = strchr(line + 7, '\n');
            if (end) *end = '\0';
            dns_proxy_set_upstream(inet_addr(line + 7));
        }
        else return 3;
        puts("OK"); fflush(stdout);
    }
    return 0;
}
