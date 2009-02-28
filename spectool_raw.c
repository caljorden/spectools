/* Spectrum tools raw interface
 *
 * Basic dumper to dump the raw values from the USB device to stdout.  Meant
 * to be fed to another app via popen (such as the python grapher).  Caller
 * is responsible for converting the RSSI values.
 *
 * Mike Kershaw/Dragorn <dragorn@kismetwireless.net>
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

#include <stdio.h>
#include <usb.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"

#include "spectool_container.h"
#include "spectool_net_client.h"

wispy_phy *devs = NULL;
int ndev = 0;

void sighandle(int sig) {
	int x;

	printf("Dying %d from signal %d\n", getpid(), sig);

	exit(1);
}

void Usage(void) {
	printf("wispy_raw [ options ]\n"
		   " -n / --net  tcp://host:port  Connect to network server instead of\n"
		   " -b / --broadcast             Listen for (and connect to) broadcast servers\n"
		   " -l / --list				  List devices and ranges only\n"
		   " -r / --range [device:]range  Configure a device for a specific range\n"
		   "                              local USB devices\n");
	return;
}

int main(int argc, char *argv[]) {
	wispy_device_list list;
	int x = 0, r = 0;
	wispy_sample_sweep *sb;
	spectool_server sr;
	char errstr[WISPY_ERROR_MAX];
	int ret;
	wispy_phy *pi;

	static struct option long_options[] = {
		{ "net", required_argument, 0, 'n' },
		{ "broadcast", no_argument, 0, 'b' },
		{ "list", no_argument, 0, 'l' },
		{ "range", required_argument, 0, 'r' },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};
	int option_index;

	char *neturl = NULL;

	char bcasturl[SPECTOOL_NETCLI_URL_MAX];
	int bcastlisten = 0;
	int bcastsock;

	int list_only = 0;

	ndev = wispy_device_scan(&list);

	int *rangeset = NULL;
	if (ndev > 0) {
		rangeset = (int *) malloc(sizeof(int) * ndev);
		memset(rangeset, 0, sizeof(int) * ndev);
	}

	while (1) {
		int o = getopt_long(argc, argv, "n:bhr:l",
							long_options, &option_index);

		if (o < 0)
			break;

		if (o == 'h') {
			Usage();
			return;
		} else if (o == 'b') {
			bcastlisten = 1;
		} else if (o == 'n') {
			neturl = strdup(optarg);
			printf("debug - wispy_raw neturl %s\n", neturl);
			continue;
		} else if (o == 'l') {
			list_only = 1;
		} else if (o == 'r' && ndev > 0) {
			if (sscanf(optarg, "%d:%d", &x, &r) != 2) {
				if (sscanf(optarg, "%d", &r) != 1) {
					fprintf(stderr, "Invalid range, expected device#:range# "
							"or range#\n");
					exit(-1);
				} else {
					rangeset[0] = r;
				}
			} else {
				if (x < 0 || x >= ndev) {
					fprintf(stderr, "Invalid range, no device %d\n", x);
					exit(-1);
				} else {
					rangeset[x] = r;
				}
			}
		}
	}

	signal(SIGINT, sighandle);

	if (list_only) {
		if (ndev <= 0) {
			printf("No wispy devices found, bailing\n");
			exit(1);
		}

		printf("Found %d devices...\n", ndev);

		for (x = 0; x < ndev; x++) {
			printf("Device %d: %s id %u\n", 
				   x, list.list[x].name, list.list[x].device_id);

			for (r = 0; r < list.list[x].num_sweep_ranges; r++) {
				wispy_sample_sweep *ran = 
					&(list.list[x].supported_ranges[r]);

				printf("  Range %d: \"%s\" %d%s-%d%s @ %0.2f%s, %d samples\n", r, 
					   ran->name,
					   ran->start_khz > 1000 ? 
					   ran->start_khz / 1000 : ran->start_khz,
					   ran->start_khz > 1000 ? "MHz" : "KHz",
					   ran->end_khz > 1000 ? ran->end_khz / 1000 : ran->end_khz,
					   ran->end_khz > 1000 ? "MHz" : "KHz",
					   (ran->res_hz / 1000) > 1000 ? 
					   		((float) ran->res_hz / 1000) / 1000 : ran->res_hz / 1000,
					   (ran->res_hz / 1000) > 1000 ? "MHz" : "KHz",
					   ran->num_samples);
			}

		}

		exit(0);
	}

	if (bcastlisten) {
		printf("Initializing broadcast listen...\n");

		if ((bcastsock = spectool_netcli_initbroadcast(WISPY_NET_DEFAULT_PORT,
													   errstr)) < 0) {
			printf("Error initializing bcast socket: %s\n", errstr);
			exit(1);
		}

		printf("Waiting for a broadcast server ID...\n");
	} else if (neturl != NULL) {
		printf("Initializing network connection...\n");

		if (spectool_netcli_init(&sr, neturl, errstr) < 0) {
			printf("Error initializing network connection: %s\n", errstr);
			exit(1);
		}

		if (spectool_netcli_connect(&sr, errstr) < 0) {
			printf("Error opening network connection: %s\n", errstr);
			exit(1);
		}

		printf("Connected to server, waiting for device list...\n");
	} else if (neturl == NULL) {
		if (ndev <= 0) {
			printf("No wispy devices found, bailing\n");
			exit(1);
		}

		printf("Found %d wispy devices...\n", ndev);

		for (x = 0; x < ndev; x++) {
			printf("Initializing WiSPY device %s id %u\n", 
				   list.list[x].name, list.list[x].device_id);

			pi = (wispy_phy *) malloc(WISPY_PHY_SIZE);
			pi->next = devs;
			devs = pi;

			if (wispy_device_init(pi, &(list.list[x])) < 0) {
				printf("Error initializing WiSPY device %s id %u\n",
					   list.list[x].name, list.list[x].device_id);
				printf("%s\n", wispy_get_error(pi));
				exit(1);
			}

			if (wispy_phy_open(pi) < 0) {
				printf("Error opening WiSPY device %s id %u\n",
					   list.list[x].name, list.list[x].device_id);
				printf("%s\n", wispy_get_error(pi));
				exit(1);
			}

			wispy_phy_setcalibration(pi, 1);

			/* configure the default sweep block */
			wispy_phy_setposition(pi, rangeset[x], 0, 0);
		}

		wispy_device_scan_free(&list); 
	}

	/* Naive poll that doesn't use select() to find pending data */
	while (1) {
		fd_set rfds;
		fd_set wfds;
		int maxfd = 0;
		struct timeval tm;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		pi = devs;
		while (pi != NULL) {
			if (wispy_phy_getpollfd(pi) >= 0) {
				FD_SET(wispy_phy_getpollfd(pi), &rfds);

				if (wispy_phy_getpollfd(pi) > maxfd)
					maxfd = wispy_phy_getpollfd(pi);
			}

			pi = pi->next;
		}

		if (neturl != NULL) {
			if (spectool_netcli_getpollfd(&sr) >= 0) {
				FD_SET(spectool_netcli_getpollfd(&sr), &rfds);

				if (spectool_netcli_getpollfd(&sr) > maxfd)
					maxfd = spectool_netcli_getpollfd(&sr);
			}
			if (spectool_netcli_getwritepend(&sr) > 0) {
				FD_SET(spectool_netcli_getwritefd(&sr), &wfds);

				if (spectool_netcli_getwritefd(&sr) > maxfd)
					maxfd = spectool_netcli_getwritefd(&sr);
			}
		}

		if (bcastlisten) {
			FD_SET(bcastsock, &rfds);
			if (bcastsock > maxfd)
				maxfd = bcastsock;
		}

		tm.tv_sec = 0;
		tm.tv_usec = 10000;

		if (select(maxfd + 1, &rfds, &wfds, NULL, &tm) < 0) {
			printf("wispy_raw select() error: %s\n", strerror(errno));
			exit(1);
		}

		if (bcastlisten && FD_ISSET(bcastsock, &rfds)) {
			if (spectool_netcli_pollbroadcast(bcastsock, bcasturl, errstr) == 1) {
				printf("Saw broadcast for server %s\n", bcasturl);

				if (neturl == NULL) {
					neturl = strdup(bcasturl);

					if (spectool_netcli_init(&sr, neturl, errstr) < 0) {
						printf("Error initializing network connection: %s\n", errstr);
						exit(1);
					}

					if (spectool_netcli_connect(&sr, errstr) < 0) {
						printf("Error opening network connection: %s\n", errstr);
						exit(1);
					}
				}
			}
		}

		if (neturl != NULL && spectool_netcli_getwritefd(&sr) >= 0 &&
			FD_ISSET(spectool_netcli_getwritefd(&sr), &wfds)) {
			if (spectool_netcli_writepoll(&sr, errstr) < 0) {
				printf("Error write-polling network server %s\n", errstr);
				exit(1);
			}
		}

		ret = SPECTOOL_NETCLI_POLL_ADDITIONAL;
		while (neturl != NULL && spectool_netcli_getpollfd(&sr) >= 0 &&
			   FD_ISSET(spectool_netcli_getpollfd(&sr), &rfds) &&
			   (ret & SPECTOOL_NETCLI_POLL_ADDITIONAL)) {

			if ((ret = spectool_netcli_poll(&sr, errstr)) < 0) {
				printf("Error polling network server %s\n", errstr);
				exit(1);
			}

			if ((ret & SPECTOOL_NETCLI_POLL_NEWDEVS)) {
				spectool_net_dev *ndi = sr.devlist;
				while (ndi != NULL) {
					printf("Enabling network device: %s (%u)\n", ndi->device_name,
						   ndi->device_id);
					pi = spectool_netcli_enabledev(&sr, ndi->device_id, errstr);

					pi->next = devs;
					devs = pi;

					ndi = ndi->next;
				}

			}
		}

		pi = devs;
		while (pi != NULL) {
			wispy_phy *di = pi;
			pi = pi->next;

			if (wispy_phy_getpollfd(di) < 0) {
				if (wispy_get_state(di) == WISPY_STATE_ERROR) {
					printf("Error polling wispy device %s\n",
						   wispy_phy_getname(di));
					printf("%s\n", wispy_get_error(di));
					exit(1);
				}

				continue;
			}

			if (FD_ISSET(wispy_phy_getpollfd(di), &rfds) == 0) {
				continue;
			}

			do {
				r = wispy_phy_poll(di);

				if ((r & WISPY_POLL_CONFIGURED)) {
					printf("Configured device %u (%s)\n", 
						   wispy_phy_getdevid(di), 
						   wispy_phy_getname(di),
						   di->device_spec->num_sweep_ranges);

					wispy_sample_sweep *ran = 
						wispy_phy_getcurprofile(di);

					if (ran == NULL) {
						printf("Error - no current profile?\n");
						continue;
					}

					printf("    %d%s-%d%s @ %0.2f%s, %d samples\n", 
						   ran->start_khz > 1000 ? 
						   ran->start_khz / 1000 : ran->start_khz,
						   ran->start_khz > 1000 ? "MHz" : "KHz",
						   ran->end_khz > 1000 ? ran->end_khz / 1000 : ran->end_khz,
						   ran->end_khz > 1000 ? "MHz" : "KHz",
						   (ran->res_hz / 1000) > 1000 ? 
						   	((float) ran->res_hz / 1000) / 1000 : ran->res_hz / 1000,
						   (ran->res_hz / 1000) > 1000 ? "MHz" : "KHz",
						   ran->num_samples);

					continue;
				} else if ((r & WISPY_POLL_ERROR)) {
					printf("Error polling wispy device %s\n",
						   wispy_phy_getname(di));
					printf("%s\n", wispy_get_error(di));
					exit(1);
				} else if ((r & WISPY_POLL_SWEEPCOMPLETE)) {
					sb = wispy_phy_getsweep(di);
					if (sb == NULL)
						continue;
					printf("%s: ", wispy_phy_getname(di));
					for (r = 0; r < sb->num_samples; r++) {
						printf("%d ", 
							WISPY_RSSI_CONVERT(sb->amp_offset_mdbm, sb->amp_res_mdbm,
											   sb->sample_data[r]));
					}
					printf("\n");
				}
			} while ((r & WISPY_POLL_ADDITIONAL));

		}
	}

	return 0;
}	

