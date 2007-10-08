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

int wispy_get_state(wispy_phy *phydev) {
	return phydev->state;
}

char *wispy_get_error(wispy_phy *phydev) {
	return phydev->errstr;
}

char *wispy_phy_getname(wispy_phy *phydev) {
	if (phydev->device_spec == NULL)
		return NULL;

	return phydev->device_spec->device_name;
}

int wispy_phy_getdevid(wispy_phy *phydev) {
	if (phydev->device_spec == NULL)
		return 0;

	return phydev->device_spec->device_id;
}

void wispy_phy_setname(wispy_phy *phydev, char *name) {
	if (phydev->device_spec == NULL)
		return;

	snprintf(phydev->device_spec->device_name, WISPY_PHY_NAME_MAX, "%s", name);
}

int wispy_phy_open(wispy_phy *phydev) {
	if (phydev->open_func == NULL)
		return 0;

	return (*(phydev->open_func))(phydev);
}

int wispy_phy_close(wispy_phy *phydev) {
	if (phydev->close_func == NULL)
		return 0;

	return (*(phydev->close_func))(phydev);
}

int wispy_phy_poll(wispy_phy *phydev) {
	if (phydev->poll_func == NULL)
		return WISPY_POLL_ERROR;

	return (*(phydev->poll_func))(phydev);
}

int wispy_phy_getpollfd(wispy_phy *phydev) {
	if (phydev->pollfd_func == NULL)
		return -1;

	return (*(phydev->pollfd_func))(phydev);
}

void wispy_phy_setcalibration(wispy_phy *phydev, int enable) {
	if (phydev->setcalib_func != NULL)
		return (*(phydev->setcalib_func))(phydev, enable);
}

int wispy_phy_getflags(wispy_phy *phydev) {
	return phydev->device_spec->device_flags;
}

int wispy_phy_setposition(wispy_phy *phydev, int start_khz, int res_hz) {
	if (phydev->setposition_func != NULL)
		return (*(phydev->setposition_func))(phydev, start_khz, res_hz);

	snprintf(phydev->errstr, WISPY_ERROR_MAX, "Device does not support setting "
			 "scan position or resolution");
	return -1;
}

wispy_sample_sweep *wispy_phy_getsweep(wispy_phy *phydev) {
	if (phydev->getsweep_func == NULL)
		return NULL;

	return (*(phydev->getsweep_func))(phydev);
}

wispy_sweep_cache *wispy_cache_alloc(int nsweeps, int calc_peak, int calc_avg) {
	int x;
	wispy_sweep_cache *c = (wispy_sweep_cache *) malloc(sizeof(wispy_sweep_cache));

	c->sweeplist = 
		(wispy_sample_sweep **) malloc(sizeof(wispy_sample_sweep *) * nsweeps);
	c->avg = NULL;
	c->peak = NULL;
	c->latest = NULL;

	for (x = 0; x < nsweeps; x++)
		c->sweeplist[x] = NULL;

	c->num_alloc = nsweeps;
	c->pos = -1;

	c->calc_peak = calc_peak;
	c->calc_avg = calc_avg;

	return c;
}

void wispy_cache_free(wispy_sweep_cache *c) {
	if (c->avg != NULL)
		free(c->avg);
	if (c->peak != NULL)
		free(c->peak);
	free(c->sweeplist);
}

void wispy_cache_clear(wispy_sweep_cache *c) {
	if (c->avg != NULL)
		free(c->avg);
	c->avg = NULL;

	if (c->peak != NULL)
		free(c->peak);
	c->peak = NULL;

	c->pos = 0;
}

void wispy_cache_append(wispy_sweep_cache *c, wispy_sample_sweep *s) {
	int x, y, sum = 0;
	int *avgdata, *avgsum;
	int navg;

	/* Make sure we don't overflow and crash, should be sufficient
	 * to make sure that the new sweep is reasonable */
	if (c->avg != NULL && c->avg->num_samples != s->num_samples) 
		return;

	if (c->pos == (c->num_alloc - 1))
		c->pos = 0;
	else
		c->pos++;

	if (c->sweeplist[c->pos] != NULL)
		free(c->sweeplist[c->pos]);

	c->sweeplist[c->pos] = 
		(wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(s->num_samples));

	memcpy(c->sweeplist[c->pos], s, WISPY_SWEEP_SIZE(s->num_samples));

	c->latest = c->sweeplist[c->pos];

	if (c->avg == NULL && c->calc_avg) {
		c->avg = (wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(s->num_samples));
		memcpy(c->avg, s, WISPY_SWEEP_SIZE(s->num_samples));
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
		for (x = 0; x < c->num_alloc; x++) {
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
		c->peak = (wispy_sample_sweep *) malloc(WISPY_SWEEP_SIZE(s->num_samples));
		memcpy(c->peak, s, WISPY_SWEEP_SIZE(s->num_samples));
	} else if (c->calc_peak) {
		for (x = 0; x < c->peak->num_samples; x++) {
			if (c->peak->sample_data[x] < s->sample_data[x]) {
				c->peak->sample_data[x] = s->sample_data[x];
			}
		}
	}
}

void wispy_device_scan_init(wispy_device_list *list) {
	list->list = (wispy_device_rec *) malloc(sizeof(wispy_device_rec) * MAX_SCAN_RESULT);
	list->num_devs = 0;
	list->max_devs = MAX_SCAN_RESULT;
}

int wispy_device_scan(wispy_device_list *list) {
	wispy_device_scan_init(list);

	if (wispy1_usb_device_scan(list) < 0) {
		return -1;
	}

	if (wispy24x_usb_device_scan(list) < 0) {
		return -1;
	}

	return list->num_devs;
}

void wispy_device_scan_free(wispy_device_list *list) {
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

int wispy_device_init(wispy_phy *phydev, wispy_device_rec *rec) {
	return (*(rec->init_func))(phydev, rec);
}

