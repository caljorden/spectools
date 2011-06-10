/* Metageek WiSPY network protocol interface 
 * Mike Kershaw/Dragorn <dragorn@kismetwireless.net>
 *
 * Generic WiSPY userspace container for the Metageek WiSPY
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
 * Extra thanks to Ryan Woodings @ Metageek for interface documentation
 */

/*
 * Implementation of the spectool network protocol
 * {published url goes here}
 *
 * All multibyte values are expected to be big-endian
 *
 * Frames may contain multiple blocks.  These blocks must be of the same
 * type.  Stacking is at the discretion of the sender, and is intended for
 * optimization to the TCP window size
 *
 * All sub-frame types contain a total frame length for future expandability.
 *
 * !!! NOTE !!!
 * THIS IS NOT THE FINAL SPECTOOL PROTOCOL, DON'T USE THIS EXPECTING IT TO STAY
 * THE SAME, THIS IS KLUGED TOGETHER AS A PROTOTYPE BEFORE THE FINAL PROTOCOL
 * IS DEFINED BY METAGEEK.
 *
 */

#ifndef __SPECTOOL_NET_H__
#define __SPECTOOL_NET_H__

#include "config.h"

#ifdef HAVE_STDINT
#include <stdint.h>
#endif

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/* WiSPY Network Protocol
 *
 * Multiple (homogenous) subframes may be aggregated into a single frame
 * header at the discretion of the server in order to optimize packing into
 * frames.  
 *
 * Subframes of different types cannot be mixed in a single header.
 */

#define SPECTOOL_NET_FRAME_DEVICE		0x00
#define SPECTOOL_NET_FRAME_SWEEP		0x01
#define SPECTOOL_NET_FRAME_COMMAND		0x02
#define SPECTOOL_NET_FRAME_MESSAGE		0x03

#define SPECTOOL_NET_SENTINEL			0xDECAFBAD

#define SPECTOOL_NET_PROTO_VERSION		0x01

#define SPECTOOL_NET_DEFAULT_PORT		30569

typedef struct _spectool_fr_header {
	uint32_t sentinel;
	uint16_t frame_len;
	uint8_t proto_version;
	uint8_t block_type;
	uint8_t num_blocks;
	uint8_t data[0];
} __attribute__ ((packed)) spectool_fr_header;
/* Size of a container header */
#define spectool_fr_header_size()		(sizeof(spectool_fr_header))

#define SPECTOOL_NET_SWEEPTYPE_CUR		0x01
#define SPECTOOL_NET_SWEEPTYPE_AVG		0x02
#define SPECTOOL_NET_SWEEPTYPE_PEAK	0x03

typedef struct _spectool_fr_sweep {
	uint16_t frame_len;
	uint32_t device_id;
	uint8_t sweep_type;
	uint32_t start_sec;
	uint32_t start_usec;
	uint8_t sample_data[0];
} __attribute__ ((packed)) spectool_fr_sweep;
/* Size of a sweep of N samples */
#define spectool_fr_sweep_size(x)		(sizeof(spectool_fr_sweep) + (x))

#define SPECTOOL_NET_DEVTYPE_USB1		0x01
#define SPECTOOL_NET_DEVTYPE_USB2		0x02
#define SPECTOOL_NET_DEVTYPE_LASTDEV	0xFF

#define SPECTOOL_NET_DEVFLAG_NONE		0x00
#define SPECTOOL_NET_DEVFLAG_VARSWEEP	0x01
#define SPECTOOL_NET_DEVFLAG_LOCKED	0x02

typedef struct _spectool_fr_device {
	uint16_t frame_len;
	uint8_t device_version;
	uint16_t device_flags;
	uint32_t device_id;
	uint8_t device_name_len;
	uint8_t device_name[256];

	uint32_t amp_offset_mdbm;
	uint32_t amp_res_mdbm;
	uint16_t rssi_max;

	uint32_t def_start_khz;
	uint32_t def_res_hz;
	uint16_t def_num_samples;

	uint32_t start_khz;
	uint32_t res_hz;
	uint16_t num_samples;
} __attribute__ ((packed)) spectool_fr_device;
/* Size of a device frame of N sample definitions */
#define spectool_fr_device_size()		(sizeof(spectool_fr_device))

#define SPECTOOL_NET_TXTTYPE_INFO		0x00
#define SPECTOOL_NET_TXTTYPE_ERROR		0x01
#define SPECTOOL_NET_TXTTYPE_FATAL		0x02
typedef struct _spectool_fr_txtmessage {
	uint16_t frame_len;
	uint8_t message_type;
	uint16_t message_len;
	uint8_t message[0];
} __attribute__ ((packed)) spectool_fr_txtmessage;
/* Size of a text message of N characters */
#define spectool_fr_txtmessage_size(x)	(sizeof(spectool_fr_txtmessage) + (x))

#define SPECTOOL_NET_COMMAND_NULL			0x00
#define SPECTOOL_NET_COMMAND_ENABLEDEV		0x01
#define SPECTOOL_NET_COMMAND_DISABLEDEV	0x02
#define SPECTOOL_NET_COMMAND_SETSCAN		0x03
#define SPECTOOL_NET_COMMAND_LOCK			0x04
#define SPECTOOL_NET_COMMAND_UNLOCK		0x05
typedef struct _spectool_fr_command {
	uint16_t frame_len;
	uint8_t command_id;
	uint16_t command_len;
	uint8_t command_data[0];
} __attribute__ ((packed)) spectool_fr_command;
#define spectool_fr_command_size(x)	(sizeof(spectool_fr_command) + (x))

typedef struct _spectool_fr_command_enabledev {
	uint32_t device_id;
} __attribute__ ((packed)) spectool_fr_command_enabledev;
#define spectool_fr_command_enabledev_size(x)	(sizeof(spectool_fr_command_enabledev))

typedef struct _spectool_fr_command_disabledev {
	uint32_t device_id;
} __attribute__ ((packed)) spectool_fr_command_disabledev;
#define spectool_fr_command_disabledev_size(x)	(sizeof(spectool_fr_command_disabledev))

typedef struct _spectool_fr_command_setscan {
	uint32_t device_id;
	uint32_t start_khz;
	uint32_t res_hz;
	uint32_t filter_bw_hz;
	uint16_t sweep_points;
	uint8_t lock_dev;
} __attribute__ ((packed)) spectool_fr_command_setscan;
#define spectool_fr_command_setscan_size(x)	(sizeof(spectool_fr_command_setscan))

typedef struct _spectool_fr_command_lockdev {
	uint32_t device_id;
} __attribute__ ((packed)) spectool_fr_command_lockdev;
#define spectool_fr_command_lockdev_size(x)	(sizeof(spectool_fr_command_lockdev))

typedef struct _spectool_fr_command_unlockdev {
	uint32_t device_id;
} __attribute__ ((packed)) spectool_fr_command_unlockdev;
#define spectool_fr_command_unlockdev_size(x)	(sizeof(spectool_fr_command_unlockdev))

typedef struct _spectool_fr_broadcast {
	uint32_t sentinel;
	uint8_t version;
	uint16_t server_port;
} __attribute__ ((packed)) spectool_fr_broadcast;

#endif

