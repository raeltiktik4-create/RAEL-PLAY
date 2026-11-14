/**
 *  win_usb.h - Windows USB tethering detection for UxPlay / RaelPlay
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 */

#ifndef WIN_USB_H
#define WIN_USB_H

#include <stddef.h>

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

/* Fills out with the IPv4 address of an Apple USB tethering adapter.
 * Returns 1 on success, 0 if no such adapter is up. */
int win_usb_tether_ipv4(char *out, size_t outlen);

/* 1 = an Apple device is on the USB bus, 0 = none, -1 = usbmuxd not compiled in */
int win_usb_apple_device_present(void);

/* Resolves an iface argument ("usb", "auto", "1.2.3.4").
 * Returns 1 on success; an empty out string means "listen everywhere".
 * Returns 0 on failure and fills err. */
int win_usb_resolve_iface(const char *arg, char *out, size_t outlen, char *err, size_t errlen);

/* Dumps every adapter with state, IPv4 and whether it looks like Apple USB. */
void win_usb_print_adapters(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* WIN_USB_H */