/* 
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
 * Extra thanks to Ryan Woodings @ Metageek for interface documentation
 */

#ifndef __WISPY_HW_GEN1_H__
#define __WISPY_HW_GEN1_H__

#include "spectool_container.h"

/* Wispy1 device scan results */
typedef struct _wispy1_usb_pair {
	char bus[64];
	char dev[64];
} wispy1_usb_pair;

int wispy1_usb_device_scan(spectool_device_list *list);

/* Wispy1 init function to build a phydev linked to a bus and device path
 * scanned */
int wispy1_usb_init_path(spectool_phy *phydev, char *buspath, char *devpath);
int wispy1_usb_init(spectool_phy *phydev, spectool_device_rec *rec);

#endif

