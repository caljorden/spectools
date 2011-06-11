/*
 * Ubertooth1 interface, for the Ubertooth One hardware
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __UBERTOOTH_HW_U1_H__
#define __UBERTOOTH_HW_U1_H__

#include "spectool_container.h"

/* U1 scan results */
typedef struct _ubertooth_u1_usb_pair {
	char bus[64];
	char dev[64];
} ubertooth_u1_usb_pair;

int ubertooth_u1_device_scan(spectool_device_list *list);

int ubertooth_u1_init_path(spectool_phy *phydev, char *buspath, char *devpath);
int ubertooth_u1_init(spectool_phy *phydev, spectool_device_rec *rec);

#endif
