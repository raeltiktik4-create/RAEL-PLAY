/**
 *  win_usb.c - Windows USB tethering detection for UxPlay / RaelPlay
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 */

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "win_usb.h"

#ifdef HAVE_USBMUXD
#include <usbmuxd.h>
#endif

#define ADAPTER_ATTEMPTS 3
#define ADAPTER_BUFFER   16384

static void wide_to_narrow(char *dst, size_t dstlen, const wchar_t *src)
{
    if (!dst || dstlen == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int) dstlen, NULL, NULL);
    dst[dstlen - 1] = '\0';
}

static void ascii_lower(char *dst, size_t dstlen, const char *src)
{
    size_t i = 0;
    if (!dst || dstlen == 0) {
        return;
    }
    for (; src && src[i] && i + 1 < dstlen; i++) {
        dst[i] = (char) tolower((unsigned char) src[i]);
    }
    dst[i] = '\0';
}

static int looks_like_apple_usb(const wchar_t *description, const wchar_t *friendly)
{
    static const char *keys[] = {
        "apple mobile device",
        "apple usb",
        "remote ndis",
        "rndis",
        "usb ncm",
        "iphone",
        "ipad",
        NULL
    };
    char low_desc[256];
    char low_friendly[256];
    char tmp[256];

    low_desc[0] = '\0';
    low_friendly[0] = '\0';

    if (description) {
        wide_to_narrow(tmp, sizeof(tmp), description);
        ascii_lower(low_desc, sizeof(low_desc), tmp);
    }
    if (friendly) {
        wide_to_narrow(tmp, sizeof(tmp), friendly);
        ascii_lower(low_friendly, sizeof(low_friendly), tmp);
    }

    for (int i = 0; keys[i]; i++) {
        if (strstr(low_desc, keys[i]) || strstr(low_friendly, keys[i])) {
            return 1;
        }
    }
    return 0;
}

/* 172.20.10.0/28 is the iOS personal hotspot subnet */
static int ipv4_is_tether_subnet(unsigned int host_order)
{
    return ((host_order & 0xFFFFFFF0u) == 0xAC140A00u);
}

static IP_ADAPTER_ADDRESSES *get_adapters(void)
{
    ULONG size = ADAPTER_BUFFER;

    for (int attempt = 0; attempt < ADAPTER_ATTEMPTS; attempt++) {
        IP_ADAPTER_ADDRESSES *addrs = (IP_ADAPTER_ADDRESSES *) malloc(size);
        ULONG ret;

        if (!addrs) {
            return NULL;
        }
        ret = GetAdaptersAddresses(AF_INET,
                                   GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                   GAA_FLAG_SKIP_DNS_SERVER,
                                   NULL, addrs, &size);
        if (ret == ERROR_SUCCESS) {
            return addrs;
        }
        free(addrs);
        if (ret != ERROR_BUFFER_OVERFLOW) {
            return NULL;
        }
    }
    return NULL;
}

static int adapter_usable(const IP_ADAPTER_ADDRESSES *a)
{
    if (a->OperStatus != IfOperStatusUp) {
        return 0;
    }
    if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        return 0;
    }
    if (a->IfType == IF_TYPE_TUNNEL) {
        return 0;
    }
    return 1;
}

static int adapter_ipv4(const IP_ADAPTER_ADDRESSES *a, char *out, size_t outlen)
{
    for (const IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
        const struct sockaddr_in *sin;
        unsigned int host;

        if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) {
            continue;
        }
        sin = (const struct sockaddr_in *) u->Address.lpSockaddr;
        host = ntohl(sin->sin_addr.s_addr);
        if ((host & 0xFF000000u) == 0x7F000000u) {
            continue;   /* loopback */
        }
        if ((host & 0xFFFF0000u) == 0xA9FE0000u) {
            continue;   /* 169.254 link-local */
        }
        if (!inet_ntop(AF_INET, &sin->sin_addr, out, (socklen_t) outlen)) {
            continue;
        }
        return 1;
    }
    return 0;
}

int win_usb_tether_ipv4(char *out, size_t outlen)
{
    IP_ADAPTER_ADDRESSES *addrs;
    char fallback[64] = { 0 };
    int found = 0;

    if (!out || outlen == 0) {
        return 0;
    }
    out[0] = '\0';

    addrs = get_adapters();
    if (!addrs) {
        return 0;
    }

    for (const IP_ADAPTER_ADDRESSES *a = addrs; a; a = a->Next) {
        char ip[64] = { 0 };

        if (!adapter_usable(a) || !adapter_ipv4(a, ip, sizeof(ip))) {
            continue;
        }
        if (looks_like_apple_usb(a->Description, a->FriendlyName)) {
            snprintf(out, outlen, "%s", ip);
            found = 1;
            break;
        }
        if (!fallback[0]) {
            struct in_addr tmp;
            if (inet_pton(AF_INET, ip, &tmp) == 1 &&
                ipv4_is_tether_subnet(ntohl(tmp.s_addr))) {
                snprintf(fallback, sizeof(fallback), "%s", ip);
            }
        }
    }
    free(addrs);

    if (!found && fallback[0]) {
        snprintf(out, outlen, "%s", fallback);
        found = 1;
    }
    return found;
}

int win_usb_apple_device_present(void)
{
#ifdef HAVE_USBMUXD
    usbmuxd_device_info_t *list = NULL;
    int present = 0;
    int count = usbmuxd_get_device_list(&list);

    if (count > 0 && list) {
        for (int i = 0; i < count; i++) {
            if (list[i].conn_type == CONNECTION_TYPE_USB) {
                present = 1;
                break;
            }
        }
        usbmuxd_device_list_free(&list);
    }
    return present;
#else
    return -1;
#endif
}

int win_usb_resolve_iface(const char *arg, char *out, size_t outlen, char *err, size_t errlen)
{
    struct in_addr tmp;
    int usb_present;

    if (!out || outlen == 0) {
        return 0;
    }
    out[0] = '\0';
    if (err && errlen) {
        err[0] = '\0';
    }
    if (!arg || !*arg) {
        arg = "usb";
    }

    if (!strcmp(arg, "usb")) {
        usb_present = win_usb_apple_device_present();
        if (win_usb_tether_ipv4(out, outlen)) {
            return 1;
        }
        if (err && errlen) {
            snprintf(err, errlen,
                     "RAELPLAY_IFACE=usb: nenhuma interface de tethering USB encontrada.\n"
                     "  1) conecte o iPhone por cabo   2) toque em \"Confiar\"\n"
                     "  3) Ajustes > Ponto de Acesso Pessoal > Ligado\n"
                     "  4) rode \"RAELPLAY_IFACE=list uxplay.exe\" para ver o que o Windows enxerga%s",
                     usb_present == 0
                         ? "\n  (ha um dispositivo Apple no barramento USB, mas o adaptador de rede esta DOWN)"
                         : "");
        }
        return 0;
    }

    if (!strcmp(arg, "auto")) {
        if (win_usb_tether_ipv4(out, outlen)) {
            return 1;
        }
        out[0] = '\0';   /* vazio = escutar em todas as interfaces */
        return 1;
    }

    if (inet_pton(AF_INET, arg, &tmp) == 1) {
        snprintf(out, outlen, "%s", arg);
        return 1;
    }

    if (err && errlen) {
        snprintf(err, errlen,
                 "RAELPLAY_IFACE=\"%s\": esperado \"usb\", \"auto\", \"list\" ou um endereco IPv4", arg);
    }
    return 0;
}

void win_usb_print_adapters(void)
{
    IP_ADAPTER_ADDRESSES *addrs = get_adapters();

    if (!addrs) {
        printf("win_usb: GetAdaptersAddresses falhou (%lu)\n", GetLastError());
        return;
    }
    printf("%-6s %-18s %-5s %s\n", "ESTADO", "IPv4", "USB?", "NOME | DESCRICAO");
    for (const IP_ADAPTER_ADDRESSES *a = addrs; a; a = a->Next) {
        char name[128];
        char desc[256];
        char ip[64] = "-";

        wide_to_narrow(name, sizeof(name), a->FriendlyName);
        wide_to_narrow(desc, sizeof(desc), a->Description);
        adapter_ipv4(a, ip, sizeof(ip));

        printf("%-6s %-18s %-5s %s | %s\n",
               a->OperStatus == IfOperStatusUp ? "up" : "down",
               ip,
               looks_like_apple_usb(a->Description, a->FriendlyName) ? "SIM" : "",
               name, desc);
    }
    free(addrs);
}

#endif /* _WIN32 */