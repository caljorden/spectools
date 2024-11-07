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

#ifndef __SPECTOOL_DEVCONTAINER_H__
#define __SPECTOOL_DEVCONTAINER_H__

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
typedef struct _spectool_sample_sweep {
	/* Name of sweep (if used as a range marker */
	char *name;

	/* Starting frequency of the sweep, in KHz */
	uint32_t start_khz;
	/* Ending frequency of the sweep, in KHz */
	uint32_t end_khz;
	/* Sample resolution, in KHz */
	uint32_t res_hz;

	/* RSSI conversion information in mdbm
	 * db = (rssi * (amp_res_mdbm / 1000)) - (amp_offset_mdbm / 1000) */
	int amp_offset_mdbm;
	int amp_res_mdbm;
	unsigned int rssi_max;

	/* Lowest RSSI seen by the device */
	unsigned int min_rssi_seen;

	/* This could be derived from start, end, and resolution, but we include
	 * it here to save on math */
	unsigned int num_samples;
	/* Filter resolution in hz, hw config */
	unsigned int filter_bw_hz;

	/* Samples per point (hw aggregation) */
	unsigned int samples_per_point;

	/* Timestamp for when the sweep begins and ends */
	struct timeval tm_start;
	struct timeval tm_end;

	/* Phy reference */
	void *phydev;

	/* Actual sample data.  This is num_samples of uint8_t RSSI */
	uint8_t sample_data[0];
} spectool_sample_sweep;

#define SPECTOOL_RSSI_CONVERT(O,R,D)	(int) ((D) * ((double) (R) / 1000.0f) + \
										   ((double) (O) / 1000.0f))

#define SPECTOOL_SWEEP_SIZE(y)		(sizeof(spectool_sample_sweep) + (y))

/* Sweep record for aggregating multiple sweep points */
typedef struct _spectool_sweep_cache {
	spectool_sample_sweep **sweeplist;
	spectool_sample_sweep *avg;
	spectool_sample_sweep *peak;
	spectool_sample_sweep *roll_peak;
	spectool_sample_sweep *latest;
	int num_alloc, pos, looped;
	int calc_peak, calc_avg;
	int num_used;
	uint32_t device_id;
} spectool_sweep_cache;

typedef struct _spectool_sweep_cache_itr {
	int pos_start;
	int pos_cur;
	int looped_start;
} spectool_sweep_cache_itr;

/* Allocate and manipulate sweep caches */
spectool_sweep_cache *spectool_cache_alloc(int nsweeps, int calc_peak, int calc_avg);
void spectool_cache_append(spectool_sweep_cache *c, spectool_sample_sweep *s);
void spectool_cache_clear(spectool_sweep_cache *c);
void spectool_cache_free(spectool_sweep_cache *c);

void spectool_cache_itr_init(spectool_sweep_cache *c, spectool_sweep_cache_itr *i);
spectool_sample_sweep *spectool_cache_itr_next(spectool_sweep_cache *c, spectool_sweep_cache_itr *i);

#define SPECTOOL_ERROR_MAX			512
#define SPECTOOL_PHY_NAME_MAX		256
typedef struct _spectool_dev_spec {
	/* A unique ID fetched from the firmware (in the future) or extracted from the
	 * USB bus (currently) */
	uint32_t device_id;

	/* User-specified name */
	char device_name[SPECTOOL_PHY_NAME_MAX];

	/* Version of the physical source device.
	 * 0x01 WiSPY generation 1 USB device
	 * 0x02 WiSPY generation 2 USB device
	 * 0x03 WiSPY generation 3 USB device
	 */
	uint8_t device_version;

	/* Device flags */
	uint8_t device_flags;

	spectool_sample_sweep *default_range;

	/* Number of sweep ranges this device supports. 
	 * Gen1 supports 1 range.
	 */
	unsigned int num_sweep_ranges;

	/* Supported sweep ranges */
	spectool_sample_sweep *supported_ranges;

	int cur_profile;
} spectool_dev_spec;

/* Device flags */
#define SPECTOOL_DEV_FL_NONE			0
/* Variable sweep supported */
#define SPECTOOL_DEV_FL_VAR_SWEEP		1

#define SPECTOOL_DEV_SIZE(y)		(sizeof(spectool_dev_spec))

/* Central tracking structure for spectool device data and API callbacks */
typedef struct _spectool_phy {
	/* Phy capabilities */
	spectool_dev_spec *device_spec;

	/* Running state */
	int state;

	/* Min RSSI seen */
	unsigned int min_rssi_seen;

	/* External phy-specific data */
	void *auxptr;

	/* Function pointers to be filled in by the device init system */
	int (*open_func)(struct _spectool_phy *);
	int (*close_func)(struct _spectool_phy *);
	int (*poll_func)(struct _spectool_phy *);
	int (*pollfd_func)(struct _spectool_phy *);
	void (*setcalib_func)(struct _spectool_phy *, int);
	int (*setposition_func)(struct _spectool_phy *, int, int, int);
	spectool_sample_sweep *(*getsweep_func)(struct _spectool_phy *);

	char errstr[SPECTOOL_ERROR_MAX];

	/* Linked list elements incase we need them in our implementation */
	struct _spectool_phy *next;

	/* Suggested delay for drawing */
	int draw_agg_suggestion;
} spectool_phy;

#define SPECTOOL_PHY_SIZE		(sizeof(spectool_phy))

int spectool_get_state(spectool_phy *phydev);
char *spectool_get_error(spectool_phy *phydev);
int spectool_phy_open(spectool_phy *phydev);
int spectool_phy_close(spectool_phy *phydev);
int spectool_phy_poll(spectool_phy *phydev);
int spectool_phy_getpollfd(spectool_phy *phydev);
spectool_sample_sweep *spectool_phy_getsweep(spectool_phy *phydev);
void spectool_phy_setcalibration(spectool_phy *phydev, int enable);
int spectool_phy_setposition(spectool_phy *phydev, int in_profile, 
						  int start_khz, int res_hz);
char *spectool_phy_getname(spectool_phy *phydev);
void spectool_phy_setname(spectool_phy *phydev, char *name);
int spectool_phy_getdevid(spectool_phy *phydev);
int spectool_phy_get_flags(spectool_phy *phydev);
spectool_sample_sweep *spectool_phy_getcurprofile(spectool_phy *phydev);

/* Running states */
#define SPECTOOL_STATE_CLOSED			0
#define SPECTOOL_STATE_CONFIGURING		1
#define SPECTOOL_STATE_CALIBRATING		2
#define SPECTOOL_STATE_RUNNING			3
#define SPECTOOL_STATE_ERROR			255

/* Poll return states */
/* Failure */
#define SPECTOOL_POLL_ERROR			256
/* No state - partial poll, etc */
#define SPECTOOL_POLL_NONE				1
/* Sweep is complete, caller should pull data */
#define SPECTOOL_POLL_SWEEPCOMPLETE	2
/* Device has finished configuring */
#define SPECTOOL_POLL_CONFIGURED		4
/* Device has additional pending data and poll should be called again */
#define SPECTOOL_POLL_ADDITIONAL		8

/* Device scan handling */
typedef struct _spectool_device_rec {
	/* Name of device */
	char name[SPECTOOL_PHY_NAME_MAX];
	/* ID of device */
	uint32_t device_id;
	/* Init function */
	int (*init_func)(struct _spectool_phy *, struct _spectool_device_rec *);
	/* Hardware record pointing to the aux handling */
	void *hw_rec;

	/* Supported sweep ranges identified from hw type */
	unsigned int num_sweep_ranges;
	spectool_sample_sweep *supported_ranges;
} spectool_device_rec;

typedef struct _spectool_device_list {
	int num_devs;
	int max_devs;
	spectool_device_rec *list;
} spectool_device_list;

/* Hopefully this doesn't come back and bite us, but, really, 32 SAs on one system? */
#define MAX_SCAN_RESULT		32

/* Scan for all attached devices we can handle */
void spectool_device_scan_init(spectool_device_list *list);
int spectool_device_scan(spectool_device_list *list);
void spectool_device_scan_free(spectool_device_list *list);
int spectool_device_init(spectool_phy *phydev, spectool_device_rec *rec);

struct spectool_channels {
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

static int chan_freqs_5[] = {
	5180000, 5200000, 5220000, 5240000, 5260000, 5280000, 5300000, 5320000,
	5500000, 5520000, 5540000, 5560000, 5580000, 5600000, 5620000, 5640000,
	5660000, 5680000, 5700000, 5745000, 5765000, 5785000, 5805000, 5825000
};

static char *chan_text_5[] = {
	"36", "40", "44", "48", "52", "56", "60", "64", "100", "104", 
	"108", "112", "116", "120", "124", "128", "132", "136", "140", 
	"149", "153", "157", "161", "165"
};

static int chan_freqs_5_low[] = {
	5180000, 5200000, 5220000, 5240000, 5260000, 5280000, 5300000, 5320000
};

static char *chan_text_5_low[] = {
	"36", "40", "44", "48", "52", "56", "60", "64"
};

static int chan_freqs_5_mid[] = {
	5500000, 5520000, 5540000, 5560000, 5580000, 5600000, 5620000, 5640000,
	5660000, 5680000, 5700000
};

static char *chan_text_5_mid[] = {
	"100", "104", "108", "112", "116", "120", "124", "128", "132", "136", "140"
};

static int chan_freqs_5_high[] = {
	5745000, 5765000, 5785000, 5805000, 5825000
};

static char *chan_text_5_high[] = {
	"149", "153", "157", "161", "165"
};


static int chan_freqs_900[] = {
	905000, 910000, 915000, 920000, 925000
};

static char *chan_text_900[] = {
	"905", "910", "915", "920", "925"
};

/* Allocate all our channels in a big nasty array */
static struct spectool_channels channel_list[] = {
	{ "802.11b/g", 2400000, 2483000, 14, chan_freqs_24, 22000, chan_text_24 },
	{ "802.11b/g", 2402000, 2480000, 14, chan_freqs_24, 22000, chan_text_24 },
	{ "802.11a", 5170000, 5835000, 24, chan_freqs_5, 20000, chan_text_5 },
	{ "802.11a Low", 5170000, 5330000, 8, chan_freqs_5_low, 20000, chan_text_5_low },
	{ "802.11a Medium", 5490000, 5710000, 11, chan_freqs_5_mid, 20000, chan_text_5_mid },
	{ "802.11a High", 5735000, 5835000, 5, chan_freqs_5_high, 20000, chan_text_5_high },
	{ "900 ISM", 902000, 927000, 5, chan_freqs_900, 5000, chan_text_900 },
	{ NULL, 0, 0, 0, NULL, 0, NULL }
};


#endif

