/*
 * Generic spectrum tool container class for physical and logic devices,
 * sample sweeps, and aggregations of sample sweeps
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

#ifndef __WISPY_DEVCONTAINER_H__
#define __WISPY_DEVCONTAINER_H__

#include <time.h>
#include <sys/time.h>

#ifdef HAVE_STDINT
#include <stdint.h>
#endif

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/* A sweep record.  Because the sample array is allocated dynamically we 
 * re-use this same record as the definition of what sweep ranges a device
 * can handle.
 */
typedef struct _wispy_sample_sweep {
	/* Starting frequency of the sweep, in KHz */
	uint32_t start_khz;
	/* Ending frequency of the sweep, in KHz */
	uint32_t end_khz;
	/* Sample resolution, in KHz */
	uint32_t res_hz;

	/* RSSI conversion information in mdbm
	 * db = (rssi * (amp_res_mdbm / 1000)) - (amp_offset_mdbm / 1000) */
	int amp_offset_mdbm;
	unsigned int amp_res_mdbm;
	unsigned int rssi_max;

	/* Lowest RSSI seen by the device */
	unsigned int min_rssi_seen;

	/* This could be derived from start, end, and resolution, but we include
	 * it here to save on math */
	unsigned int num_samples;

	/* Timestamp for when the sweep begins and ends */
	struct timeval tm_start;
	struct timeval tm_end;

	/* Actual sample data.  This is num_samples of uint8_t RSSI */
	uint8_t sample_data[0];
} wispy_sample_sweep;

#define WISPY_RSSI_CONVERT(O,R,D)	(int) ((D) * ((double) (R) / 1000.0f) + \
										   ((double) (O) / 1000.0f))

#define WISPY_SWEEP_SIZE(y)		(sizeof(wispy_sample_sweep) + (y))

/* Sweep record for aggregating multiple sweep points */
typedef struct _wispy_sweep_cache {
	wispy_sample_sweep **sweeplist;
	wispy_sample_sweep *avg;
	wispy_sample_sweep *peak;
	wispy_sample_sweep *latest;
	int num_alloc, pos;
	int calc_peak, calc_avg;
	uint32_t device_id;
} wispy_sweep_cache;

/* Allocate and manipulate sweep caches */
wispy_sweep_cache *wispy_cache_alloc(int nsweeps, int calc_peak, int calc_avg);
void wispy_cache_append(wispy_sweep_cache *c, wispy_sample_sweep *s);
void wispy_cache_clear(wispy_sweep_cache *c);
void wispy_cache_free(wispy_sweep_cache *c);

#define WISPY_ERROR_MAX			512
#define WISPY_PHY_NAME_MAX		256
typedef struct _wispy_dev_spec {
	/* A unique ID fetched from the firmware (in the future) or extracted from the
	 * USB bus (currently) */
	uint32_t device_id;

	/* User-specified name */
	char device_name[WISPY_PHY_NAME_MAX];

	/* Version of the physical source device.
	 * 0x01 WiSPY generation 1 USB dongle
	 * 0x02 WiSPY generation 2 USB dongle
	 */
	uint8_t device_version;

	/* Device flags */
	uint8_t device_flags;

	wispy_sample_sweep default_range;

	/* Number of sweep ranges this device supports. 
	 * Gen1 supports 1 range.
	 */
	unsigned int num_sweep_ranges;

	/* Supported sweep ranges */
	wispy_sample_sweep *supported_ranges;
} wispy_dev_spec;

/* Device flags */
#define WISPY_DEV_FL_NONE			0
/* Variable sweep supported */
#define WISPY_DEV_FL_VAR_SWEEP		1

#define WISPY_DEV_SIZE(y)		(sizeof(wispy_dev_spec))

/* Central tracking structure for wispy device data and API callbacks */
typedef struct _wispy_phy {
	/* Phy capabilities */
	wispy_dev_spec *device_spec;

	/* Running state */
	int state;

	/* Min RSSI seen */
	unsigned int min_rssi_seen;

	/* External phy-specific data */
	void *auxptr;

	/* Function pointers to be filled in by the device init system */
	int (*open_func)(struct _wispy_phy *);
	int (*close_func)(struct _wispy_phy *);
	int (*poll_func)(struct _wispy_phy *);
	int (*pollfd_func)(struct _wispy_phy *);
	void (*setcalib_func)(struct _wispy_phy *, int);
	int (*setposition_func)(struct _wispy_phy *, int, int);
	wispy_sample_sweep *(*getsweep_func)(struct _wispy_phy *);

	char errstr[WISPY_ERROR_MAX];

	/* Linked list elements incase we need them in our implementation */
	struct _wispy_phy *next;
} wispy_phy;

#define WISPY_PHY_SIZE		(sizeof(wispy_phy))

int wispy_get_state(wispy_phy *phydev);
char *wispy_get_error(wispy_phy *phydev);
int wispy_phy_open(wispy_phy *phydev);
int wispy_phy_close(wispy_phy *phydev);
int wispy_phy_poll(wispy_phy *phydev);
int wispy_phy_getpollfd(wispy_phy *phydev);
wispy_sample_sweep *wispy_phy_getsweep(wispy_phy *phydev);
void wispy_phy_setcalibration(wispy_phy *phydev, int enable);
int wispy_phy_setposition(wispy_phy *phydev, int start_khz, int res_hz);
char *wispy_phy_getname(wispy_phy *phydev);
void wispy_phy_setname(wispy_phy *phydev, char *name);
int wispy_phy_getdevid(wispy_phy *phydev);
int wispy_phy_get_flags(wispy_phy *phydev);

/* Running states */
#define WISPY_STATE_CLOSED			0
#define WISPY_STATE_CONFIGURING		1
#define WISPY_STATE_CALIBRATING		2
#define WISPY_STATE_RUNNING			3
#define WISPY_STATE_ERROR			255

/* Poll return states */
/* Failure */
#define WISPY_POLL_ERROR			256
/* No state - partial poll, etc */
#define WISPY_POLL_NONE				1
/* Sweep is complete, caller should pull data */
#define WISPY_POLL_SWEEPCOMPLETE	2
/* Device has finished configuring */
#define WISPY_POLL_CONFIGURED		4
/* Device has additional pending data and poll should be called again */
#define WISPY_POLL_ADDITIONAL		8

/* Device scan handling */
typedef struct _wispy_device_rec {
	/* Name of device */
	char name[WISPY_PHY_NAME_MAX];
	/* ID of device */
	uint32_t device_id;
	/* Init function */
	int (*init_func)(struct _wispy_phy *, struct _wispy_device_rec *);
	/* Hardware record pointing to the aux handling */
	void *hw_rec;
} wispy_device_rec;

typedef struct _wispy_device_list {
	int num_devs;
	int max_devs;
	wispy_device_rec *list;
} wispy_device_list;

/* Hopefully this doesn't come back and bite us, but, really, 32 SAs on one system? */
#define MAX_SCAN_RESULT		32

/* Scan for all attached devices we can handle */
void wispy_device_scan_init(wispy_device_list *list);
int wispy_device_scan(wispy_device_list *list);
void wispy_device_scan_free(wispy_device_list *list);
int wispy_device_init(wispy_phy *phydev, wispy_device_rec *rec);

struct wispy_channels {
	/* Name of the channel set */
	char *name;
	/* Start and end khz for matching */
	int startkhz;
	int endkhz;
	/* Number of channels */
	int chan_num;
	/* Offsets in khz */
	int *chan_freqs;
	/* Width of channels in khz */
	int chan_width;
	/* Text of channel numbers */
	char **chan_text;
};

/* Some channel lists */
static int chan_freqs_24[] = { 
	2411000, 2416000, 2421000, 2426000, 2431000, 2436000, 2441000,
	2446000, 2451000, 2456000, 2461000, 2466000, 2471000, 2483000 
};

static char *chan_text_24[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14"
};

/* Allocate all our channels in a big nasty array */
static struct wispy_channels channel_list[] = {
	{ "802.11b/g", 2400000, 2483000, 14, chan_freqs_24, 22000, chan_text_24 },
	{ NULL, 0, 0, 0, NULL, 0, NULL }
};

#endif

