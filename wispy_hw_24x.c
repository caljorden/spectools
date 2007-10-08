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

#define METAGEEK_WISPY24x_VID		0x1781
#define METAGEEK_WISPY24x_PID		0x083f

/* Default # of samples */
#define WISPY24x_USB_NUM_SAMPLES		256

/* Set data feature defaults */
#define WISPY24x_USB_DEF_CARRIER		0x58e2f8
#define WISPY24x_USB_DEF_STEP_MANT		0x8e
#define WISPY24x_USB_DEF_STEP_EXP		0x03
#define WISPY24x_USB_DEF_BW				0x05
#define WISPY24x_USB_DEF_BW_AVG			0x01
#define WISPY24x_USB_DEF_STEPS			255
#define WISPY24x_USB_DEF_READS			6
#define WISPY24x_USB_DEF_H_MINKHZ		2399938
#define WISPY24x_USB_DEF_H_RESHZ		327942

#define WISPY24x_USB_CARRIER_C_FROM_KHZ(K)	(int) ceil((float) (K) / \
											((27.0f / pow(2, 16)) * 1000))
#define WISPY24x_USB_CARRIER_KHZ_FROM_C(C)	(int) ((27.0f / pow(2, 16)) * 1000 * (C))
/* Calculate the spacing mantissa from khz */
#define WISPY24x_USB_RES_M_FROM_HZ(H,E)	(int) (((H) / ((27.0f / pow(2, 18)) * \
								 pow(2, (E)) * 1000000)) - 256.0)
#define WISPY24x_USB_RES_HZ_FROM_ME(M,E)	(int) ((27.0f / pow(2, 18)) * (256 + (M)) * \
								 pow(2, (E)) * 1000000)
/* Calculate the bandwidth - Typically we want 3:1 bandwidth:sampleres */
#define WISPY24x_USB_BW_HZ_FROM_ME(M,E)	(int) (27.0f / (8 * (4 + (M)) * \
								 pow(2, (E))) * 1000000)
#define WISPY24x_USB_BW_M_FROM_HZ(H,E)		(int) ((27.0f * 1000000 / ((H) * \
								 8 * pow(2, (E)))) - 4)

#define WISPY24x_USB_BW_EXTRACT_M(D) 	(int) (((D) & 0xC) >> 2)
#define WISPY24x_USB_BW_EXTRACT_E(D)	(int) (((D) & 0x3))
#define WISPY24x_USB_BW_BUILD(M,E)		(int) ((((M) << 2) | ((E))) & 0xF)

#define WISPY24x_USB_OFFSET_MDBM		-134000
#define WISPY24x_USB_RES_MDBM			500
#define WISPY24x_USB_RSSI_MAX			222

/*
#define WISPY24x_USB_MIN_SAMPLE			-125
#define WISPY24x_USB_MIN_SIGNAL			-119
#define WISPY24x_USB_MAX_SAMPLE			-23

#define WISPY24x_USB_RSSI(x)			(((float) (x) * 0.5) - 134.0)
*/

#include "spectool_container.h"
#include "wispy_hw_24x.h"

#define endian_swap32(x) \
({ \
    uint32_t __x = (x); \
    ((uint32_t)( \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0x000000ff) << 24) | \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0x0000ff00) << 8) | \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0x00ff0000) >> 8) | \
        (uint32_t)(((uint32_t)(__x) & (uint32_t)0xff000000) >> 24) )); \
})

/* Aux tracking struct for wispy1 characteristics */
typedef struct _wispy24x_usb_aux {
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
} wispy24x_usb_aux;

typedef struct _wispy24x_rfsettings {
	uint8_t feature_id;
	uint32_t freq_carrier : 24;
	uint8_t step_mantissa;
	uint8_t step_exponent;
	uint8_t bandwidth;
	uint8_t bw_average;
	uint8_t num_steps;
	uint8_t num_reads;
	uint32_t freq_min_khz;
	uint32_t freq_res_hz : 24;
} __attribute__((packed)) wispy24x_rfsettings;

typedef struct _wispy24x_report {
	uint8_t report_id;
	uint32_t freq_start_khz;
	uint32_t freq_res_hz : 24;
	uint8_t valid_bytes;
	uint8_t data[54];
} __attribute__((packed)) wispy24x_report;

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

int wispy24x_usb_detach_hack(struct usb_dev_handle *dev, int interface, char *errstr) {
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
int wispy24x_usb_open(wispy_phy *);
int wispy24x_usb_close(wispy_phy *);
int wispy24x_usb_thread_close(wispy_phy *);
int wispy24x_usb_poll(wispy_phy *);
int wispy24x_usb_getpollfd(wispy_phy *);
void wispy24x_usb_setcalibration(wispy_phy *, int);
int wispy24x_usb_setposition(wispy_phy *, int, int);
wispy_sample_sweep *wispy24x_usb_getsweep(wispy_phy *);

uint32_t wispy24x_adler_checksum(const char *buf1, int len) {
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
int wispy24x_usb_device_scan(wispy_device_list *list) {
	struct usb_bus *bus;
	struct usb_device *dev;
	int num_found = 0;
	wispy24x_usb_pair *auxpair;
	char combopath[128];

	/* Libusb init */
	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (((dev->descriptor.idVendor == METAGEEK_WISPY24x_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY24x_PID))) {

				/* If we're full up, break */
				if (list->num_devs == list->max_devs - 1)
					break;

				auxpair = (wispy24x_usb_pair *) malloc(sizeof(wispy24x_usb_pair));

				snprintf(auxpair->bus, 64, "%s", bus->dirname);
				snprintf(auxpair->dev, 64, "%s", dev->filename);

				snprintf(combopath, 128, "%s%s", auxpair->bus, auxpair->dev);

				/* Fill in the list elements */
				list->list[list->num_devs].device_id = 
					wispy24x_adler_checksum(combopath, 128);
				snprintf(list->list[list->num_devs].name, WISPY_PHY_NAME_MAX,
						 "Wi-Spy 24x USB %u", bus->dirname, dev->filename,
						 list->list[list->num_devs].device_id);
				list->list[list->num_devs].init_func = wispy24x_usb_init;
				list->list[list->num_devs].hw_rec = auxpair;

				list->num_devs++;

				num_found++;
			}
		}
	}

	return num_found;
}

int wispy24x_usb_init(wispy_phy *phydev, wispy_device_rec *rec) {
	wispy24x_usb_pair *auxpair = (wispy24x_usb_pair *) rec->hw_rec;

	if (auxpair == NULL)
		return -1;

	return wispy24x_usb_init_path(phydev, auxpair->bus, auxpair->dev);
}

/* Initialize a specific USB device based on bus and device IDs passed by the UI */
int wispy24x_usb_init_path(wispy_phy *phydev, char *buspath, char *devpath) {
	struct usb_bus *bus = NULL;
	struct usb_device *dev = NULL;

	struct usb_device *usb_dev_chosen = NULL;

	char combopath[128];
	uint32_t cid;

	wispy24x_usb_aux *auxptr = NULL;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	snprintf(combopath, 128, "%s%s", buspath, devpath);
	cid = wispy24x_adler_checksum(combopath, 128);

	/* Don't know if a smarter way offhand, and we don't do this often, so just
	 * crawl and compare */
	for (bus = usb_busses; bus; bus = bus->next) {
		if (strcmp(bus->dirname, buspath))
			continue;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (strcmp(dev->filename, devpath))
				continue;

			if (((dev->descriptor.idVendor == METAGEEK_WISPY24x_VID) &&
				 (dev->descriptor.idProduct == METAGEEK_WISPY24x_PID))) {
				usb_dev_chosen = dev;
				break;
			} else {
				snprintf(phydev->errstr, WISPY_ERROR_MAX,
						 "WISPY24x_INIT failed, specified device %u does not "
						 "appear to be a Wi-Spy device", cid);
				return -1;
			}
		}
	}

	if (usb_dev_chosen == NULL) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "WISPY24x_INIT failed, specified device %u does not appear "
				 "to exist.", cid);
		return -1;
	}

	/* Build the device record with one sweep capability */
	phydev->device_spec = (wispy_dev_spec *) malloc(sizeof(wispy_dev_spec));

	phydev->device_spec->device_id = cid;

	/* Default the name to the buspath */
	snprintf(phydev->device_spec->device_name, WISPY_PHY_NAME_MAX,
			 "Wi-Spy 24x USB %u", cid);

	/* State */
	phydev->state = WISPY_STATE_CLOSED;

	phydev->min_rssi_seen = -1;

	phydev->device_spec->device_version = 0x02;
	phydev->device_spec->device_flags = WISPY_DEV_FL_VAR_SWEEP;

	/* Will be filled in by setposition later */
	phydev->device_spec->num_sweep_ranges = 0;
	phydev->device_spec->supported_ranges = NULL;

	phydev->device_spec->default_range.num_samples = WISPY24x_USB_NUM_SAMPLES;
	/*
	phydev->device_spec->default_range.min_sample = WISPY24x_USB_MIN_SAMPLE;
	phydev->device_spec->default_range.min_sig_report = WISPY24x_USB_MIN_SIGNAL;
	phydev->device_spec->default_range.max_sample = WISPY24x_USB_MAX_SAMPLE;
	*/

	phydev->device_spec->default_range.amp_offset_mdbm = WISPY24x_USB_OFFSET_MDBM;
	phydev->device_spec->default_range.amp_res_mdbm = WISPY24x_USB_RES_MDBM;
	phydev->device_spec->default_range.rssi_max = WISPY24x_USB_RSSI_MAX;

	phydev->device_spec->default_range.start_khz = WISPY24x_USB_DEF_H_MINKHZ;
	phydev->device_spec->default_range.end_khz = 
		WISPY24x_USB_DEF_H_MINKHZ + ((WISPY24x_USB_DEF_STEPS *
									  WISPY24x_USB_DEF_H_RESHZ) / 1000);
	phydev->device_spec->default_range.res_hz = WISPY24x_USB_DEF_H_RESHZ;

	/* Set up the aux state */
	auxptr = malloc(sizeof(wispy24x_usb_aux));
	phydev->auxptr = auxptr;

	auxptr->configured = 0;

	auxptr->dev = dev;
	auxptr->devhdl = NULL;
	auxptr->phydev = phydev;
	auxptr->sockpair[0] = -1;
	auxptr->sockpair[1] = -1;

	/* Will be filled in by setposition later */
	auxptr->sweepbuf_initialized = 0;
	auxptr->sweepbuf = NULL;

	phydev->open_func = &wispy24x_usb_open;
	phydev->close_func = &wispy24x_usb_close;
	phydev->poll_func = &wispy24x_usb_poll;
	phydev->pollfd_func = &wispy24x_usb_getpollfd;
	phydev->setcalib_func = &wispy24x_usb_setcalibration;
	phydev->getsweep_func = &wispy24x_usb_getsweep;
	phydev->setposition_func = &wispy24x_usb_setposition;

	return 0;
}

void *wispy24x_usb_servicethread(void *aux) {
	wispy24x_usb_aux *auxptr = (wispy24x_usb_aux *) aux;

	int sock;
	struct usb_device *dev;
	struct usb_dev_handle *wispy;

	char buf[64];
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

	while (1) {
		/* wait until we're able to write out to the IPC socket, go into a blocking
		 * select */
		FD_ZERO(&wset);
		FD_SET(sock, &wset);

		if (select(sock + 1, NULL, &wset, NULL, NULL) < 0) {
			snprintf(auxptr->phydev->errstr, WISPY_ERROR_MAX,
					 "wispy24x_usb poller failed on IPC write select(): %s",
					 strerror(errno));
			auxptr->usb_thread_alive = 0;
			auxptr->phydev->state = WISPY_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (auxptr->usb_thread_alive == 0) {
			auxptr->phydev->state = WISPY_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (FD_ISSET(sock, &wset) == 0)
			continue;

		/* Get new data only if we haven't requeued */
		if (error == 0) {
			memset(buf, 0, 64);

			if (usb_interrupt_read(wispy, 0x81, buf, 64, TIMEOUT) == 0) {
				if (errno == EAGAIN)
					continue;

				snprintf(auxptr->phydev->errstr, WISPY_ERROR_MAX,
						 "wispy24x_usb poller failed to read USB data: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = WISPY_STATE_ERROR;
				pthread_exit(NULL);
			}

			/* Send it to the IPC remote, re-queue on enobufs */
			if (send(sock, buf, 64, 0) < 0) {
				if (errno == ENOBUFS) {
					error = 1;
					continue;
				}

				snprintf(auxptr->phydev->errstr, WISPY_ERROR_MAX,
						 "wispy24x_usb poller failed on IPC send: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = WISPY_STATE_ERROR;
				pthread_exit(NULL);
			}

		}

		error = 0;
	}

	auxptr->usb_thread_alive = 0;
	send(sock, buf, 64, 0);
	auxptr->phydev->state = WISPY_STATE_ERROR;
	pthread_exit(NULL);
}

int wispy24x_usb_getpollfd(wispy_phy *phydev) {
	wispy24x_usb_aux *auxptr = (wispy24x_usb_aux *) phydev->auxptr;

	if (auxptr->usb_thread_alive == 0) {
		wispy24x_usb_close(phydev);
		return -1;
	}

	return auxptr->sockpair[0];
}

int wispy24x_usb_open(wispy_phy *phydev) {
	int pid_status;
	wispy24x_usb_aux *auxptr = (wispy24x_usb_aux *) phydev->auxptr;

	/* Make the client/server socketpair */
	if (socketpair(PF_UNIX, SOCK_DGRAM, 0, auxptr->sockpair) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispy24x_usb open failed to create socket pair for capture "
				 "process: %s", strerror(errno));
		return -1;
	}

	if ((auxptr->devhdl = usb_open(auxptr->dev)) == NULL) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispy24x_usb capture process failed to open USB device: %s",
				 strerror(errno));
		return -1;
	}

	/* We'll fail later if this really failed for this platform */
	usb_set_configuration(auxptr->devhdl, auxptr->dev->config->bConfigurationValue);

#ifndef SYS_DARWIN
	/* Claim the device on non-OSX systems */
	if (usb_claim_interface(auxptr->devhdl, 0) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "could not claim interface: %s", usb_strerror());
#ifdef LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP
		if (usb_detach_kernel_driver_np(auxptr->devhdl, 0) < 0) {
			fprintf(stderr, "Could not detach kernel driver %s\n", usb_strerror());
			snprintf(phydev->errstr, WISPY_ERROR_MAX,
					 "Could not detach device from kernel driver: %s",
					 usb_strerror());
#endif
#ifdef SYS_LINUX
		if (wispy24x_usb_detach_hack(auxptr->devhdl, 0, phydev->errstr) < 0) {
			return -1;
		}

		usb_set_configuration(auxptr->devhdl, auxptr->dev->config->bConfigurationValue);

		if (usb_claim_interface(auxptr->devhdl, 0) < 0) {
			snprintf(phydev->errstr, WISPY_ERROR_MAX,
					 "wispy24x_usb capture process detached device but still "
					 "can't claim interface: %s", strerror(errno));
			return -1;
		}
#else
		return -1;
#endif
#ifdef LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP
		}
#endif
	}
#endif

#ifdef SYS_DARWIN
	usb_set_configuration(auxptr->devhdl, 
						  auxptr->dev->config->bConfigurationValue);
#endif

	auxptr->usb_thread_alive = 1;

	if (pthread_create(&(auxptr->usb_thread), NULL, 
					   wispy24x_usb_servicethread, auxptr) < 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispy24x_usb capture failed to create thread: %s",
				 strerror(errno));
		auxptr->usb_thread_alive = 0;
		return -1;
	}

	/* Update the state */
	phydev->state = WISPY_STATE_CONFIGURING;

	/* Push the config down to the device */
	if (wispy24x_usb_setposition(phydev, 0, 0) < 0)
		return -1;

	return 1;
}

int wispy24x_usb_close(wispy_phy *phydev) {
	wispy24x_usb_aux *aux;
	
	if (phydev == NULL)
		return 0;

	aux = (wispy24x_usb_aux *) phydev->auxptr;

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

wispy_sample_sweep *wispy24x_usb_getsweep(wispy_phy *phydev) {
	wispy24x_usb_aux *auxptr = (wispy24x_usb_aux *) phydev->auxptr;

	return auxptr->sweepbuf;
}

void wispy24x_usb_setcalibration(wispy_phy *phydev, int in_calib) {
	phydev->state = WISPY_STATE_RUNNING;
}

int wispy24x_usb_poll(wispy_phy *phydev) {
	wispy24x_usb_aux *auxptr = (wispy24x_usb_aux *) phydev->auxptr;
	char lbuf[64];
	int x;
	int base, res;
	wispy24x_report *report;

	/* Push a configure event before anything else */
	if (auxptr->configured == 0) {
		auxptr->configured = 1;
		return WISPY_POLL_CONFIGURED;
	}

	/* Use the error set by the polling thread */
	if (auxptr->usb_thread_alive == 0) {
		phydev->state = WISPY_STATE_ERROR;
		wispy24x_usb_close(phydev);
		return WISPY_POLL_ERROR;
	}

	if (recv(auxptr->sockpair[0], lbuf, 64, 0) < 0) {
		if (auxptr->usb_thread_alive != 0)
			snprintf(phydev->errstr, WISPY_ERROR_MAX,
					 "wispy24x_usb IPC receiver failed to read signal data: %s",
					 strerror(errno));
		phydev->state = WISPY_STATE_ERROR;
		return WISPY_POLL_ERROR;
	}

	report = (wispy24x_report *) lbuf;

	/* Derive the base slot from the khz start of this sample, based on
	 * our sweep record */
#ifdef WORDS_BIGENDIAN
	base = endian_swap32(report->freq_start_khz) - auxptr->sweepbuf->start_khz;
	res = endian_swap32(report->freq_res_hz) >> 8;
#else
	base = report->freq_start_khz - auxptr->sweepbuf->start_khz;
	res = report->freq_res_hz;
#endif

	/*
	if (base != 0)
		base = (base) / ((float) auxptr->sweepbuf->res_hz / 1000);
	*/
	
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

	for (x = 0; x < report->valid_bytes; x++) {
		if (base + x >= auxptr->sweepbuf->num_samples) {
			break;
		}

		/*
		auxptr->sweepbuf->sample_data[base + x] =
			WISPY24x_USB_RSSI(report->data[x]);
		*/
		auxptr->sweepbuf->sample_data[base + x] = report->data[x];

		if (report->data[x] < phydev->min_rssi_seen)
			phydev->min_rssi_seen = report->data[x];
	}

	auxptr->sweepbase += report->valid_bytes;

	/* Flag that a sweep is complete */
	if (base + report->valid_bytes == auxptr->sweepbuf->num_samples) {
		gettimeofday(&(auxptr->sweepbuf->tm_end), NULL);
		auxptr->sweepbuf->min_rssi_seen = phydev->min_rssi_seen;
		return WISPY_POLL_SWEEPCOMPLETE;
	}

	return WISPY_POLL_NONE;
}

int wispy24x_usb_setposition(wispy_phy *phydev, int start_khz, int res_hz) {
	int temp_d, temp_m;
	int best_s_m = 0, best_s_e = 0, best_b_m = 0, best_b_e = 0;
	int m = 0, e = 0, best_d;
	int target_bw;
	struct usb_dev_handle *wispy;
	wispy24x_rfsettings rfset;
	wispy24x_usb_aux *auxptr = (wispy24x_usb_aux *) phydev->auxptr;

	if (start_khz == 0) {
		start_khz = WISPY24x_USB_DEF_H_MINKHZ;
		res_hz = WISPY24x_USB_DEF_H_RESHZ;
		best_s_m = WISPY24x_USB_DEF_STEP_MANT;
		best_s_e = WISPY24x_USB_DEF_STEP_EXP;
		best_b_m = WISPY24x_USB_BW_EXTRACT_M(WISPY24x_USB_DEF_BW);
		best_b_e = WISPY24x_USB_BW_EXTRACT_E(WISPY24x_USB_DEF_BW);
	} else {
		/* Brute-force search the exponents and use the reverse of the function as the
		 * fitness test.  Pick the resolution which is closest to our requested one. */
		best_d = INT_MAX;
		for (e = 0; e <= 4; e++) {
			temp_m = WISPY24x_USB_RES_M_FROM_HZ(res_hz, e) & 0xFF;
			temp_d = abs(res_hz - WISPY24x_USB_RES_HZ_FROM_ME(temp_m, e));

			if (temp_d < best_d) {
				best_d = temp_d;
				best_s_m = temp_m;
				best_s_e = e;
			}
		}

		/* Brute force the bandwidth w/ the mantissa and exponent again and look for the 
		 * bandwidth closest to what we want.
		 * TODO - add bw allocation sliding based on requested resolution, suggested is 
		 * 1:1 for higher ranges of the resolution and 3:1 for lower ranges
		 */
		if (res_hz < 100000)
			target_bw = res_hz * 3;
		else if (res_hz < 200000)
			target_bw = res_hz * 2;
		else
			target_bw = res_hz;
		best_d = INT_MAX;
		for (m = 0; m <= 4; m++) {
			for (e = 0; e <= 4; e++) {
				temp_d = abs(target_bw - WISPY24x_USB_BW_HZ_FROM_ME(m, e));

				if (temp_d < best_d) {
					best_d = temp_d;
					best_b_m = m;
					best_b_e = e;
				}
			}
		}

		/* Reset start_khz to the derived/reversed startkhz to account for any rounding
		 * in the function */
		start_khz = 
			WISPY24x_USB_CARRIER_KHZ_FROM_C(WISPY24x_USB_CARRIER_C_FROM_KHZ(start_khz));
		/* reset the res_hz to the derived value */
		res_hz = WISPY24x_USB_RES_HZ_FROM_ME(best_s_m, best_s_e);
	}

	/* Initialize the hw sweep features */
	rfset.feature_id = 0x02;

	/* Multibytes have to be handled in USB-endian (little) */
#ifdef WORDS_BIGENDIAN
	/* 24-bit values get shifted over to align properly */
	rfset.freq_carrier = endian_swap32(WISPY24x_USB_CARRIER_C_FROM_KHZ(start_khz)) >> 8;
	rfset.freq_min_khz = endian_swap32(start_khz);
	rfset.freq_res_hz = endian_swap32(res_hz) >> 8;
#else
	rfset.freq_carrier = WISPY24x_USB_CARRIER_C_FROM_KHZ(start_khz);
	rfset.freq_min_khz = start_khz;
	rfset.freq_res_hz = res_hz;
#endif

	rfset.step_mantissa = best_s_m;
	rfset.step_exponent = best_s_e;
	rfset.bandwidth = WISPY24x_USB_BW_BUILD(best_b_m, best_b_e);
	rfset.bw_average = WISPY24x_USB_DEF_BW_AVG;
	rfset.num_steps = WISPY24x_USB_DEF_STEPS;
	rfset.num_reads = WISPY24x_USB_DEF_READS;

	wispy = auxptr->devhdl;

	if (usb_control_msg(wispy, 
						USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
						HID_SET_REPORT, 
						0x02 + (0x03 << 8),
						0, 
						(uint8_t *) &rfset, (int) sizeof(wispy24x_rfsettings), 
						TIMEOUT) == 0) {
		snprintf(phydev->errstr, WISPY_ERROR_MAX,
				 "wispy24x_usb setposition failed to set sweep feature set: %s",
				 strerror(errno));
		phydev->state = WISPY_STATE_ERROR;
		return -1;
	}

	/* If we successfully configured the hardware, update the sweep capabilities and
	 * the sweep buffer and reset the device */

	phydev->device_spec->num_sweep_ranges = 1;
	if (phydev->device_spec->supported_ranges)
		free(phydev->device_spec->supported_ranges);
	phydev->device_spec->supported_ranges = 
		(wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(0));

	/* Set the universal values */
	phydev->device_spec->supported_ranges[0].num_samples = WISPY24x_USB_NUM_SAMPLES;

	phydev->device_spec->supported_ranges[0].amp_offset_mdbm = WISPY24x_USB_OFFSET_MDBM;
	phydev->device_spec->supported_ranges[0].amp_res_mdbm = WISPY24x_USB_RES_MDBM;
	phydev->device_spec->supported_ranges[0].rssi_max = WISPY24x_USB_RSSI_MAX;

	/* Set the sweep records based on default or new data */
	if (start_khz == 0) {
		phydev->device_spec->supported_ranges[0].start_khz = WISPY24x_USB_DEF_H_MINKHZ;
		phydev->device_spec->supported_ranges[0].end_khz = 
			WISPY24x_USB_DEF_H_MINKHZ + ((WISPY24x_USB_NUM_SAMPLES *
										  WISPY24x_USB_DEF_H_RESHZ) / 1000);
		phydev->device_spec->supported_ranges[0].res_hz = WISPY24x_USB_DEF_H_RESHZ;
	} else {
		phydev->device_spec->supported_ranges[0].start_khz = start_khz;
		phydev->device_spec->supported_ranges[0].end_khz = 
			start_khz + ((WISPY24x_USB_NUM_SAMPLES * res_hz) / 1000);
		phydev->device_spec->supported_ranges[0].res_hz = res_hz;
	}

	/* We're not configured, so we need to push a new configure block out next time
	 * we sweep */
	auxptr->configured = 0;

	/* Rebuild the sweep buffer */
	if (auxptr->sweepbuf)
		free(auxptr->sweepbuf);

	auxptr->sweepbuf =
		(wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(WISPY24x_USB_NUM_SAMPLES));
	auxptr->sweepbuf->start_khz = 
		phydev->device_spec->supported_ranges[0].start_khz;
	auxptr->sweepbuf->end_khz = 
		phydev->device_spec->supported_ranges[0].end_khz;
	auxptr->sweepbuf->res_hz = 
		phydev->device_spec->supported_ranges[0].res_hz;
	auxptr->sweepbuf->num_samples = 
		phydev->device_spec->supported_ranges[0].num_samples;

	auxptr->sweepbuf->amp_offset_mdbm =
		phydev->device_spec->supported_ranges[0].amp_offset_mdbm;
	auxptr->sweepbuf->amp_res_mdbm =
		phydev->device_spec->supported_ranges[0].amp_res_mdbm;
	auxptr->sweepbuf->rssi_max =
		phydev->device_spec->supported_ranges[0].rssi_max;

	/*
	auxptr->sweepbuf->min_sample = 
		phydev->device_spec->supported_ranges[0].min_sample;
	auxptr->sweepbuf->min_sig_report = 
		phydev->device_spec->supported_ranges[0].min_sig_report;
	auxptr->sweepbuf->max_sample =
		phydev->device_spec->supported_ranges[0].max_sample;
	*/

	auxptr->sweepbuf_initialized = 0;
	auxptr->num_sweeps = -1;
}

