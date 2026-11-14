/**
 *  iface.h - RAELPLAY_IFACE resolution hook
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 */

#ifndef IFACE_H
#define IFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reads RAELPLAY_IFACE and binds every socket of this process to the chosen
 * interface. No-op when the variable is unset or on non-Windows builds. */
void iface_bind_from_env(void);

#ifdef __cplusplus
}
#endif

#endif /* IFACE_H */