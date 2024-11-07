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
 * Extra thanks to all @ Metageek for interface documentation
 */

#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#ifdef HAVE_VALUES_H
#include <values.h>
#endif

#ifdef SYS_LINUX
/* Needed for our own tools */
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

/* Kluge around kernel internal/external header breakage */
#ifndef __user
#define __user
#endif

#include <linux/usbdevice_fs.h>
#include <errno.h>
/* 
 * Miserable hack to fix some distros which bundle a modern kernel but didn't
 * update their linux/usbdevice_fs.h files.  We define the ioctl locally, in
 * theory the worst that could happen is that the kernel rejects it anyhow.
 */
#ifndef USBDEVFS_DISCONNECT
#warning "Kernel headers dont define USB disconnect support, trying to fake it"
#define USBDEVFS_DISCONNECT        _IO('U', 22)
#endif
#endif /* linux hack */

/* LibUSB stuff */
#include <usb.h>

/* USB HID functions from specs which aren't defined for us */
#define HID_GET_REPORT 0x01
#define HID_SET_REPORT 0x09
#define TIMEOUT	9000

#define METAGEEK_WISPYDBx_VID		0x1dd5
#define METAGEEK_WISPYDBx_PID		0x5000

#define METAGEEK_WISPYDBx_V2_VID	0x1dd5
#define METAGEEK_WISPYDBx_V2_PID	0x5001

#define METAGEEK_WISPYDBx_V3_VID	0x1dd5
#define METAGEEK_WISPYDBx_V3_PID	0x5002

#define METAGEEK_WISPY24I_VID		0x1dd5
#define METAGEEK_WISPY24I_PID		0x2400

#define METAGEEK_WISPY24x_V2_VID	0x1dd5
#define METAGEEK_WISPY24x_V2_PID	0x2410

// 900x v2 uses the same vid/pid, we detect via bcdDevice
#define METAGEEK_WISPY900x_VID		0x1dd5
#define METAGEEK_WISPY900x_PID		0x0900

#define METAGEEK_WISPY950x_VID		0x1dd5
#define METAGEEK_WISPY950x_PID		0x0950

/* Default config */
#define WISPYDBx_USB_STARTKHZ_58		5100000
#define WISPYDBx_USB_RESHZ_58			748536
#define WISPYDBx_USB_FILTERHZ_58		375000
#define WISPYDBx_USB_NUM_SAMPLES_58		1024
#define WISPYDBx_USB_SAMPLESPOINT_58	1

/* u-nii lower (5400mhz) */
#define WISPYDBx_USB_STARTKHZ_58_UNL		5100000
#define WISPYDBx_USB_RESHZ_58_UNL			374268
#define WISPYDBx_USB_FILTERHZ_58_UNL		375000
#define WISPYDBx_USB_NUM_SAMPLES_58_UNL		1024
#define WISPYDBx_USB_SAMPLESPOINT_58_UNL	1

/* Used for DBx and 24i */
#define WISPYDBx_USB_DEF_STARTKHZ_24		2400000
#define WISPYDBx_USB_DEF_RESHZ_24			199951
#define WISPYDBx_USB_DEF_FILTERHZ_24		203125
#define WISPYDBx_USB_DEF_SAMPLESPOINT_24	4
#define WISPYDBx_USB_NUM_SAMPLES_24			419

/* Used for DBx and 24i */
#define WISPYDBx_USB_DEF_STARTKHZ_24_FAST		2400000
#define WISPYDBx_USB_DEF_RESHZ_24_FAST			560000
#define WISPYDBx_USB_DEF_FILTERHZ_24_FAST		500000
#define WISPYDBx_USB_DEF_SAMPLESPOINT_24_FAST	1
#define WISPYDBx_USB_NUM_SAMPLES_24_FAST		150

/* 900x */
#define WISPY900x_USB_DEF_STARTKHZ		902000
#define WISPY900x_USB_DEF_RESHZ			101807
#define WISPY900x_USB_DEF_FILTERHZ		125000
#define WISPY900x_USB_DEF_SAMPLESPOINT	4
#define WISPY900x_USB_NUM_SAMPLES		255

/* Common across all DBx variant firmware */
#define WISPYDBx_USB_OFFSET_MDBM		-134000
#define WISPYDBx_USB_RES_MDBM			500
#define WISPYDBx_USB_RSSI_MAX			222

#define WISPYDBx_MODEL_DBxV1	0
#define WISPYDBx_MODEL_24i		1
#define WISPYDBx_MODEL_900x		2
#define WISPYDBx_MODEL_DBxV2	3
#define WISPYDBx_MODEL_24xV2	4
#define WISPYDBx_MODEL_950x		5
#define WISPYDBx_MODEL_900xV2	6
#define WISPYDBx_MODEL_DBxV3	7

#include "spectool_container.h"
#include "wispy_hw_dbx.h"

#define endian_swap32(x) \
({ \
    uint32_t __x = (x); \
    ((uint32_t)( \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0x000000ff) << 24) | \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0x0000ff00) << 8) | \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0x00ff0000) >> 8) | \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0xff000000) >> 24) )); \
})

#define endian_swap16(x) \
({ \
    uint16_t __x = (x); \
    ((uint16_t)( \
        (uint16_t)(((uint16_t)(__x) & (uint16_t)0x00ff) << 8) | \
        (uint16_t)(((uint16_t)(__x) & (uint16_t)0xff00) >> 8) )); \
})

/* Aux tracking struct for wispydbx characteristics */
typedef struct _wispydbx_usb_aux {
	struct usb_device *dev;
	struct usb_dev_handle *devhdl;

	time_t last_read;

	/* have we pushed a configure event from sweeps */
	int configured;

	/* IPC tracking records to the forked process for capturing data */
	pthread_t usb_thread;
	int usb_thread_alive;

	/* Has the sweep data buffer been initialized?  (ie, did we get a sample at 0) */
	int sweepbuf_initialized;
	/* how many sweeps has this device done over the run time?  Nice to know, and
	 * we can use it for calibration counters too */
	int num_sweeps;

	/* Sweep buffer we maintain and return */
	spectool_sample_sweep *sweepbuf;

	int sockpair[2];

	int sweepbase;

	spectool_phy *phydev;

	int model;

	// Protocol - v1 or v2
	int protocol;

	uint8_t cmd_seq : 2;
} wispydbx_usb_aux;

#define WISPYDBx_USB_ASSEMBLE_CMDFLAGS(seq, len) \
	({ \
	 ((uint8_t)( \
		 (uint8_t)(((uint8_t)(seq) & (uint8_t) 0x3) << 5) | \
		 (uint8_t)(((uint8_t)(len) & (uint8_t) 0x1F)) )); \
    })

typedef struct _wispydbx_rfsettings {
	uint8_t report_id;
	uint8_t command_id;
	uint8_t command_flags;

	uint32_t start_khz;
	uint32_t freq_res_hz;
	uint32_t filter_bw_hz;
	uint16_t points_per_sweep;
	uint8_t samples_per_point;
} __attribute__((packed)) wispydbx_rfsettings;
#define WISPYDBx_USB_RFSETTINGS_LEN		15

typedef struct _wispydbx_rfsettings_v2 {
	uint8_t report_id;
	uint8_t command_id;
	uint8_t command_flags;

	uint32_t start_khz;
	uint32_t freq_res_hz;
	uint32_t filter_bw_hz;
	uint16_t points_per_sweep;

	// Samples per point goes away and becomes dwell time

	uint8_t dwell_time;
	uint8_t dither_steps;
	uint8_t reserved;
} __attribute__((packed)) wispydbx_rfsettings_v2;
#define WISPYDBx_USB_RFSETTINGS_V2_LEN		17

typedef struct _wispydbx_startsweep {
	uint8_t report_id;
	uint8_t command_id;
	uint8_t command_flags;
} __attribute__((packed)) wispydbx_startsweep;
#define WISPYDBx_USB_STARTSWEEP_LEN		0

typedef struct _wispydbx_report {
	uint8_t report_id;
	uint16_t packet_index;
	uint8_t data[61];
} __attribute__((packed)) wispydbx_report;

typedef struct _wispydbx_report_v2 {
	uint8_t report_id;
	uint16_t packet_index;
	uint8_t current_dither;
	uint8_t reserved;
	uint8_t data[59];
} __attribute__((packed)) wispydbx_report_v2;

#ifdef SYS_LINUX
/* Libusb doesn't seem to always provide this, so we'll use our own, taken from the 
* usb_detatch_kernel_driver_np...
*
* THIS IS A HORRIBLE EVIL HACK THAT SHOULDN'T BE DONE, EVER
* 
*/
struct local_usb_ioctl {
	int ifno;
	int ioctl_code;
	void *data;
};

struct ghetto_libusb_devhandle {
	int fd;
	/* Nooo... so bad. */
};

int wispydbx_usb_detach_hack(struct usb_dev_handle *dev, int interface, char *errstr) {
	struct local_usb_ioctl command;
	struct ghetto_libusb_devhandle *gdev;

	command.ifno = interface;
	command.ioctl_code = USBDEVFS_DISCONNECT;
	command.data = NULL;

	gdev = (struct ghetto_libusb_devhandle *) dev;

	if (ioctl(gdev->fd, USBDEVFS_IOCTL, &command) < 0) {
		if (errno == EINVAL) {
			snprintf(errstr, SPECTOOL_ERROR_MAX, "Your kernel doesn't appear to accept "
					 "the USB disconnect command.  Either your kernel is too old and "
					 "does not support device removal, or support for removal has "
					 "been changed by your distribution kernel maintainers.");
		} 

		snprintf(errstr, SPECTOOL_ERROR_MAX, "Could not detatch kernel driver from "
				 "interface %d: %s", interface, strerror(errno));
		return -1;
	}

	return 0;
}
#endif /* sys_linux */

/* Prototypes */
int wispydbx_usb_open(spectool_phy *);
int wispydbx_usb_close(spectool_phy *);
int wispydbx_usb_thread_close(spectool_phy *);
int wispydbx_usb_poll(spectool_phy *);
int wispydbx_usb_getpollfd(spectool_phy *);
void wispydbx_usb_setcalibration(spectool_phy *, int);
int wispydbx_usb_setposition(spectool_phy *, int, int, int);
spectool_sample_sweep *wispydbx_usb_getsweep(spectool_phy *);

void wispydbx_create_settings_from_preset(spectool_sample_sweep *range, 
										  char *name,
										  float startFrequencyMHz,
										  float stopFrequencyMHz,
										  float frequencyResolutionkHz,
										  float filterBandwidthkHz,
										  int model) {
	// Dwell/sample-per-point shared
	int dwellTime;

	// If we're a v1 device set the samples per point based on filter,
	// otherwise use a fixed dwelltime on v2+ devices
	if (model == WISPYDBx_MODEL_DBxV1 ||
		model == WISPYDBx_MODEL_900x ||
		model == WISPYDBx_MODEL_950x) {
		dwellTime = 8;

		if (filterBandwidthkHz > 75)
			dwellTime = 6;

		if (filterBandwidthkHz > 125)
			dwellTime = 4;
	} else {
		dwellTime = 100;
	}

	range->name = strdup(name);
	range->num_samples = (((stopFrequencyMHz * 1000) - (startFrequencyMHz * 1000)) / (frequencyResolutionkHz));
	range->amp_offset_mdbm = WISPYDBx_USB_OFFSET_MDBM;
	range->amp_res_mdbm = WISPYDBx_USB_RES_MDBM;
	range->rssi_max = WISPYDBx_USB_RSSI_MAX;
	range->start_khz = (startFrequencyMHz * 1000);
	range->end_khz = (stopFrequencyMHz * 1000);
	range->res_hz = (frequencyResolutionkHz * 1000);
	range->samples_per_point = dwellTime;
	range->filter_bw_hz = filterBandwidthkHz * 1000;

	return;
}

/* Unified channel range adder */
void wispydbx_add_supportedranges(int *num_ranges, spectool_sample_sweep **ranges, int model) {
	*num_ranges = 0;

	if (model == WISPYDBx_MODEL_DBxV1 ||
		model == WISPYDBx_MODEL_DBxV2 ||
		model == WISPYDBx_MODEL_DBxV3) {
		// DBX devices get 2.4, 2.4 turbo, 5, 5-1, 5-2, 5-3
		*num_ranges = 6; 
	} else if (model == WISPYDBx_MODEL_24i ||
			   model == WISPYDBx_MODEL_24xV2) {
		// 24i and x are 2.4 only, and get just the 2.4 and 2.4 turbo
		*num_ranges = 2;
	} else if (model == WISPYDBx_MODEL_900x ||
			   model == WISPYDBx_MODEL_900xV2) {
		// 900x series gets 2 900mhz-ish ranges
		*num_ranges = 2;
	} else if (model == WISPYDBx_MODEL_950x) {
		// 950x only does one range
		*num_ranges = 1;
	} else {
		// We've failed somehow
		fprintf(stderr, "FAILURE: Couldn't determine device model\n");
		return;
	}

	*ranges = (spectool_sample_sweep *) malloc(sizeof(spectool_sample_sweep) * *num_ranges);

	if (model == WISPYDBx_MODEL_DBxV1 ||
		model == WISPYDBx_MODEL_DBxV2 ||
		model == WISPYDBx_MODEL_DBxV3 ||
		model == WISPYDBx_MODEL_24i ||
		model == WISPYDBx_MODEL_24xV2) {

		wispydbx_create_settings_from_preset(&((*ranges)[0]),
											 "Full 2.4GHz Band", 2400.0f, 2495.0f, 
											 333.3f, 200, model);
		wispydbx_create_settings_from_preset(&((*ranges)[1]),
											 "Full 2.4GHz Band (Turbo)", 2400.0f, 2495.0f, 
											 1000.0f, 500, model);
	}

	if (model == WISPYDBx_MODEL_DBxV1 ||
		model == WISPYDBx_MODEL_DBxV2 ||
		model == WISPYDBx_MODEL_DBxV3) {
		wispydbx_create_settings_from_preset(&((*ranges)[2]),
											 "Full 5GHz Band", 5150.0f, 5836.0f, 
											 1497.070f, 428, model);
		wispydbx_create_settings_from_preset(&((*ranges)[3]),
											 "UNII Low (ch. 36-64)", 5150.0f, 5350.0f, 
											 748.535f, 428, model);
		wispydbx_create_settings_from_preset(&((*ranges)[4]),
											 "UNII Mid (ch. 100-140)", 5470.0f, 5725.0f, 
											 1122.070f, 428, model);
		wispydbx_create_settings_from_preset(&((*ranges)[5]),
											 "UNII High (ch. 149-165)", 5725.0f, 5836.0f, 
											 375.0f, 428, model);
	}

	if (model == WISPYDBx_MODEL_900x ||
		model == WISPYDBx_MODEL_900xV2) {
		wispydbx_create_settings_from_preset(&((*ranges)[0]),
											 "902-928 MHz Band", 902, 928, 
											 102.0f, 125, model);
		wispydbx_create_settings_from_preset(&((*ranges)[1]),
											 "862-870 MHz Band", 862, 870.2f, 
											 38.0f, 53.4f, model);
	}

	if (model == WISPYDBx_MODEL_950x) {
		wispydbx_create_settings_from_preset(&((*ranges)[0]),
											 "950 MHz Band", 945, 964, 
											 75.0f, 90, model);
	}

}

uint32_t wispydbx_adler_checksum(const char *buf1, int len) {
	int i;
	uint32_t s1, s2;
	char *buf = (char *)buf1;
	int CHAR_OFFSET = 0;

	s1 = s2 = 0;
	for (i = 0; i < (len-4); i+=4) {
		s2 += 4*(s1 + buf[i]) + 3*buf[i+1] + 2*buf[i+2] + buf[i+3] + 
			10*CHAR_OFFSET;
		s1 += (buf[i+0] + buf[i+1] + buf[i+2] + buf[i+3] + 4*CHAR_OFFSET); 
	}

	for (; i < len; i++) {
		s1 += (buf[i]+CHAR_OFFSET); s2 += s1;
	}

	return (s1 & 0xffff) + (s2 << 16);
}

/* Scan for devices */
int wispydbx_usb_device_scan(spectool_device_list *list) {
	struct usb_bus *bus;
	struct usb_device *dev;
	int num_found = 0;
	wispydbx_usb_pair *auxpair;
	char combopath[128];
	int model = 0;

	/* Libusb init */
	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (((dev->descriptor.idVendor == METAGEEK_WISPYDBx_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPYDBx_V2_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_V2_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPYDBx_V3_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_V3_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY24I_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY24I_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY24x_V2_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY24x_V2_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY900x_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY900x_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY950x_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY950x_PID))) {

				/* If we're full up, break */
				if (list->num_devs == list->max_devs - 1)
					break;

				auxpair = (wispydbx_usb_pair *) malloc(sizeof(wispydbx_usb_pair));

				snprintf(auxpair->bus, 64, "%s", bus->dirname);
				snprintf(auxpair->dev, 64, "%s", dev->filename);

				snprintf(combopath, 128, "%s%s", auxpair->bus, auxpair->dev);

				if (dev->descriptor.idProduct == METAGEEK_WISPY24I_PID) {
					snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
							 "Wi-Spy %s USB %u", "24i", list->list[list->num_devs].device_id);
					model = WISPYDBx_MODEL_DBxV1;
				} else if (dev->descriptor.idProduct == METAGEEK_WISPY900x_PID) {
#ifdef _DEBUG
					fprintf(stderr, "debug - bcd %x\n", dev->descriptor.bcdDevice);
#endif
					if (dev->descriptor.bcdDevice == 0x100) {
						snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
								 "Wi-Spy %s USB %u", "900x", list->list[list->num_devs].device_id);
						model = WISPYDBx_MODEL_900x;
					} else {
						snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
								 "Wi-Spy %s USB %u", "900x2", list->list[list->num_devs].device_id);
						model = WISPYDBx_MODEL_900xV2;
					}
				} else if (dev->descriptor.idProduct == METAGEEK_WISPY950x_PID) {
					snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
							 "Wi-Spy %s USB %u", "950x", list->list[list->num_devs].device_id);
					model = WISPYDBx_MODEL_950x;
				} else if (dev->descriptor.idProduct == METAGEEK_WISPYDBx_V2_PID) {
					snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
							 "Wi-Spy %s USB %u", "DBx2", list->list[list->num_devs].device_id);
					model = WISPYDBx_MODEL_DBxV2;
				} else if (dev->descriptor.idProduct == METAGEEK_WISPYDBx_V3_PID) {
					snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
							 "Wi-Spy %s USB %u", "DBx3", list->list[list->num_devs].device_id);
					model = WISPYDBx_MODEL_DBxV3;
				} else if (dev->descriptor.idProduct == METAGEEK_WISPY24x_V2_PID) {
					snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
							 "Wi-Spy %s USB %u", "24x2", list->list[list->num_devs].device_id);
					model = WISPYDBx_MODEL_24xV2;
				} else {
					snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
							 "Wi-Spy %s USB %u", "DBx", list->list[list->num_devs].device_id);
					model = WISPYDBx_MODEL_DBxV1;
				}

				/* Fill in the list elements */
				list->list[list->num_devs].device_id = 5;
					// wispydbx_adler_checksum(combopath, 128);
				list->list[list->num_devs].init_func = wispydbx_usb_init;
				list->list[list->num_devs].hw_rec = auxpair;

				wispydbx_add_supportedranges(&(list->list[list->num_devs].num_sweep_ranges),
											 &(list->list[list->num_devs].supported_ranges),
											 model);

#if 0
				if (model == 0 || model == 3)
					wispydbx_add_supportedranges(
							 &(list->list[list->num_devs].num_sweep_ranges),
							 &(list->list[list->num_devs].supported_ranges));
				else if (model == 1 || model == 4)
					wispy24i_add_supportedranges(
							 &(list->list[list->num_devs].num_sweep_ranges),
							 &(list->list[list->num_devs].supported_ranges));
				else if (model == 2 || model == 5 || model == 6)
					wispy900x_add_supportedranges(
							 &(list->list[list->num_devs].num_sweep_ranges),
							 &(list->list[list->num_devs].supported_ranges));
#endif

				list->num_devs++;

				num_found++;
			}
		}
	}

	return num_found;
}

int wispydbx_usb_init(spectool_phy *phydev, spectool_device_rec *rec) {
	wispydbx_usb_pair *auxpair = (wispydbx_usb_pair *) rec->hw_rec;

	if (auxpair == NULL)
		return -1;

	return wispydbx_usb_init_path(phydev, auxpair->bus, auxpair->dev);
}

/* Initialize a specific USB device based on bus and device IDs passed by the UI */
int wispydbx_usb_init_path(spectool_phy *phydev, char *buspath, char *devpath) {
	struct usb_bus *bus = NULL;
	struct usb_device *dev = NULL;

	struct usb_device *usb_dev_chosen = NULL;

	char combopath[128];
	uint32_t cid = 0;

	int model = 0;

	wispydbx_usb_aux *auxptr = NULL;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	memset(combopath, 0, 128);
	snprintf(combopath, 128, "%s%s", buspath, devpath);
	cid = wispydbx_adler_checksum(combopath, 128);

	/* Don't know if a smarter way offhand, and we don't do this often, so just
	 * crawl and compare */
	for (bus = usb_busses; bus; bus = bus->next) {
		if (strcmp(bus->dirname, buspath))
			continue;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (strcmp(dev->filename, devpath))
				continue;

			if (((dev->descriptor.idVendor == METAGEEK_WISPYDBx_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPYDBx_V2_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_V2_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPYDBx_V3_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_V3_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY24I_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY24I_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY24x_V2_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY24x_V2_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY900x_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY900x_PID)) ||
				((dev->descriptor.idVendor == METAGEEK_WISPY950x_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY950x_PID))) {
				usb_dev_chosen = dev;
				break;
			} else {
				snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
						 "WISPYDBx_INIT failed, specified device %u does not "
						 "appear to be a Wi-Spy device", cid);
				return -1;
			}
		}
	}

	if (usb_dev_chosen == NULL) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "WISPYDBx_INIT failed, specified device %u does not appear "
				 "to exist.", cid);
		return -1;
	}

	if (usb_dev_chosen->descriptor.idProduct == METAGEEK_WISPY24I_PID)
		model = WISPYDBx_MODEL_24i;
	else if (usb_dev_chosen->descriptor.idProduct == METAGEEK_WISPY900x_PID) {
		if (dev->descriptor.bcdDevice == 0x100) {
			model = WISPYDBx_MODEL_900x;
		} else {
			model = WISPYDBx_MODEL_900xV2;
		}
	} else if (usb_dev_chosen->descriptor.idProduct == METAGEEK_WISPYDBx_V2_PID)
		model = WISPYDBx_MODEL_DBxV2;
	else if (usb_dev_chosen->descriptor.idProduct == METAGEEK_WISPYDBx_V3_PID)
		model = WISPYDBx_MODEL_DBxV3;
	else if (usb_dev_chosen->descriptor.idProduct == METAGEEK_WISPY24x_V2_PID)
		model = WISPYDBx_MODEL_24xV2;
	else if (usb_dev_chosen->descriptor.idProduct == METAGEEK_WISPY950x_PID)
		model = WISPYDBx_MODEL_900xV2;
	else
		model = WISPYDBx_MODEL_DBxV1;

	/* Build the device record with appropriate sweep capabilities */
	phydev->device_spec = (spectool_dev_spec *) malloc(sizeof(spectool_dev_spec));

	phydev->device_spec->device_id = cid;

	/* Default the name to the buspath */

	switch (model) {
		case WISPYDBx_MODEL_24i:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "24i", cid);
			break;
		case WISPYDBx_MODEL_900x:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "900x", cid);
			break;
		case WISPYDBx_MODEL_DBxV2:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "DBx2", cid);
			break;
		case WISPYDBx_MODEL_24xV2:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "24x2", cid);
			break;
		case WISPYDBx_MODEL_950x:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "950x", cid);
			break;
		case WISPYDBx_MODEL_900xV2:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "900x2", cid);
			break;
		default:
			snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
					 "Wi-Spy %s USB %u", "DBx", cid);
			break;
	}

	/* State */
	phydev->state = SPECTOOL_STATE_CLOSED;

	phydev->min_rssi_seen = -1;

	phydev->device_spec->device_version = 0x03;
	phydev->device_spec->device_flags = SPECTOOL_DEV_FL_VAR_SWEEP;


	wispydbx_add_supportedranges(&phydev->device_spec->num_sweep_ranges,
								 &phydev->device_spec->supported_ranges, model);
#if 0
	if (model == 0 || model == 3) {
		// DBX v1 and V2
		wispydbx_add_supportedranges(&phydev->device_spec->num_sweep_ranges,
									 &phydev->device_spec->supported_ranges);
	} else if (model == 1 || model == 4) {
		// 24i and 24xv2
		wispy24i_add_supportedranges(&phydev->device_spec->num_sweep_ranges,
									 &phydev->device_spec->supported_ranges);
	} else if (model == 2 || model == 5 || model == 6) {
		// 900 and 950x
		wispy900x_add_supportedranges(&phydev->device_spec->num_sweep_ranges,
									  &phydev->device_spec->supported_ranges);
	}
#endif

	phydev->device_spec->cur_profile = 0;

	phydev->device_spec->default_range = phydev->device_spec->supported_ranges;


	/* Set up the aux state */
	auxptr = malloc(sizeof(wispydbx_usb_aux));
	phydev->auxptr = auxptr;

	auxptr->model = model;

	// Set the protocol version; only a few old devices are v1 now so default to v2
	if (model == WISPYDBx_MODEL_DBxV1 ||
		model == WISPYDBx_MODEL_900x ||
		model == WISPYDBx_MODEL_950x) {
		auxptr->protocol = 1;
	} else {
		auxptr->protocol = 2;
	}

#if 0
	if (model == 1 || model == 2 || model == 5)
		auxptr->protocol = 1;
	else
		auxptr->protocol = 2;
#endif

	auxptr->configured = 0;

	auxptr->sweepbase = 0;

	auxptr->dev = dev;
	auxptr->devhdl = NULL;
	auxptr->phydev = phydev;
	auxptr->sockpair[0] = -1;
	auxptr->sockpair[1] = -1;

	/* Will be filled in by setposition later */
	auxptr->sweepbuf_initialized = 0;
	auxptr->sweepbuf = NULL;

	phydev->open_func = &wispydbx_usb_open;
	phydev->close_func = &wispydbx_usb_close;
	phydev->poll_func = &wispydbx_usb_poll;
	phydev->pollfd_func = &wispydbx_usb_getpollfd;
	phydev->setcalib_func = &wispydbx_usb_setcalibration;
	phydev->getsweep_func = &wispydbx_usb_getsweep;
	phydev->setposition_func = &wispydbx_usb_setposition;

	phydev->draw_agg_suggestion = 1;

	return 0;
}

void *wispydbx_usb_servicethread(void *aux) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) aux;

	int sock;
	struct usb_device *dev;
	struct usb_dev_handle *wispy;

	// Size by v2 report, which is bigger
	char buf[sizeof(wispydbx_report_v2)];
	int bufsz;

	int x = 0, error = 0;
	fd_set wset;

	struct timeval tm;

	sigset_t signal_set;

	error = 0;

	sock = auxptr->sockpair[1];

	// Size report based on v1 or v2 protocol
	if (auxptr->protocol == 2)
		bufsz = sizeof(wispydbx_report_v2);
	else
		bufsz = sizeof(wispydbx_report);

	dev = auxptr->dev;
	wispy = auxptr->devhdl;

	/* We don't want to see any signals in the child thread */
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

#ifdef _DEBUG
	fprintf(stderr, "debug - servicethread started\n");
#endif

	while (1) {
		/* wait until we're able to write out to the IPC socket, go into a blocking
		 * select */
		FD_ZERO(&wset);
		FD_SET(sock, &wset);

#ifdef _DEBUG
		fprintf(stderr, "debug - about to enter blocking select\n");
#endif
		if (select(sock + 1, NULL, &wset, NULL, NULL) < 0) {
			snprintf(auxptr->phydev->errstr, SPECTOOL_ERROR_MAX,
					 "wispydbx_usb poller failed on IPC write select(): %s",
					 strerror(errno));
			auxptr->usb_thread_alive = 0;
			auxptr->phydev->state = SPECTOOL_STATE_ERROR;
			pthread_exit(NULL);
		}

#ifdef _DEBUG
		fprintf(stderr, "debug - blocking select completed\n");
#endif

		if (auxptr->usb_thread_alive == 0) {
#ifdef _DEBUG
			fprintf(stderr, "debug - usb thread no longer alive?\n");
#endif
			auxptr->phydev->state = SPECTOOL_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (FD_ISSET(sock, &wset) == 0) {
			continue;
		}

		/* Get new data only if we haven't requeued */
		if (error == 0 && auxptr->phydev->state == SPECTOOL_STATE_RUNNING) {
			int len = 0;
			memset(buf, 0, bufsz);

#ifdef _DEBUG
			fprintf(stderr, "debug - usb_interrupt_read\n");
#endif
			if ((len = usb_interrupt_read(wispy, 0x82, buf, 
								   bufsz, TIMEOUT)) <= 0) {
				if (errno == EAGAIN) {
#ifdef _DEBUG
					fprintf(stderr, "debug - eagain on usb_interrupt_read\n");
#endif
					continue;
				}

#ifdef _DEBUG
				fprintf(stderr, "debug - failed - %s\n", strerror(errno));
				fprintf(stderr, "debug - %s\n", usb_strerror());
#endif

				snprintf(auxptr->phydev->errstr, SPECTOOL_ERROR_MAX,
						 "wispydbx_usb poller failed to read USB data: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = SPECTOOL_STATE_ERROR;
				pthread_exit(NULL);
			}

#ifdef _DEBUG
			fprintf(stderr, "debug - usb read return %d\n", len);
#endif

			/* Send it to the IPC remote, re-queue on enobufs */
			if (send(sock, buf, bufsz, 0) < 0) {
				if (errno == ENOBUFS) {
					error = 1;
					continue;
				}

				snprintf(auxptr->phydev->errstr, SPECTOOL_ERROR_MAX,
						 "wispydbx_usb poller failed on IPC send: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = SPECTOOL_STATE_ERROR;
				pthread_exit(NULL);
			}

		}

		error = 0;
	}

	auxptr->usb_thread_alive = 0;
	send(sock, buf, bufsz, 0);
	auxptr->phydev->state = SPECTOOL_STATE_ERROR;
	pthread_exit(NULL);
}

int wispydbx_usb_getpollfd(spectool_phy *phydev) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;

	if (auxptr->usb_thread_alive == 0) {
		// fprintf(stderr, "debug - thread alive = 0\n");
		wispydbx_usb_close(phydev);
		return -1;
	}

	// fprintf(stderr, "debug - auxptr sockpair 0 %d\n", auxptr->sockpair[0]);
	return auxptr->sockpair[0];
}

int wispydbx_usb_open(spectool_phy *phydev) {
	int pid_status;
	struct usb_dev_handle *wispy;
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;
	wispydbx_startsweep startcmd;

	/* Make the client/server socketpair */
	if (socketpair(PF_UNIX, SOCK_DGRAM, 0, auxptr->sockpair) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb open failed to create socket pair for capture "
				 "process: %s", strerror(errno));
		return -1;
	}

	if ((auxptr->devhdl = usb_open(auxptr->dev)) == NULL) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb capture process failed to open USB device: %s",
				 strerror(errno));
		return -1;
	}

#if defined(LIBUSB_HAS_GET_DRIVER_NP) && defined(LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP)
	if (usb_detach_kernel_driver_np(auxptr->devhdl, 0) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "Could not detach device from kernel driver: %s",
				 usb_strerror()); 
	}
#endif

	// fprintf(stderr, "debug - set_configuration\n");
	if (usb_set_configuration(auxptr->devhdl, 1) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "could not configure interface: %s", usb_strerror());
		// fprintf(stderr, "debug - failed to set config: %s\n", usb_strerror());
	}

	// fprintf(stderr, "debug - claiming interface\n");
	if (usb_claim_interface(auxptr->devhdl, 0) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "could not claim interface: %s", usb_strerror());
	}
	// fprintf(stderr, "debug - done claiming\n");

	auxptr->usb_thread_alive = 1;
	auxptr->last_read = time(0);

	// printf("debug - creating thread\n");

	if (pthread_create(&(auxptr->usb_thread), NULL, 
					   wispydbx_usb_servicethread, auxptr) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb capture failed to create thread: %s",
				 strerror(errno));
		auxptr->usb_thread_alive = 0;
		return -1;
	}

	// printf("debug - done creating thread\n");

	/* Update the state */
	phydev->state = SPECTOOL_STATE_CONFIGURING;

	/*
	if (wispydbx_usb_setposition(phydev, 0, 0, 0) < 0) {
		// fprintf(stderr, "debug - setposition failed\n");
		return -1;
	}
	*/

	/* Initialize the hw sweep features */
	startcmd.report_id = 0x53;
	startcmd.command_id = 0x89;
	startcmd.command_flags =
		WISPYDBx_USB_ASSEMBLE_CMDFLAGS(auxptr->cmd_seq++, 
									   WISPYDBx_USB_STARTSWEEP_LEN);

	wispy = auxptr->devhdl;

#ifdef _DEBUG
	fprintf(stderr, "debug - writing usb start control msg\n");
#endif

	if (usb_control_msg(wispy, 
						USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
						HID_SET_REPORT, 
						0x02 + (0x03 << 8),
						0, 
						(uint8_t *) &startcmd, (int) sizeof(wispydbx_startsweep), 
						0) <= 0) {
		// fprintf(stderr, "debug - controlmsg start failed %s\n", strerror(errno));
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb open failed to send start command: %s",
				 strerror(errno));
		phydev->state = SPECTOOL_STATE_ERROR;
		return -1;
	}

#ifdef _DEBUG
	fprintf(stderr, "debug - finished writing usb control msg\n");
#endif

	return 1;
}

int wispydbx_usb_close(spectool_phy *phydev) {
	wispydbx_usb_aux *aux;
	
	if (phydev == NULL)
		return 0;

	aux = (wispydbx_usb_aux *) phydev->auxptr;

	if (aux == NULL)
		return 0;

	/* If the thread is still alive, don't take away the devices it might
	 * still be reading, wait for it to error down */
	if (aux->usb_thread_alive) {
		aux->usb_thread_alive = 0;
		pthread_join(aux->usb_thread, NULL);
	}

	if (aux->devhdl) {
		usb_close(aux->devhdl);
		aux->devhdl = NULL;
	}

	if (aux->sockpair[0] >= 0) {
		close(aux->sockpair[0]);
		aux->sockpair[0] = -1;
	}

	if (aux->sockpair[1] >= 0) {
		close(aux->sockpair[1]);
		aux->sockpair[1] = -1;
	}

	return 1;
}

spectool_sample_sweep *wispydbx_usb_getsweep(spectool_phy *phydev) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;

	return auxptr->sweepbuf;
}

void wispydbx_usb_setcalibration(spectool_phy *phydev, int in_calib) {
	phydev->state = SPECTOOL_STATE_RUNNING;
}

int wispydbx_usb_poll(spectool_phy *phydev) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;

	// Use v2 report size as it is larger
	char lbuf[sizeof(wispydbx_report_v2)];
	int bufsz;

	int x;
	int base = 0;
	int ret = 0;
	int sweep_full = 0;

	wispydbx_report *report;
	wispydbx_report_v2 *report2;

	uint16_t packet_index;
	uint8_t *data;
	unsigned int nsamples;

#ifdef _DEBUG
	fprintf(stderr, "debug - dbx_usb_poll\n");
#endif

	if (auxptr->protocol == 2) {
		bufsz = sizeof(wispydbx_report_v2);
		nsamples = 59;
	} else {
		bufsz = sizeof(wispydbx_report);
		nsamples = 61;
	}

	// printf("debug - usb poll\n");

	/* Push a configure event before anything else */
	if (auxptr->configured == 0) {
		auxptr->configured = 1;
		// printf("debug - usb poll return configured\n");
		return SPECTOOL_POLL_CONFIGURED;
	}

	/* Use the error set by the polling thread */
	if (auxptr->usb_thread_alive == 0) {
		phydev->state = SPECTOOL_STATE_ERROR;
		wispydbx_usb_close(phydev);
		// printf("debug - usb poll return error\n");
		return SPECTOOL_POLL_ERROR;
	}

	if ((ret = recv(auxptr->sockpair[0], lbuf, bufsz, 0)) < 0) {
		// printf("debug - usb poll return recv error\n");
		if (auxptr->usb_thread_alive != 0)
			snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
					 "wispydbx_usb IPC receiver failed to read signal data: %s",
					 strerror(errno));
		phydev->state = SPECTOOL_STATE_ERROR;
		return SPECTOOL_POLL_ERROR;
	}

	if (time(0) - auxptr->last_read > 3) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb didn't see any data for more than 3 seconds, "
				 "something has gone wrong (was the device removed?)");
		phydev->state = SPECTOOL_STATE_ERROR;
		return SPECTOOL_POLL_ERROR;
	}

	if (ret > 0)
		auxptr->last_read = time(0);

	// printf("debug usb poll recv len %d\n", ret);
	//
	if (ret < bufsz) {
		printf("Short report\n");
		return SPECTOOL_POLL_NONE;
	}

	// If we don't have a sweepbuf we're not configured, barf
	if (auxptr->sweepbuf == NULL)
		return SPECTOOL_POLL_NONE;

	if (auxptr->protocol == 2) {
		report2 = (wispydbx_report_v2 *) lbuf;

		packet_index = report2->packet_index;
		data = report2->data;

	} else {
		report = (wispydbx_report *) lbuf;

		packet_index = report->packet_index;
		data = report->data;
	}

	/* Extract the slot index */
#ifdef WORDS_BIGENDIAN
	base = endian_swap16(packet_index);
#else
	base = packet_index;
#endif

	if (base == 0)
		auxptr->sweepbase = 0;
	else
		base = auxptr->sweepbase;

	if (base < 0 || base > auxptr->sweepbuf->num_samples) {
#ifdef _DEBUG
		fprintf(stderr, "debug - bunk data, base %d\n", base);
#endif
		/* Bunk data, throw it out */
		return SPECTOOL_POLL_NONE;
	}

	/* Initialize the sweep buffer when we get to it 
	 * If we haven't gotten around to a 0 state to initialize the buffer, we throw
	 * out the sample data until we do. */
	if (base == 0) {
		auxptr->sweepbuf_initialized = 1;
		auxptr->num_sweeps++;

		/* Init the timestamp for sweep begin */
		gettimeofday(&(auxptr->sweepbuf->tm_start), NULL);
	} else if (auxptr->sweepbuf_initialized == 0) {
#ifdef _DEBUG
		fprintf(stderr, "debug - sweepbuf unitialized\n");
#endif
		return SPECTOOL_POLL_NONE;
	}

#ifdef _DEBUG
	fprintf(stderr, "debug - got %d samples\n", nsamples);
#endif

	for (x = 0; x < nsamples; x++) {
		if (base + x >= auxptr->sweepbuf->num_samples) {
			sweep_full = 1;
			break;
		}

		auxptr->sweepbuf->sample_data[base + x] = data[x];

		if (data[x] < phydev->min_rssi_seen)
			phydev->min_rssi_seen = data[x];
	}

	auxptr->sweepbase += nsamples;

	/* Flag that a sweep is complete */
	if (base + nsamples == auxptr->sweepbuf->num_samples || sweep_full) {
		gettimeofday(&(auxptr->sweepbuf->tm_end), NULL);
		auxptr->sweepbuf->min_rssi_seen = phydev->min_rssi_seen;

		return SPECTOOL_POLL_SWEEPCOMPLETE;
	}

	return SPECTOOL_POLL_NONE;
}

int wispydbx_usb_setposition(spectool_phy *phydev, int in_profile, 
							 int start_khz, int res_hz) {
	struct usb_dev_handle *wispy;

	wispydbx_rfsettings rfset;
	wispydbx_rfsettings_v2 rfset2;
	uint8_t *use_rfset = NULL;
	int rfset_len = 0;

	int x;

	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;
	int use_default = 0;

	uint32_t filter_bw_hz;
	uint16_t points_per_sweep;
	uint8_t samples_per_point;

#ifdef _DEBUG
	fprintf(stderr, "debug - setposition profile %d %d %d\n", in_profile, start_khz, res_hz);
#endif

	// Todo - add support for setting arbitrary ranges
	if (in_profile < 0 || in_profile > (int) phydev->device_spec->num_sweep_ranges) {
		fprintf(stderr, "profile out of range\n");
		return -1;
	}

	phydev->device_spec->cur_profile = in_profile;

	start_khz = phydev->device_spec->supported_ranges[in_profile].start_khz;
	res_hz = phydev->device_spec->supported_ranges[in_profile].res_hz;
	filter_bw_hz = phydev->device_spec->supported_ranges[in_profile].filter_bw_hz;
	points_per_sweep = phydev->device_spec->supported_ranges[in_profile].num_samples;
	samples_per_point = 
		phydev->device_spec->supported_ranges[in_profile].samples_per_point;

	/* Initialize the hw sweep features */
	if (auxptr->protocol == 2) {
		// model 3 (dbx v2) gets the v2 settings block
		rfset2.report_id = 0x53;
		rfset2.command_id = 0x10;

		rfset2.command_flags = 
			WISPYDBx_USB_ASSEMBLE_CMDFLAGS(auxptr->cmd_seq++, 
										   WISPYDBx_USB_RFSETTINGS_V2_LEN);

		/* Multibytes have to be handled in USB-endian (little) */
#ifdef WORDS_BIGENDIAN
		rfset2.start_khz = endian_swap32(start_khz);
		rfset2.freq_res_hz = endian_swap32(res_hz);
		rfset2.filter_bw_hz = endian_swap32(filter_bw_hz);
		rfset2.points_per_sweep = endian_swap16(points_per_sweep);
#else
		rfset2.start_khz = start_khz;
		rfset2.freq_res_hz = res_hz;
		rfset2.filter_bw_hz = filter_bw_hz;
		rfset2.points_per_sweep = points_per_sweep;
#endif
		// Re-use samples per point as dwelltime in v2+
		rfset2.dwell_time = samples_per_point;
		rfset2.dither_steps = 1;
		rfset2.reserved = 0;

		use_rfset = (unsigned char *)&rfset2;
		rfset_len = (int) sizeof(wispydbx_rfsettings_v2);
	} else {
		rfset.report_id = 0x53;
		rfset.command_id = 0x10;
		rfset.command_flags =
			WISPYDBx_USB_ASSEMBLE_CMDFLAGS(auxptr->cmd_seq++, 
										   WISPYDBx_USB_RFSETTINGS_LEN);

		/* Multibytes have to be handled in USB-endian (little) */
#ifdef WORDS_BIGENDIAN
		rfset.start_khz = endian_swap32(start_khz);
		rfset.freq_res_hz = endian_swap32(res_hz);
		rfset.filter_bw_hz = endian_swap32(filter_bw_hz);
		rfset.points_per_sweep = endian_swap16(points_per_sweep);
#else
		rfset.start_khz = start_khz;
		rfset.freq_res_hz = res_hz;
		rfset.filter_bw_hz = filter_bw_hz;
		rfset.points_per_sweep = points_per_sweep;
#endif
		rfset.samples_per_point = samples_per_point;

		use_rfset = (unsigned char *)&rfset;
		rfset_len = (int) sizeof(wispydbx_rfsettings);

	}

	wispy = auxptr->devhdl;

#ifdef _DEBUG
	fprintf(stderr, "debug - writing usb control msg %d\n", 0x02 + (0x03 << 8));
#endif

	if (usb_control_msg(wispy, 
						USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
						HID_SET_REPORT, 
						0x02 + (0x03 << 8),
						0, 
						(uint8_t *) use_rfset, rfset_len, 
						0) == 0) {
		fprintf(stderr, "debug - control_msg_fail: %s\n", usb_strerror());
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb setposition failed to set sweep feature set: %s",
				 strerror(errno));
		phydev->state = SPECTOOL_STATE_ERROR;
		return -1;
	}
#ifdef _DEBUG
	fprintf(stderr, "debug - finished wrting usb control\n");
#endif

#if 0
	memset(use_rfset, 0, rfset_len);

	if (auxptr->protocol == 2) {
		rfset2.report_id = 0x53;
		rfset2.command_id = 0x11;
		rfset2.command_flags =
			WISPYDBx_USB_ASSEMBLE_CMDFLAGS(auxptr->cmd_seq++, 
										   WISPYDBx_USB_RFSETTINGS_V2_LEN);
	} else {
		rfset.report_id = 0x53;
		rfset.command_id = 0x11;
		rfset.command_flags =
			WISPYDBx_USB_ASSEMBLE_CMDFLAGS(auxptr->cmd_seq++, 
										   WISPYDBx_USB_RFSETTINGS_LEN);
	}

	if (usb_control_msg(wispy, 
						USB_ENDPOINT_IN + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
						HID_GET_REPORT, 
						0x02 + (0x03 << 8),
						0, 
						(uint8_t *) use_rfset, rfset_len, 
						0) == 0) {
		fprintf(stderr, "debug - control_msg_fail: %s\n", usb_strerror());
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "wispydbx_usb setposition failed to get sweep feature set: %s",
				 strerror(errno));
		phydev->state = SPECTOOL_STATE_ERROR;
		return -1;
	}

	for (x = 0; x < rfset_len; x++) {
		printf("%02x ", use_rfset[x]);
	}
	printf("\n");
#endif

	/* We're not configured, so we need to push a new configure block out next time
	 * we sweep */
	auxptr->configured = 0;

	/* Rebuild the sweep buffer */
	if (auxptr->sweepbuf)
		free(auxptr->sweepbuf);

	auxptr->sweepbuf =
		(spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(phydev->device_spec->supported_ranges[in_profile].num_samples));
	auxptr->sweepbuf->phydev = phydev;
	auxptr->sweepbuf->start_khz = 
		phydev->device_spec->supported_ranges[in_profile].start_khz;
	auxptr->sweepbuf->end_khz = 
		phydev->device_spec->supported_ranges[in_profile].end_khz;
	auxptr->sweepbuf->res_hz = 
		phydev->device_spec->supported_ranges[in_profile].res_hz;
	auxptr->sweepbuf->num_samples = 
		phydev->device_spec->supported_ranges[in_profile].num_samples;

	auxptr->sweepbuf->amp_offset_mdbm =
		phydev->device_spec->supported_ranges[in_profile].amp_offset_mdbm;
	auxptr->sweepbuf->amp_res_mdbm =
		phydev->device_spec->supported_ranges[in_profile].amp_res_mdbm;
	auxptr->sweepbuf->rssi_max =
		phydev->device_spec->supported_ranges[in_profile].rssi_max;

	auxptr->sweepbuf_initialized = 0;
	auxptr->num_sweeps = -1;

	return 1;
}

