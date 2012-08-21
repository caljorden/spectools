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

#define UBERTOOTH_U1_VID		0xffff
#define UBERTOOTH_U1_PID		0x0004

#define UBERTOOTH_U1_NEW_VID		0x1d50
#define UBERTOOTH_U1_NEW_PID		0x6002

/* # of samples to average */
#define UBERTOOTH_U1_AVG_SAMPLES		3

/* Default # of samples */
#define UBERTOOTH_U1_NUM_SAMPLES		79

#define UBERTOOTH_U1_OFFSET_MDBM		-109000
#define UBERTOOTH_U1_RES_MDBM			1000
#define UBERTOOTH_U1_RSSI_MAX			58

#define UBERTOOTH_U1_DEF_H_MINKHZ		2402000
#define UBERTOOTH_U1_DEF_H_RESHZ		1000000
#define UBERTOOTH_U1_DEF_H_STEPS		78

#include "spectool_container.h"
#include "ubertooth_hw_u1.h"

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

typedef struct _ubertooth_u1_sample {
	uint16_t be_freq;
	int8_t rssi;
} __attribute__((packed)) ubertooth_u1_sample;

typedef struct _ubertooth_u1_report {
	uint8_t type;
	uint8_t status;
	uint8_t channel;
	uint8_t clk_high;
	uint32_t clk_100ns;
	uint8_t reserved[6];
	ubertooth_u1_sample data[16];
} __attribute__((packed)) ubertooth_u1_report;

/* Aux tracking struct for wispy1 characteristics */
typedef struct _ubertooth_u1_aux {
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

	/* Sweep buffer we fill */
	spectool_sample_sweep *sweepbuf;
	/* Sweep buffer we return */
	spectool_sample_sweep *full_sweepbuf;

	/* peak samples */
	spectool_sweep_cache *peak_cache;

	int sockpair[2];

	int sweepbase;

	/* Primed - we don't start at 1 */
	int primed;

	spectool_phy *phydev;
} ubertooth_u1_aux;

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

int ubertooth_u1_detach_hack(struct usb_dev_handle *dev, int interface, char *errstr) {
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
int ubertooth_u1_open(spectool_phy *);
int ubertooth_u1_close(spectool_phy *);
int ubertooth_u1_thread_close(spectool_phy *);
int ubertooth_u1_poll(spectool_phy *);
int ubertooth_u1_getpollfd(spectool_phy *);
void ubertooth_u1_setcalibration(spectool_phy *, int);
int ubertooth_u1_setposition(spectool_phy *, int, int, int);
spectool_sample_sweep *ubertooth_u1_getsweep(spectool_phy *);
spectool_sample_sweep *ubertooth_u1_build_sweepbuf(spectool_phy *);

uint32_t ubertooth_u1_adler_checksum(const char *buf1, int len) {
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
int ubertooth_u1_device_scan(spectool_device_list *list) {
	struct usb_bus *bus;
	struct usb_device *dev;
	int num_found = 0;
	ubertooth_u1_usb_pair *auxpair;
	char combopath[128];

	/* Libusb init */
	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (((dev->descriptor.idVendor == UBERTOOTH_U1_VID) &&
				 (dev->descriptor.idProduct == UBERTOOTH_U1_PID)) ||
                            ((dev->descriptor.idVendor == UBERTOOTH_U1_NEW_VID) &&
				 (dev->descriptor.idProduct == UBERTOOTH_U1_NEW_PID))) {
				/* If we're full up, break */
				if (list->num_devs == list->max_devs - 1)
					break;

				auxpair = 
					(ubertooth_u1_usb_pair *) malloc(sizeof(ubertooth_u1_usb_pair));

				snprintf(auxpair->bus, 64, "%s", bus->dirname);
				snprintf(auxpair->dev, 64, "%s", dev->filename);

				snprintf(combopath, 128, "%s%s", auxpair->bus, auxpair->dev);

				/* Fill in the list elements */
				list->list[list->num_devs].device_id = 
					wispy24x_adler_checksum(combopath, 128);
				snprintf(list->list[list->num_devs].name, SPECTOOL_PHY_NAME_MAX,
						 "Ubertooth One USB %u", list->list[list->num_devs].device_id);

				list->list[list->num_devs].init_func = ubertooth_u1_init;
				list->list[list->num_devs].hw_rec = auxpair;

				list->list[list->num_devs].num_sweep_ranges = 1;
				list->list[list->num_devs].supported_ranges =
					(spectool_sample_sweep *) malloc(sizeof(spectool_sample_sweep));

				list->list[list->num_devs].supported_ranges[0].name = 
					strdup("2.4GHz ISM");

				list->list[list->num_devs].supported_ranges[0].num_samples = 
					UBERTOOTH_U1_NUM_SAMPLES;

				list->list[list->num_devs].supported_ranges[0].amp_offset_mdbm = 
					UBERTOOTH_U1_OFFSET_MDBM;
				list->list[list->num_devs].supported_ranges[0].amp_res_mdbm = 
					UBERTOOTH_U1_RES_MDBM;
				list->list[list->num_devs].supported_ranges[0].rssi_max = 
					UBERTOOTH_U1_RSSI_MAX;

				list->list[list->num_devs].supported_ranges[0].start_khz = 
					UBERTOOTH_U1_DEF_H_MINKHZ;
				list->list[list->num_devs].supported_ranges[0].end_khz = 
					UBERTOOTH_U1_DEF_H_MINKHZ + ((UBERTOOTH_U1_DEF_H_STEPS *
												  UBERTOOTH_U1_DEF_H_RESHZ) / 1000);
				list->list[list->num_devs].supported_ranges[0].res_hz = 
					UBERTOOTH_U1_DEF_H_RESHZ;

				list->num_devs++;

				num_found++;
			}
		}
	}

	return num_found;
}

int ubertooth_u1_init(spectool_phy *phydev, spectool_device_rec *rec) {
	ubertooth_u1_usb_pair *auxpair = (ubertooth_u1_usb_pair *) rec->hw_rec;

	if (auxpair == NULL)
		return -1;

	return ubertooth_u1_init_path(phydev, auxpair->bus, auxpair->dev);
}

/* Initialize a specific USB device based on bus and device IDs passed by the UI */
int ubertooth_u1_init_path(spectool_phy *phydev, char *buspath, char *devpath) {
	struct usb_bus *bus = NULL;
	struct usb_device *dev = NULL;

	struct usb_device *usb_dev_chosen = NULL;

	char combopath[128];
	uint32_t cid;

	ubertooth_u1_aux *auxptr = NULL;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	snprintf(combopath, 128, "%s%s", buspath, devpath);
	cid = ubertooth_u1_adler_checksum(combopath, 128);

	/* Don't know if a smarter way offhand, and we don't do this often, so just
	 * crawl and compare */
	for (bus = usb_busses; bus; bus = bus->next) {
		if (strcmp(bus->dirname, buspath))
			continue;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (strcmp(dev->filename, devpath))
				continue;

			if (((dev->descriptor.idVendor == UBERTOOTH_U1_VID) &&
				 (dev->descriptor.idProduct == UBERTOOTH_U1_PID)) ||
                            ((dev->descriptor.idVendor == UBERTOOTH_U1_NEW_VID) &&
                                 (dev->descriptor.idProduct == UBERTOOTH_U1_NEW_PID))) {
				usb_dev_chosen = dev;
				break;
			} else {
				snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
						 "UBERTOOTH_U1_INIT failed, specified device %u does not "
						 "appear to be an Ubertooth One device", cid);
				return -1;
			}
		}
	}

	if (usb_dev_chosen == NULL) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "UBERTOOTH_U1_INIT failed, specified device %u does not appear "
				 "to exist.", cid);
		return -1;
	}

	/* Build the device record with one sweep capability */
	phydev->device_spec = (spectool_dev_spec *) malloc(sizeof(spectool_dev_spec));

	phydev->device_spec->device_id = cid;

	/* Default the name to the buspath */
	snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX,
			 "Ubertooth One %u", cid);

	/* State */
	phydev->state = SPECTOOL_STATE_CLOSED;

	phydev->min_rssi_seen = -1;

	phydev->device_spec->device_version = 0x02;
	phydev->device_spec->device_flags = SPECTOOL_DEV_FL_NONE;

	phydev->device_spec->num_sweep_ranges = 1;
	phydev->device_spec->supported_ranges =
		(spectool_sample_sweep *) malloc(sizeof(spectool_sample_sweep));

	phydev->device_spec->default_range = phydev->device_spec->supported_ranges;

	phydev->device_spec->default_range->name = strdup("2.4GHz ISM");

	phydev->device_spec->default_range->num_samples = UBERTOOTH_U1_NUM_SAMPLES;

	phydev->device_spec->default_range->amp_offset_mdbm = UBERTOOTH_U1_OFFSET_MDBM;
	phydev->device_spec->default_range->amp_res_mdbm = UBERTOOTH_U1_RES_MDBM;
	phydev->device_spec->default_range->rssi_max = UBERTOOTH_U1_RSSI_MAX;

	phydev->device_spec->default_range->start_khz = UBERTOOTH_U1_DEF_H_MINKHZ;
	phydev->device_spec->default_range->end_khz = 
		UBERTOOTH_U1_DEF_H_MINKHZ + ((UBERTOOTH_U1_DEF_H_STEPS *
									  UBERTOOTH_U1_DEF_H_RESHZ) / 1000);
	phydev->device_spec->default_range->res_hz = UBERTOOTH_U1_DEF_H_RESHZ;

	phydev->device_spec->cur_profile = 0;

	/* Set up the aux state */
	auxptr = malloc(sizeof(ubertooth_u1_aux));
	phydev->auxptr = auxptr;

	auxptr->configured = 0;
	auxptr->primed = 0;

	auxptr->dev = dev;
	auxptr->devhdl = NULL;
	auxptr->phydev = phydev;
	auxptr->sockpair[0] = -1;
	auxptr->sockpair[1] = -1;

	/* Will be filled in by setposition later */
	auxptr->sweepbuf_initialized = 0;
	auxptr->sweepbuf = NULL;
	auxptr->full_sweepbuf = NULL;

	auxptr->peak_cache = spectool_cache_alloc(UBERTOOTH_U1_AVG_SAMPLES, 1, 0);

	phydev->open_func = &ubertooth_u1_open;
	phydev->close_func = &ubertooth_u1_close;
	phydev->poll_func = &ubertooth_u1_poll;
	phydev->pollfd_func = &ubertooth_u1_getpollfd;
	phydev->setcalib_func = &ubertooth_u1_setcalibration;
	phydev->getsweep_func = &ubertooth_u1_getsweep;
	phydev->setposition_func = &ubertooth_u1_setposition;

	phydev->draw_agg_suggestion = 1;

	return 0;
}

void *ubertooth_u1_servicethread(void *aux) {
	ubertooth_u1_aux *auxptr = (ubertooth_u1_aux *) aux;

	int sock;
	struct usb_device *dev;
	struct usb_dev_handle *u1;

	char buf[64];
	int x = 0, error = 0;
	fd_set wset;

	struct timeval tm;

	sigset_t signal_set;

	error = 0;

	sock = auxptr->sockpair[1];

	dev = auxptr->dev;
	u1 = auxptr->devhdl;

	/* We don't want to see any signals in the child thread */
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	while (1) {
		/* wait until we're able to write out to the IPC socket, go into a blocking
		 * select */
		FD_ZERO(&wset);
		FD_SET(sock, &wset);

		if (select(sock + 1, NULL, &wset, NULL, NULL) < 0) {
			snprintf(auxptr->phydev->errstr, SPECTOOL_ERROR_MAX,
					 "ubertooth_u1 poller failed on IPC write select(): %s",
					 strerror(errno));
			auxptr->usb_thread_alive = 0;
			auxptr->phydev->state = SPECTOOL_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (auxptr->usb_thread_alive == 0) {
			auxptr->phydev->state = SPECTOOL_STATE_ERROR;
			pthread_exit(NULL);
		}

		if (FD_ISSET(sock, &wset) == 0)
			continue;

		/* Get new data only if we haven't requeued */
		if (error == 0) {
			memset(buf, 0, 64);

			if (usb_bulk_read(u1, 0x82, buf, 64, TIMEOUT) <= 0) {
				if (errno == EAGAIN)
					continue;

				snprintf(auxptr->phydev->errstr, SPECTOOL_ERROR_MAX,
						 "ubertooth_u1 poller failed to read USB data: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = SPECTOOL_STATE_ERROR;
				pthread_exit(NULL);
			}

			/* Send it to the IPC remote, re-queue on enobufs */
			if (send(sock, buf, 64, 0) < 0) {
				if (errno == ENOBUFS) {
					error = 1;
					continue;
				}

				snprintf(auxptr->phydev->errstr, SPECTOOL_ERROR_MAX,
						 "ubertooth_u1 poller failed on IPC send: %s",
						 strerror(errno));
				auxptr->usb_thread_alive = 0;
				auxptr->phydev->state = SPECTOOL_STATE_ERROR;
				pthread_exit(NULL);
			}

		}

		error = 0;
	}

	auxptr->usb_thread_alive = 0;
	send(sock, buf, 64, 0);
	auxptr->phydev->state = SPECTOOL_STATE_ERROR;
	pthread_exit(NULL);
}

int ubertooth_u1_getpollfd(spectool_phy *phydev) {
	ubertooth_u1_aux *auxptr = (ubertooth_u1_aux *) phydev->auxptr;

	if (auxptr->usb_thread_alive == 0) {
		ubertooth_u1_close(phydev);
		return -1;
	}

	return auxptr->sockpair[0];
}

int ubertooth_u1_open(spectool_phy *phydev) {
	int pid_status;
	ubertooth_u1_aux *auxptr = (ubertooth_u1_aux *) phydev->auxptr;

	/* Make the client/server socketpair */
	if (socketpair(PF_UNIX, SOCK_DGRAM, 0, auxptr->sockpair) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "ubertooth_u1 open failed to create socket pair for capture "
				 "process: %s", strerror(errno));
		return -1;
	}

	if ((auxptr->devhdl = usb_open(auxptr->dev)) == NULL) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "ubertooth_u1 capture process failed to open USB device: %s",
				 strerror(errno));
		return -1;
	}

#ifdef LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP
	// fprintf(stderr, "debug - detatch kernel driver np\n");
	if (usb_detach_kernel_driver_np(auxptr->devhdl, 0)) {
		// fprintf(stderr, "Could not detach kernel driver %s\n", usb_strerror());
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "Could not detach device from kernel driver: %s",
				 usb_strerror());
	}
#endif

	// fprintf(stderr, "debug - set_configuration\n");
	usb_set_configuration(auxptr->devhdl, 1);

	// fprintf(stderr, "debug - claiming interface\n");
	if (usb_claim_interface(auxptr->devhdl, 0) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "could not claim interface: %s", usb_strerror());
	}

	auxptr->usb_thread_alive = 1;

	auxptr->last_read = time(0);

	if (pthread_create(&(auxptr->usb_thread), NULL, 
					   ubertooth_u1_servicethread, auxptr) < 0) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "ubertooth_u1 capture failed to create thread: %s",
				 strerror(errno));
		auxptr->usb_thread_alive = 0;
		return -1;
	}

	/* Update the state */
	phydev->state = SPECTOOL_STATE_CONFIGURING;

	/*
	if (ubertooth_u1_setposition(phydev, 0, 0, 0) < 0)
		return -1;
	*/

	return 1;
}

int ubertooth_u1_close(spectool_phy *phydev) {
	ubertooth_u1_aux *aux;
	
	if (phydev == NULL)
		return 0;

	aux = (ubertooth_u1_aux *) phydev->auxptr;

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

spectool_sample_sweep *ubertooth_u1_getsweep(spectool_phy *phydev) {
	ubertooth_u1_aux *auxptr = (ubertooth_u1_aux *) phydev->auxptr;

	// return auxptr->full_sweepbuf;
	return auxptr->peak_cache->roll_peak;
}

void ubertooth_u1_setcalibration(spectool_phy *phydev, int in_calib) {
	phydev->state = SPECTOOL_STATE_RUNNING;
}

int ubertooth_u1_poll(spectool_phy *phydev) {
	ubertooth_u1_aux *auxptr = (ubertooth_u1_aux *) phydev->auxptr;
	char lbuf[64];
	int x, freq, ret, full = 0, rssi;
	ubertooth_u1_report *report = (ubertooth_u1_report *) lbuf;

	/* Push a configure event before anything else */
	if (auxptr->configured == 0) {
		auxptr->configured = 1;
		return SPECTOOL_POLL_CONFIGURED;
	}

	/* Use the error set by the polling thread */
	if (auxptr->usb_thread_alive == 0) {
		phydev->state = SPECTOOL_STATE_ERROR;
		ubertooth_u1_close(phydev);
		return SPECTOOL_POLL_ERROR;
	}

	if ((ret = recv(auxptr->sockpair[0], lbuf, 64, 0)) < 0) {
		if (auxptr->usb_thread_alive != 0)
			snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
					 "ubertooth_u1 IPC receiver failed to read signal data: %s",
					 strerror(errno));
		phydev->state = SPECTOOL_STATE_ERROR;
		return SPECTOOL_POLL_ERROR;
	}

	if (time(0) - auxptr->last_read > 3) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "ubertooth_u1 didn't see any data for more than 3 seconds, "
				 "something has gone wrong (was the device removed?)");
		phydev->state = SPECTOOL_STATE_ERROR;
		return SPECTOOL_POLL_ERROR;
	}

	if (ret > 0)
		auxptr->last_read = time(0);

	// If we don't have a sweepbuf we're not configured, barf
	if (auxptr->sweepbuf == NULL) {
		return SPECTOOL_POLL_NONE;
	}

	// If we're full entering a read we need to wipe out
	if (auxptr->peak_cache->num_used >= UBERTOOTH_U1_AVG_SAMPLES) {
		// spectool_cache_clear(auxptr->peak_cache);
		// printf("debug - clearing peak cache\n");
	}

	for (x = 0; x < 16; x++) {
		// printf("%u %d\n", endian_swap16(report->data[x].be_freq), report->data[x].rssi);
#ifdef WORDS_BIGENDIAN
		freq = report->data[x].be_freq;
#else
		freq = endian_swap16(report->data[x].be_freq);
#endif

		freq = freq - (auxptr->sweepbuf->start_khz / 1000);

		rssi = (report->data[x].rssi + 55);

		// printf("%u = %d ", freq, rssi);

		if (freq < 0 || freq >= auxptr->sweepbuf->num_samples) {
			printf("debug - sample freq %d not in range\n", freq);
			continue;
		}

		auxptr->sweepbuf->sample_data[freq] = rssi;

		if (rssi < phydev->min_rssi_seen)
			phydev->min_rssi_seen = rssi;

		if (freq == 0) {
			if (auxptr->primed == 0) {
				// printf("debug - u1 primed\n");
				auxptr->primed = 1;
				continue;
			}

			auxptr->sweepbuf_initialized = 1;
			auxptr->num_sweeps++;

			gettimeofday(&(auxptr->sweepbuf->tm_end), NULL);
			auxptr->sweepbuf->min_rssi_seen = phydev->min_rssi_seen;

			/*
			if (auxptr->full_sweepbuf != NULL) {
				free(auxptr->full_sweepbuf);
			}

			auxptr->full_sweepbuf = auxptr->sweepbuf;
			*/

			// auxptr->sweepbuf = ubertooth_u1_build_sweepbuf(phydev);

			spectool_cache_append(auxptr->peak_cache, auxptr->sweepbuf);

			if (auxptr->peak_cache->num_used >= UBERTOOTH_U1_AVG_SAMPLES) {
				full = 1;
			}

			gettimeofday(&(auxptr->sweepbuf->tm_start), NULL);

			// printf("debug - u1 - sweep complete, freq %d\n", freq);
		}
	}

	if (full == 1) {
		// printf("debug - returning sc\n");
		return SPECTOOL_POLL_SWEEPCOMPLETE;
	}


#if 0
	/*

	/* Initialize the sweep buffer when we get to it 
	 * If we haven't gotten around to a 0 state to initialize the buffer, we throw
	 * out the sample data until we do. */
	if (base == 0) {
		auxptr->sweepbuf_initialized = 1;
		auxptr->num_sweeps++;

		/* Init the timestamp for sweep begin */
		gettimeofday(&(auxptr->sweepbuf->tm_start), NULL);
	} else if (auxptr->sweepbuf_initialized == 0) {
		return SPECTOOL_POLL_NONE;
	}

	for (x = 0; x < report->valid_bytes; x++) {
		if (base + x >= auxptr->sweepbuf->num_samples) {
			break;
		}

		/*
		auxptr->sweepbuf->sample_data[base + x] =
			ubertooth_u1_RSSI(report->data[x]);
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
		return SPECTOOL_POLL_SWEEPCOMPLETE;
	}
#endif

	return SPECTOOL_POLL_NONE;
}

spectool_sample_sweep *ubertooth_u1_build_sweepbuf(spectool_phy *phydev) {
	spectool_sample_sweep *r;

	r = (spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(UBERTOOTH_U1_NUM_SAMPLES));
	r->phydev = phydev;
	r->start_khz = 
		phydev->device_spec->supported_ranges[0].start_khz;
	r->end_khz = 
		phydev->device_spec->supported_ranges[0].end_khz;
	r->res_hz = 
		phydev->device_spec->supported_ranges[0].res_hz;
	r->num_samples = 
		phydev->device_spec->supported_ranges[0].num_samples;

	r->amp_offset_mdbm =
		phydev->device_spec->supported_ranges[0].amp_offset_mdbm;
	r->amp_res_mdbm =
		phydev->device_spec->supported_ranges[0].amp_res_mdbm;
	r->rssi_max =
		phydev->device_spec->supported_ranges[0].rssi_max;

	return r;
}

int ubertooth_u1_setposition(spectool_phy *phydev, int in_profile, 
							 int start_khz, int res_hz) {
	int temp_d, temp_m;
	int best_s_m = 0, best_s_e = 0, best_b_m = 0, best_b_e = 0;
	int m = 0, e = 0, best_d;
	int target_bw;
	struct usb_dev_handle *u1;
	ubertooth_u1_aux *auxptr = (ubertooth_u1_aux *) phydev->auxptr;


	u1 = auxptr->devhdl;

	// printf("debug - writing control msg\n");

	if (usb_control_msg(u1, 
						0x40, 27, 2402, 2480, NULL, 0, TIMEOUT)) {
		snprintf(phydev->errstr, SPECTOOL_ERROR_MAX,
				 "ubertooth_u1 setposition failed to set sweep feature set: %s",
				 strerror(errno));
		phydev->state = SPECTOOL_STATE_ERROR;
		return -1;
	}

	/* If we successfully configured the hardware, update the sweep capabilities and
	 * the sweep buffer and reset the device */

	phydev->device_spec->num_sweep_ranges = 1;
	if (phydev->device_spec->supported_ranges)
		free(phydev->device_spec->supported_ranges);
	phydev->device_spec->supported_ranges = 
		(spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(0));
	memset (phydev->device_spec->supported_ranges, 0, SPECTOOL_SWEEP_SIZE(0));

	/* Set the universal values */
	phydev->device_spec->supported_ranges[0].num_samples = UBERTOOTH_U1_NUM_SAMPLES;

	phydev->device_spec->supported_ranges[0].amp_offset_mdbm = UBERTOOTH_U1_OFFSET_MDBM;
	phydev->device_spec->supported_ranges[0].amp_res_mdbm = UBERTOOTH_U1_RES_MDBM;
	phydev->device_spec->supported_ranges[0].rssi_max = UBERTOOTH_U1_RSSI_MAX;

	/* Set the sweep records based on default or new data */
	phydev->device_spec->supported_ranges[0].start_khz = UBERTOOTH_U1_DEF_H_MINKHZ;
	phydev->device_spec->supported_ranges[0].end_khz = 
		UBERTOOTH_U1_DEF_H_MINKHZ + ((UBERTOOTH_U1_NUM_SAMPLES *
									  UBERTOOTH_U1_DEF_H_RESHZ) / 1000);
	phydev->device_spec->supported_ranges[0].res_hz = UBERTOOTH_U1_DEF_H_RESHZ;

	/* We're not configured, so we need to push a new configure block out next time
	 * we sweep */
	auxptr->configured = 0;

	/* Rebuild the sweep buffer */
	if (auxptr->sweepbuf)
		free(auxptr->sweepbuf);

	auxptr->sweepbuf = ubertooth_u1_build_sweepbuf(phydev);

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

