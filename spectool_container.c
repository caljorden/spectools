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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "spectool_container.h"
#include "wispy_hw_gen1.h"
#include "wispy_hw_24x.h"
#include "wispy_hw_dbx.h"
#include "ubertooth_hw_u1.h"

int spectool_get_state(spectool_phy *phydev) {
	return phydev->state;
}

char *spectool_get_error(spectool_phy *phydev) {
	return phydev->errstr;
}

char *spectool_phy_getname(spectool_phy *phydev) {
	if (phydev->device_spec == NULL)
		return NULL;

	return phydev->device_spec->device_name;
}

int spectool_phy_getdevid(spectool_phy *phydev) {
	if (phydev->device_spec == NULL)
		return 0;

	return phydev->device_spec->device_id;
}

void spectool_phy_setname(spectool_phy *phydev, char *name) {
	if (phydev->device_spec == NULL)
		return;

	snprintf(phydev->device_spec->device_name, SPECTOOL_PHY_NAME_MAX, "%s", name);
}

int spectool_phy_open(spectool_phy *phydev) {
	if (phydev->open_func == NULL)
		return 0;

	return (*(phydev->open_func))(phydev);
}

int spectool_phy_close(spectool_phy *phydev) {
	if (phydev->close_func == NULL)
		return 0;

	return (*(phydev->close_func))(phydev);
}

int spectool_phy_poll(spectool_phy *phydev) {
	if (phydev->poll_func == NULL)
		return SPECTOOL_POLL_ERROR;

	return (*(phydev->poll_func))(phydev);
}

int spectool_phy_getpollfd(spectool_phy *phydev) {
	if (phydev == NULL)
		return -1;

	if (phydev->pollfd_func == NULL)
		return -1;

	return (*(phydev->pollfd_func))(phydev);
}

void spectool_phy_setcalibration(spectool_phy *phydev, int enable) {
	if (phydev->setcalib_func != NULL)
		return (*(phydev->setcalib_func))(phydev, enable);
}

int spectool_phy_getflags(spectool_phy *phydev) {
	return phydev->device_spec->device_flags;
}

int spectool_phy_setposition(spectool_phy *phydev, int in_profile, 
						  int start_khz, int res_hz) {
	if (phydev->setposition_func != NULL)
		return (*(phydev->setposition_func))(phydev, in_profile, start_khz, res_hz);

	snprintf(phydev->errstr, SPECTOOL_ERROR_MAX, "Device does not support setting "
			 "scan position or resolution");
	return -1;
}

spectool_sample_sweep *spectool_phy_getsweep(spectool_phy *phydev) {
	if (phydev->getsweep_func == NULL)
		return NULL;

	return (*(phydev->getsweep_func))(phydev);
}

spectool_sweep_cache *spectool_cache_alloc(int nsweeps, int calc_peak, int calc_avg) {
	int x;
	spectool_sweep_cache *c = (spectool_sweep_cache *) malloc(sizeof(spectool_sweep_cache));

	c->sweeplist = 
		(spectool_sample_sweep **) malloc(sizeof(spectool_sample_sweep *) * nsweeps);
	c->avg = NULL;
	c->peak = NULL;
	c->roll_peak = NULL;
	c->latest = NULL;

	for (x = 0; x < nsweeps; x++) {
		c->sweeplist[x] = NULL;
	}

	c->num_alloc = nsweeps;
	c->pos = -1;
	c->looped = 0;
	c->num_used = 0;

	c->calc_peak = calc_peak;
	c->calc_avg = calc_avg;

	return c;
}

void spectool_cache_free(spectool_sweep_cache *c) {
	if (c->avg != NULL)
		free(c->avg);
	if (c->peak != NULL)
		free(c->peak);
	free(c->sweeplist);
}

void spectool_cache_clear(spectool_sweep_cache *c) {
	if (c->avg != NULL)
		free(c->avg);
	c->avg = NULL;

	if (c->peak != NULL)
		free(c->peak);
	c->peak = NULL;

	if (c->roll_peak != NULL)
		free(c->roll_peak);
	c->roll_peak = NULL;

	c->pos = 0;
	c->looped = 0;
	c->num_used = 0;
}

void spectool_cache_append(spectool_sweep_cache *c, spectool_sample_sweep *s) {
	int x, y, sum = 0;
	int *avgdata, *avgsum;
	int navg;
	int nsampled;

	/* Make sure we don't overflow and crash, should be sufficient
	 * to make sure that the new sweep is reasonable */
	if (c->avg != NULL && c->avg->num_samples != s->num_samples) 
		return;

	if (c->pos == (c->num_alloc - 1)) {
		c->looped = 1;
		c->pos = 0;
	} else {
		c->pos++;
		c->num_used++;
	}

	if (c->sweeplist[c->pos] != NULL) {
		free(c->sweeplist[c->pos]);
	}

	if (c->looped)
		nsampled = c->num_alloc;
	else
		nsampled = c->pos;

	c->sweeplist[c->pos] = 
		(spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(s->num_samples));

	memcpy(c->sweeplist[c->pos], s, SPECTOOL_SWEEP_SIZE(s->num_samples));

	c->latest = c->sweeplist[c->pos];

	if (c->avg == NULL && c->calc_avg) {
		c->avg = (spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(s->num_samples));
		memcpy(c->avg, s, SPECTOOL_SWEEP_SIZE(s->num_samples));
	} else if (c->calc_avg) {
		/* Reset average times */
		c->avg->tm_start.tv_sec = 0;
		c->avg->tm_start.tv_usec = 0;
		c->avg->tm_end.tv_sec = 0;
		c->avg->tm_end.tv_usec = 0;

		/* Allocate a large int for summing */
		avgdata = (int *) malloc(sizeof(int) * c->avg->num_samples);
		avgsum = (int *) malloc(sizeof(int) * c->avg->num_samples);
		for (x = 0; x < c->avg->num_samples; x++) {
			avgdata[x] = 0;
			avgsum[x] = 0;
		}

		/* Sum them up */

		for (x = 0; x < nsampled; x++) {
			if (c->sweeplist[x] == NULL)
				continue;

			for (y = 0; y < c->avg->num_samples; y++) {
				avgdata[y] += c->sweeplist[x]->sample_data[y];
				avgsum[y]++;
			}

			/* Update the time of the average network */
			if (c->sweeplist[x]->tm_start.tv_sec > c->avg->tm_start.tv_sec) {
				c->avg->tm_start.tv_sec = c->sweeplist[x]->tm_start.tv_sec;
				c->avg->tm_start.tv_usec = c->sweeplist[x]->tm_start.tv_usec;
			} else if (c->sweeplist[x]->tm_start.tv_sec == c->avg->tm_start.tv_sec &&
					   c->sweeplist[x]->tm_start.tv_usec > c->avg->tm_start.tv_usec) {
				c->avg->tm_start.tv_usec = c->sweeplist[x]->tm_start.tv_usec;
			}
			if (c->sweeplist[x]->tm_end.tv_sec > c->avg->tm_end.tv_sec) {
				c->avg->tm_end.tv_sec = c->sweeplist[x]->tm_end.tv_sec;
				c->avg->tm_end.tv_usec = c->sweeplist[x]->tm_end.tv_usec;
			} else if (c->sweeplist[x]->tm_end.tv_sec == c->avg->tm_end.tv_sec &&
					   c->sweeplist[x]->tm_end.tv_usec > c->avg->tm_end.tv_usec) {
				c->avg->tm_end.tv_usec = c->sweeplist[x]->tm_end.tv_usec;
			}

		}

		for (x = 0; x < c->avg->num_samples; x++) {
			if (avgsum[x] == 0) {
				c->avg->sample_data[x] = 0;
				continue;
			}

			c->avg->sample_data[x] = (float) avgdata[x] / (float) avgsum[x];
		}

		free(avgdata);
		free(avgsum);
	}

	/* Allocate or update the peak.  We don't track peak timelines */
	if (c->peak == NULL && c->calc_peak) {
		c->peak = (spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(s->num_samples));
		memcpy(c->peak, s, SPECTOOL_SWEEP_SIZE(s->num_samples));
	
		/* This will never be allocated if peak is not */
		c->roll_peak = (spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(s->num_samples));
		memcpy(c->roll_peak, s, SPECTOOL_SWEEP_SIZE(s->num_samples));
	} else if (c->calc_peak) {

		for (x = 0; x < c->peak->num_samples; x++) {
			if (c->peak->sample_data[x] < s->sample_data[x]) {
				c->peak->sample_data[x] = s->sample_data[x];
			}
		}

		memcpy(c->roll_peak, s, SPECTOOL_SWEEP_SIZE(s->num_samples));
		for (x = 0; x < nsampled; x++) {
			for (y = 0; y < c->roll_peak->num_samples; y++) {
				// printf("debug - compare sampled %d pos %d loop %d set %d sample %d\n", nsampled, c->pos, c->looped, x, y);
				if (c->roll_peak->sample_data[y] < c->sweeplist[x]->sample_data[y]) {
					c->roll_peak->sample_data[y] = c->sweeplist[x]->sample_data[y];
				}
			}
		}

	}
}

void spectool_device_scan_init(spectool_device_list *list) {
	list->list = (spectool_device_rec *) malloc(sizeof(spectool_device_rec) * MAX_SCAN_RESULT);
	list->num_devs = 0;
	list->max_devs = MAX_SCAN_RESULT;
}

int spectool_device_scan(spectool_device_list *list) {
	spectool_device_scan_init(list);

	if (wispy1_usb_device_scan(list) < 0) {
		return -1;
	}

	if (wispy24x_usb_device_scan(list) < 0) {
		return -1;
	}

	if (wispydbx_usb_device_scan(list) < 0) {
		return -1;
	}

	if (ubertooth_u1_device_scan(list) < 0) {
		return -1;
	}

	return list->num_devs;
}

void spectool_device_scan_free(spectool_device_list *list) {
	int x;

	for (x = 0; x < list->num_devs && x < list->max_devs; x++) {
		if (list->list[x].hw_rec != NULL)
			free(list->list[x].hw_rec);
	}

	list->num_devs = 0;
	list->max_devs = 0;

	if (list->list != NULL)
		free(list->list);
}

int spectool_device_init(spectool_phy *phydev, spectool_device_rec *rec) {
	return (*(rec->init_func))(phydev, rec);
}

spectool_sample_sweep *spectool_phy_getcurprofile(spectool_phy *phydev) {
	if (phydev == NULL)
		return NULL;

	if (phydev->device_spec->cur_profile < 0 || 
		phydev->device_spec->cur_profile > phydev->device_spec->num_sweep_ranges)
		return NULL;

	return &(phydev->device_spec->supported_ranges[phydev->device_spec->cur_profile]);
}

