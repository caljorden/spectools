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

/* Default config */
#define WISPYDBx_USB_STARTKHZ_58		5160000
#define WISPYDBx_USB_RESHZ_58			1497070
#define WISPYDBx_USB_FILTERHZ_58		600000
#define WISPYDBx_USB_NUM_SAMPLES_58		451
#define WISPYDBx_USB_SAMPLESPOINT_58	8

#define WISPYDBx_USB_DEF_STARTKHZ_24		2400000
#define WISPYDBx_USB_DEF_RESHZ_24			239924
#define WISPYDBx_USB_DEF_FILTERHZ_24		239924
#define WISPYDBx_USB_DEF_SAMPLESPOINT_24	8
#define WISPYDBx_USB_NUM_SAMPLES_24			350

#define WISPYDBx_USB_STARTKHZ_24_SLOW		2400000
#define WISPYDBx_USB_RESHZ_24_SLOW			168000
#define WISPYDBx_USB_FILTERHZ_24_SLOW		168000
#define WISPYDBx_USB_SAMPLESPOINT_24_SLOW	8
#define WISPYDBx_USB_NUM_SAMPLES_24_SLOW	500


#define WISPYDBx_USB_OFFSET_MDBM		-134000
#define WISPYDBx_USB_RES_MDBM			500
#define WISPYDBx_USB_RSSI_MAX			222

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
        (uint16_t)(((uint16_t)(__x) & (uint16_t)0x000000ff) << 24) | \
        (uint16_t)(((uint16_t)(__x) & (uint16_t)0x0000ff00) << 8) | \
})

/* Aux tracking struct for wispy1 characteristics */
typedef struct _wispydbx_usb_aux {
	struct usb_device *dev;
	struct usb_dev_handle *devhdl;

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
	wispy_sample_sweep *sweepbuf;

	int sockpair[2];

	int sweepbase;

	wispy_phy *phydev;

	uint8_t cmd_seq : 2;
} wispydbx_usb_aux;

#define WISPYDBx_USB_ASSEMBLE_CMDFLAGS(seq, len) \
	({ \
	 ((uint8_t)( \
		 (uint8_t)(((uint8_t)(seq) & (uint8_t) 0x3) << 5) | \
		 (uint8_t)(((uint8_t)(len) & (uint8_t) 0xF)) )); \
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
			snprintf(errstr, WISPY_ERROR_MAX, "Your kernel doesn't appear to accept "
					 "the USB disconnect command.  Either your kernel is too old and "
					 "does not support device removal, or support for removal has "
					 "been changed by your distribution kernel maintainers.");
		} 

		snprintf(errstr, WISPY_ERROR_MAX, "Could not detatch kernel driver from "
				 "interface %d: %s", interface, strerror(errno));
		return -1;
	}

	return 0;
}
#endif /* sys_linux */

/* Prototypes */
int wispydbx_usb_open(wispy_phy *);
int wispydbx_usb_close(wispy_phy *);
int wispydbx_usb_thread_close(wispy_phy *);
int wispydbx_usb_poll(wispy_phy *);
int wispydbx_usb_getpollfd(wispy_phy *);
void wispydbx_usb_setcalibration(wispy_phy *, int);
int wispydbx_usb_setposition(wispy_phy *, int, int, int);
wispy_sample_sweep *wispydbx_usb_getsweep(wispy_phy *);

void wispydbx_add_supportedranges(int *num_ranges, wispy_sample_sweep **ranges) {
	*ranges = (wispy_sample_sweep *) malloc(sizeof(wispy_sample_sweep) * 3);

	*num_ranges = 3;

	(*ranges)[0].name = strdup("2.4GHz ISM");
	(*ranges)[0].num_samples = WISPYDBx_USB_NUM_SAMPLES_24;

	(*ranges)[0].amp_offset_mdbm = WISPYDBx_USB_OFFSET_MDBM;
	(*ranges)[0].amp_res_mdbm = WISPYDBx_USB_RES_MDBM;
	(*ranges)[0].rssi_max = WISPYDBx_USB_RSSI_MAX;

	(*ranges)[0].start_khz = WISPYDBx_USB_DEF_STARTKHZ_24;
	(*ranges)[0].end_khz = 
		WISPYDBx_USB_DEF_STARTKHZ_24 + ((WISPYDBx_USB_NUM_SAMPLES_24 *
										 WISPYDBx_USB_DEF_RESHZ_24) / 1000);
	(*ranges)[0].res_hz = WISPYDBx_USB_DEF_RESHZ_24;
	(*ranges)[0].samples_per_point = WISPYDBx_USB_DEF_SAMPLESPOINT_24;
	(*ranges)[0].filter_bw_hz = WISPYDBx_USB_DEF_FILTERHZ_24;

	(*ranges)[1].name = strdup("2.4GHz ISM High-Detail");
	(*ranges)[1].num_samples = WISPYDBx_USB_NUM_SAMPLES_24_SLOW;

	(*ranges)[1].amp_offset_mdbm = WISPYDBx_USB_OFFSET_MDBM;
	(*ranges)[1].amp_res_mdbm = WISPYDBx_USB_RES_MDBM;
	(*ranges)[1].rssi_max = WISPYDBx_USB_RSSI_MAX;

	(*ranges)[1].start_khz = WISPYDBx_USB_DEF_STARTKHZ_24;
	(*ranges)[1].end_khz = 
		WISPYDBx_USB_DEF_STARTKHZ_24 + ((WISPYDBx_USB_NUM_SAMPLES_24_SLOW *
										 WISPYDBx_USB_RESHZ_24_SLOW) / 1000);
	(*ranges)[1].res_hz = WISPYDBx_USB_RESHZ_24_SLOW;
	(*ranges)[1].samples_per_point = WISPYDBx_USB_SAMPLESPOINT_24_SLOW;
	(*ranges)[1].filter_bw_hz = WISPYDBx_USB_FILTERHZ_24_SLOW;

	(*ranges)[2].name = strdup("5.8GHz");
	(*ranges)[2].num_samples = WISPYDBx_USB_NUM_SAMPLES_58;

	(*ranges)[2].amp_offset_mdbm = WISPYDBx_USB_OFFSET_MDBM;
	(*ranges)[2].amp_res_mdbm = WISPYDBx_USB_RES_MDBM;
	(*ranges)[2].rssi_max = WISPYDBx_USB_RSSI_MAX;

	(*ranges)[2].start_khz = WISPYDBx_USB_STARTKHZ_58;
	(*ranges)[2].end_khz = 
		WISPYDBx_USB_STARTKHZ_58 + ((WISPYDBx_USB_NUM_SAMPLES_58 *
									 WISPYDBx_USB_RESHZ_58) / 1000);
	(*ranges)[2].res_hz = WISPYDBx_USB_RESHZ_58;
	(*ranges)[2].samples_per_point = WISPYDBx_USB_SAMPLESPOINT_58;
	(*ranges)[2].filter_bw_hz = WISPYDBx_USB_FILTERHZ_58;
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
int wispydbx_usb_device_scan(wispy_device_list *list) {
	struct usb_bus *bus;
	struct usb_device *dev;
	int num_found = 0;
	wispydbx_usb_pair *auxpair;
	char combopath[128];

	/* Libusb init */
	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (((dev->descriptor.idVendor == METAGEEK_WISPYDBx_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_PID))) {

				/* If we're full up, break */
				if (list->num_devs == list->max_devs - 1)
					break;

				auxpair = (wispydbx_usb_pair *) malloc(sizeof(wispydbx_usb_pair));

				snprintf(auxpair->bus, 64, "%s", bus->dirname);
				snprintf(auxpair->dev, 64, "%s", dev->filename);

				snprintf(combopath, 128, "%s%s", auxpair->bus, auxpair->dev);

				/* Fill in the list elements */
				list->list[list->num_devs].device_id = 5;
					// wispydbx_adler_checksum(combopath, 128);
				snprintf(list->list[list->num_devs].name, WISPY_PHY_NAME_MAX,
						 "Wi-Spy DBx USB %u", bus->dirname, dev->filename,
						 list->list[list->num_devs].device_id);
				list->list[list->num_devs].init_func = wispydbx_usb_init;
				list->list[list->num_devs].hw_rec = auxpair;

				wispydbx_add_supportedranges(
							&(list->list[list->num_devs].num_sweep_ranges),
	   						&(list->list[list->num_devs].supported_ranges));

				list->num_devs++;

				num_found++;
			}
		}
	}

	return num_found;
}

int wispydbx_usb_init(wispy_phy *phydev, wispy_device_rec *rec) {
	wispydbx_usb_pair *auxpair = (wispydbx_usb_pair *) rec->hw_rec;

	if (auxpair == NULL)
		return -1;

	return wispydbx_usb_init_path(phydev, auxpair->bus, auxpair->dev);
}

/* Initialize a specific USB device based on bus and device IDs passed by the UI */
int wispydbx_usb_init_path(wispy_phy *phydev, char *buspath, char *devpath) {
	struct usb_bus *bus = NULL;
	struct usb_device *dev = NULL;

	struct usb_device *usb_dev_chosen = NULL;

	char combopath[128];
	uint32_t cid = 0;

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
				 (dev->descriptor.idProduct == METAGEEK_WISPYDBx_PID))) {
				usb_dev_chosen = dev;
				break;
			} else {
				snprintf(phydev->errstr, WISPY_ERROR_MAX,
						 "WISPYDBx_INIT failed, specified device %u does not "
						 "appear to be a Wi-Spy device", cid);
				return -1;
			}
		}
	}

	if (usb_dev_chosen == NULL) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "WISPYDBx_INIT failed, specified device %u does not appear "
				 "to exist.", cid);
		return -1;
	}

	/* Build the device record with 3 sweep capabilities */
	phydev->device_spec = (wispy_dev_spec *) malloc(sizeof(wispy_dev_spec));

	phydev->device_spec->device_id = cid;

	/* Default the name to the buspath */
	snprintf(phydev->device_spec->device_name, WISPY_PHY_NAME_MAX,
			 "Wi-Spy DBx USB %u", cid);

	/* State */
	phydev->state = WISPY_STATE_CLOSED;

	phydev->min_rssi_seen = -1;

	phydev->device_spec->device_version = 0x03;
	phydev->device_spec->device_flags = WISPY_DEV_FL_VAR_SWEEP;

	phydev->device_spec->supported_ranges = 
		(wispy_sample_sweep *) malloc(sizeof(wispy_sample_sweep) * 3);

	phydev->device_spec->num_sweep_ranges = 3;

	wispydbx_add_supportedranges(&phydev->device_spec->num_sweep_ranges,
								 &phydev->device_spec->supported_ranges);

	phydev->device_spec->cur_profile = 0;

	phydev->device_spec->default_range = phydev->device_spec->supported_ranges;


	/* Set up the aux state */
	auxptr = malloc(sizeof(wispydbx_usb_aux));
	phydev->auxptr = auxptr;

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

	char buf[sizeof(wispydbx_report)];
	int x = 0, error = 0;
	fd_set wset;

	struct timeval tm;

	sigset_t signal_set;

	error = 0;

	sock = auxptr->sockpair[1];

	dev = auxptr->dev;
	wispy = auxptr->devhdl;

	/* We don't want to see any signals in the child thread */
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	// fprintf(stderr, "debug - servicethread started\n");

	while (1) {
		/* wait until we're able to write out to the IPC socket, go into a blocking
		 * select */
		FD_ZERO(&wset);
		FD_SET(sock, &wset);

		if (select(sock + 1, NULL, &wset, NULL, NULL) < 0) {
			snprintf(auxptr->phydev->errstr, WISPY_ERROR_MAX,
					 "wispydbx_usb poller failed on IPC write select(): %s",
					 strerror(errno));
			auxptr->usb_thread_alive = 0;
			auxptr->phydev->state = WISPY_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (auxptr->usb_thread_alive == 0) {
			auxptr->phydev->state = WISPY_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (FD_ISSET(sock, &wset) == 0) {
			continue;
		}

		/* Get new data only if we haven't requeued */
		if (error == 0 && auxptr->phydev->state == WISPY_STATE_RUNNING) {
			int len = 0;
			memset(buf, 0, sizeof(wispydbx_report));

			// fprintf(stderr, "debug - running, poll\n");

			if ((len = usb_interrupt_read(wispy, 0x82, buf, 
								   sizeof(wispydbx_report), TIMEOUT)) < 0) {
				if (errno == EAGAIN) {
					// fprintf(stderr, "debug - eagain on usb_interrupt_read\n");
					continue;
				}

				// fprintf(stderr, "debug - failed - %s\n", strerror(errno));
				// fprintf(stderr, "debug - %s\n", usb_strerror());

				snprintf(auxptr->phydev->errstr, WISPY_ERROR_MAX,
						 "wispydbx_usb poller failed to read USB data: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = WISPY_STATE_ERROR;
				pthread_exit(NULL);
			}

			// printf("debug - usb read return %d\n", len);

			/* Send it to the IPC remote, re-queue on enobufs */
			if (send(sock, buf, sizeof(wispydbx_report), 0) < 0) {
				if (errno == ENOBUFS) {
					error = 1;
					continue;
				}

				snprintf(auxptr->phydev->errstr, WISPY_ERROR_MAX,
						 "wispydbx_usb poller failed on IPC send: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = WISPY_STATE_ERROR;
				pthread_exit(NULL);
			}

		}

		error = 0;
	}

	auxptr->usb_thread_alive = 0;
	send(sock, buf, sizeof(wispydbx_report), 0);
	auxptr->phydev->state = WISPY_STATE_ERROR;
	pthread_exit(NULL);
}

int wispydbx_usb_getpollfd(wispy_phy *phydev) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;

	if (auxptr->usb_thread_alive == 0) {
		// fprintf(stderr, "debug - thread alive = 0\n");
		wispydbx_usb_close(phydev);
		return -1;
	}

	// fprintf(stderr, "debug - auxptr sockpair 0 %d\n", auxptr->sockpair[0]);
	return auxptr->sockpair[0];
}

extern int usb_debug;

int wispydbx_usb_open(wispy_phy *phydev) {
	int pid_status;
	struct usb_dev_handle *wispy;
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;
	wispydbx_startsweep startcmd;

	usb_debug = 1024;

	/* Make the client/server socketpair */
	if (socketpair(PF_UNIX, SOCK_DGRAM, 0, auxptr->sockpair) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispydbx_usb open failed to create socket pair for capture "
				 "process: %s", strerror(errno));
		return -1;
	}

	if ((auxptr->devhdl = usb_open(auxptr->dev)) == NULL) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispydbx_usb capture process failed to open USB device: %s",
				 strerror(errno));
		return -1;
	}

#if defined(LIBUSB_HAS_GET_DRIVER_NP) && defined(LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP)
	if (usb_detach_kernel_driver_np(auxptr->devhdl, 0) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "Could not detach device from kernel driver: %s",
				 usb_strerror()); 
	}
#endif

	// fprintf(stderr, "debug - set_configuration\n");
	if (usb_set_configuration(auxptr->devhdl, 1) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "could not configure interface: %s", usb_strerror());
		// fprintf(stderr, "debug - failed to set config: %s\n", usb_strerror());
	}

	// fprintf(stderr, "debug - claiming interface\n");
	if (usb_claim_interface(auxptr->devhdl, 0) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "could not claim interface: %s", usb_strerror());
	}
	// fprintf(stderr, "debug - done claiming\n");

	auxptr->usb_thread_alive = 1;

	// printf("debug - creating thread\n");

	if (pthread_create(&(auxptr->usb_thread), NULL, 
					   wispydbx_usb_servicethread, auxptr) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispydbx_usb capture failed to create thread: %s",
				 strerror(errno));
		auxptr->usb_thread_alive = 0;
		return -1;
	}

	// printf("debug - done creating thread\n");

	/* Update the state */
	phydev->state = WISPY_STATE_CONFIGURING;

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

	// fprintf(stderr, "debug - writing usb start control msg\n");
	if (usb_control_msg(wispy, 
						USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
						HID_SET_REPORT, 
						0x02 + (0x03 << 8),
						0, 
						(uint8_t *) &startcmd, (int) sizeof(wispydbx_startsweep), 
						0) <= 0) {
		// fprintf(stderr, "debug - controlmsg start failed %s\n", strerror(errno));
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispydbx_usb open failed to send start command: %s",
				 strerror(errno));
		phydev->state = WISPY_STATE_ERROR;
		return -1;
	}

	// fprintf(stderr, "debug - finished writing usb control msg\n");

	return 1;
}

int wispydbx_usb_close(wispy_phy *phydev) {
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

wispy_sample_sweep *wispydbx_usb_getsweep(wispy_phy *phydev) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;

	return auxptr->sweepbuf;
}

void wispydbx_usb_setcalibration(wispy_phy *phydev, int in_calib) {
	phydev->state = WISPY_STATE_RUNNING;
}

int wispydbx_usb_poll(wispy_phy *phydev) {
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;
	char lbuf[sizeof(wispydbx_report)];
	int x;
	int base = 0;
	int ret = 0;
	int sweep_full = 0;
	wispydbx_report *report;

	// printf("debug - usb poll\n");

	/* Push a configure event before anything else */
	if (auxptr->configured == 0) {
		auxptr->configured = 1;
		// printf("debug - usb poll return configured\n");
		return WISPY_POLL_CONFIGURED;
	}

	/* Use the error set by the polling thread */
	if (auxptr->usb_thread_alive == 0) {
		phydev->state = WISPY_STATE_ERROR;
		wispydbx_usb_close(phydev);
		// printf("debug - usb poll return error\n");
		return WISPY_POLL_ERROR;
	}

	if ((ret = recv(auxptr->sockpair[0], lbuf, sizeof(wispydbx_report), 0)) < 0) {
		// printf("debug - usb poll return recv error\n");
		if (auxptr->usb_thread_alive != 0)
			snprintf(phydev->errstr, WISPY_ERROR_MAX,
					 "wispydbx_usb IPC receiver failed to read signal data: %s",
					 strerror(errno));
		phydev->state = WISPY_STATE_ERROR;
		return WISPY_POLL_ERROR;
	}

	// printf("debug usb poll recv len %d\n", ret);
	//
	if (ret < sizeof(wispydbx_report)) {
		printf("Short report\n");
		return WISPY_POLL_NONE;
	}

	report = (wispydbx_report *) lbuf;

	/* Extract the slot index */
#ifdef WORDS_BIGENDIAN
	base = endian_swap16(report->packet_index);
#else
	base = report->packet_index;
#endif

	if (base == 0)
		auxptr->sweepbase = 0;
	else
		base = auxptr->sweepbase;

	if (base < 0 || base > auxptr->sweepbuf->num_samples) {
		/* Bunk data, throw it out */
		return WISPY_POLL_NONE;
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
		return WISPY_POLL_NONE;
	}

	for (x = 0; x < 61; x++) {
		if (base + x >= auxptr->sweepbuf->num_samples) {
			sweep_full = 1;
			break;
		}

		auxptr->sweepbuf->sample_data[base + x] = report->data[x];

		if (report->data[x] < phydev->min_rssi_seen)
			phydev->min_rssi_seen = report->data[x];
	}

	auxptr->sweepbase += 61;

	/* Flag that a sweep is complete */
	if (base + 61 == auxptr->sweepbuf->num_samples || sweep_full) {
		gettimeofday(&(auxptr->sweepbuf->tm_end), NULL);
		auxptr->sweepbuf->min_rssi_seen = phydev->min_rssi_seen;

		return WISPY_POLL_SWEEPCOMPLETE;
	}

	return WISPY_POLL_NONE;
}

int wispydbx_usb_setposition(wispy_phy *phydev, int in_profile, 
							 int start_khz, int res_hz) {
	struct usb_dev_handle *wispy;
	wispydbx_rfsettings rfset;
	wispydbx_usb_aux *auxptr = (wispydbx_usb_aux *) phydev->auxptr;
	int use_default = 0;

	uint32_t filter_bw_hz;
	uint16_t points_per_sweep;
	uint8_t samples_per_point;

	// printf("debug - setposition %d %d\n", start_khz, res_hz);

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

	wispy = auxptr->devhdl;

	// printf("debug - writing usb control msg %d\n", 0x02 + (0x03 << 8));
	if (usb_control_msg(wispy, 
						USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
						HID_SET_REPORT, 
						0x02 + (0x03 << 8),
						0, 
						(uint8_t *) &rfset, (int) sizeof(wispydbx_rfsettings), 
						0) == 0) {
		fprintf(stderr, "debug - control_msg_fail: %s\n", usb_strerror());
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispydbx_usb setposition failed to set sweep feature set: %s",
				 strerror(errno));
		phydev->state = WISPY_STATE_ERROR;
		return -1;
	}
	// printf("debug - finished wrting usb control\n");

	/* If we successfully configured the hardware, update the sweep capabilities and
	 * the sweep buffer and reset the device */

#if 0
	phydev->device_spec->num_sweep_ranges = 1;
	if (phydev->device_spec->supported_ranges)
		free(phydev->device_spec->supported_ranges);
	phydev->device_spec->supported_ranges = 
		(wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(0));

	/* Set the universal values */
	phydev->device_spec->supported_ranges[0].num_samples = WISPYDBx_USB_NUM_SAMPLES;

	phydev->device_spec->supported_ranges[0].amp_offset_mdbm = WISPYDBx_USB_OFFSET_MDBM;
	phydev->device_spec->supported_ranges[0].amp_res_mdbm = WISPYDBx_USB_RES_MDBM;
	phydev->device_spec->supported_ranges[0].rssi_max = WISPYDBx_USB_RSSI_MAX;

	/* Set the sweep records based on default or new data */
	if (use_default == 1) {
		phydev->device_spec->supported_ranges[0].start_khz = WISPYDBx_USB_DEF_STARTKHZ;
		phydev->device_spec->supported_ranges[0].end_khz = 
			WISPYDBx_USB_DEF_STARTKHZ + ((WISPYDBx_USB_NUM_SAMPLES *
										  WISPYDBx_USB_DEF_RESHZ) / 1000);
		phydev->device_spec->supported_ranges[0].res_hz = WISPYDBx_USB_DEF_RESHZ;
	} else {
		phydev->device_spec->supported_ranges[0].start_khz = start_khz;
		phydev->device_spec->supported_ranges[0].end_khz = 
			start_khz + ((WISPYDBx_USB_NUM_SAMPLES * res_hz) / 1000);
		phydev->device_spec->supported_ranges[0].res_hz = res_hz;
	}
#endif

	/* We're not configured, so we need to push a new configure block out next time
	 * we sweep */
	auxptr->configured = 0;

	/* Rebuild the sweep buffer */
	if (auxptr->sweepbuf)
		free(auxptr->sweepbuf);

	auxptr->sweepbuf =
		(wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(phydev->device_spec->supported_ranges[in_profile].num_samples));
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

