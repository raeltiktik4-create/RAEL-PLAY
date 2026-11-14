/**
 *  iface.c - RAELPLAY_IFACE resolution hook
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

#include "iface.h"
#include "netutils.h"
#include "win_usb.h"

void iface_bind_from_env(void)
{
    const char *arg = getenv("RAELPLAY_IFACE");
    char ip[64] = { 0 };
    char err[512] = { 0 };

    if (!arg || !*arg) {
        return;
    }
    if (!strcmp(arg, "list")) {
        win_usb_print_adapters();
        exit(0);
    }
    if (!win_usb_resolve_iface(arg, ip, sizeof(ip), err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        exit(1);
    }
    if (ip[0]) {
        netutils_set_bind_address(ip);
        fprintf(stderr, "***** RAELPLAY_IFACE=%s: todos os sockets amarrados em %s\n",
                arg, ip);
    } else {
        fprintf(stderr, "***** RAELPLAY_IFACE=auto: sem tethering USB, escutando em todas as interfaces\n");
    }
}

#else

#include "iface.h"

void iface_bind_from_env(void) { }

#endif