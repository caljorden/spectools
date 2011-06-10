#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>
#include <math.h>

#include "config.h"

#include "spectool_container.h"
#include "spectool_net_client.h"

spectool_phy *dev = NULL;

void sighandle(int sig) {
	int x;

	endwin();

	printf("Dying %d from signal %d\n", getpid(), sig);

	exit(1);
}

void Usage(void) {
	printf("spectool_curses [ options ]\n"
		   " -n / --net  tcp://host:port  Connect to network server\n"
		   " -l / --list                  List detected devices\n"
		   " -r / --range #               Use device range #\n"
		   " -d / --device #              Use device #\n");
	return;
}

int main(int argc, char *argv[]) {
	spectool_device_list list;
	int x = 0, r = 0, y = 0, ndev = 0;
	spectool_sample_sweep *sb = NULL;
	spectool_sweep_cache *sweepcache = NULL;
	spectool_server sr;
	char errstr[SPECTOOL_ERROR_MAX];
	int ret;

	WINDOW *window;
	WINDOW *sigwin;
	WINDOW *chwin;

	int graph_bg = 0, graph_peak = 0, graph_avg = 0;

	int amp_offset_mdbm = 0, amp_res_mdbm = 0, base_db_offset = 0;
	int min_db_draw = 0, start_db = 0;
	int nuse = 0, mod, avg, avgc, pos, group;

	int range = 0;
	int device = -1;
	int list_only = 0;

	static struct option long_options[] = {
		{ "net", required_argument, 0, 'n' },
		{ "list", no_argument, 0, 'l' },
		{ "device", required_argument, 0, 'd' },
		{ "range", required_argument, 0, 'r' },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};
	int option_index;

	char *neturl = NULL;

	ndev = spectool_device_scan(&list);

	while (1) {
		int o = getopt_long(argc, argv, "n:ld:r:h",
							long_options, &option_index);

		if (o < 0)
			break;

		if (o == 'h') {
			Usage();
			return;
		} else if (o == 'n') {
			neturl = strdup(optarg);
			continue;
		} else if (o == 'r') {
			if (sscanf(optarg, "%d", &range) != 1) {
				printf("Expected number for range, see listing for supported ranges\n");
				Usage();
				return;
			}
			continue;
		} else if (o == 'd') {
			if (sscanf(optarg, "%d", &device) != 1) {
				printf("Expected number for device, see listing for detected devices\n");
				Usage();
				return;
			}

			if (device < 0 || device >= ndev) {
				printf("Device number invalid, see listing for detected devices\n");
				Usage();
				return;
			}

			continue;
		} else if (o == 'l') {
			list_only = 1;
		}
	}

	if (list_only) {
		if (ndev <= 0) {
			printf("No spectool devices found, bailing\n");
			exit(1);
		}

		printf("Found %d devices...\n", ndev);

		for (x = 0; x < ndev; x++) {
			printf("Device %d: %s id %u\n", 
				   x, list.list[x].name, list.list[x].device_id);

			for (r = 0; r < list.list[x].num_sweep_ranges; r++) {
				spectool_sample_sweep *ran = 
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
	signal(SIGINT, sighandle);

	if (neturl != NULL) {
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
			printf("No spectool devices found, bailing\n");
			exit(1);
		}

		printf("Found %d spectool devices...\n", ndev);

		if (ndev > 1 && device == -1) {
			printf("spectool-curses can only display one device, specify one with "
				   "spectool-curses -d\n");
			exit(1);
		} else if (device == -1) {
			device = 0;
		}

		if (range < 0 || range >= list.list[device].num_sweep_ranges) {
			printf("Invalid range for device %d, see listing for supported ranges\n",
				   device);
			exit(1);
		}

		dev = (spectool_phy *) malloc(SPECTOOL_PHY_SIZE);
		dev->next = NULL;

		if (spectool_device_init(dev, &(list.list[device])) < 0) {
			printf("Error initializing WiSPY device %s id %u\n",
				   list.list[device].name, list.list[device].device_id);
			printf("%s\n", spectool_get_error(dev));
			exit(1);
		}

		if (spectool_phy_open(dev) < 0) {
			printf("Error opening WiSPY device %s id %u\n",
				   list.list[device].name, list.list[device].device_id);
			printf("%s\n", spectool_get_error(dev));
			exit(1);
		}

		spectool_phy_setcalibration(dev, 1);

		/* Configure the range */
		spectool_phy_setposition(dev, range, 0, 0);

		spectool_device_scan_free(&list); 
	}

	sweepcache = spectool_cache_alloc(50, 1, 1);

	/* Fire up curses */
	initscr();
	start_color();
	cbreak();
	noecho();

	init_pair(1, COLOR_BLACK, COLOR_BLACK);
	init_pair(2, COLOR_BLUE, COLOR_BLUE);
	init_pair(3, COLOR_GREEN, COLOR_GREEN);
	init_pair(4, COLOR_YELLOW, COLOR_BLACK);
	init_pair(5, COLOR_YELLOW, COLOR_BLUE);
	init_pair(6, COLOR_YELLOW, COLOR_GREEN);

	window = subwin(stdscr, LINES - 2, COLS - 5, 0, 5);
	sigwin = subwin(stdscr, LINES - 2, 5, 0, 0);

	box(window, 0, 0);
	mvwaddstr(sigwin, 0, 0, " dBm ");

	wrefresh(window);
	wrefresh(sigwin);
	refresh();

	/* Naive poll that doesn't use select() to find pending data */
	while (1) {
		fd_set rfds;
		fd_set wfds;
		int maxfd = 0;
		struct timeval tm;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		if (spectool_phy_getpollfd(dev) >= 0) {
			FD_SET(spectool_phy_getpollfd(dev), &rfds);

			if (spectool_phy_getpollfd(dev) > maxfd)
				maxfd = spectool_phy_getpollfd(dev);
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

		tm.tv_sec = 0;
		tm.tv_usec = 10000;

		if (select(maxfd + 1, &rfds, &wfds, NULL, &tm) < 0) {
			printf("spectool_raw select() error: %s\n", strerror(errno));
			exit(1);
		}

		if (spectool_netcli_getwritefd(&sr) >= 0 &&
			FD_ISSET(spectool_netcli_getwritefd(&sr), &wfds)) {
			if (spectool_netcli_writepoll(&sr, errstr) < 0) {
				printf("Error write-polling network server %s\n", errstr);
				exit(1);
			}
		}

		ret = SPECTOOL_NETCLI_POLL_ADDITIONAL;
		while (spectool_netcli_getpollfd(&sr) >= 0 &&
			   FD_ISSET(spectool_netcli_getpollfd(&sr), &rfds) &&
			   (ret & SPECTOOL_NETCLI_POLL_ADDITIONAL)) {

			if ((ret = spectool_netcli_poll(&sr, errstr)) < 0) {
				printf("Error polling network server %s\n", errstr);
				exit(1);
			}

			if ((ret & SPECTOOL_NETCLI_POLL_NEWDEVS)) {
				/* Only enable the first device */
				spectool_net_dev *ndi = sr.devlist;

				dev = spectool_netcli_enabledev(&sr, ndi->device_id, errstr);
			}
		}

		if (spectool_phy_getpollfd(dev) < 0) {
			if (spectool_get_state(dev) == SPECTOOL_STATE_ERROR) {
				printf("Error polling spectool device %s\n",
					   spectool_phy_getname(dev));
				printf("%s\n", spectool_get_error(dev));
				exit(1);
			}
		}

		if (FD_ISSET(spectool_phy_getpollfd(dev), &rfds) == 0) {
			continue;
		}

		do {
			r = spectool_phy_poll(dev);

			if ((r & SPECTOOL_POLL_CONFIGURED)) {
				spectool_sample_sweep *ran = &(dev->device_spec->supported_ranges[0]);

				amp_offset_mdbm = ran->amp_offset_mdbm;
				amp_res_mdbm = ran->amp_res_mdbm;
				base_db_offset =
					SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm,
									   ran->rssi_max);
				min_db_draw =
					SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm, 0);

				continue;
			} else if ((r & SPECTOOL_POLL_ERROR)) {
				printf("Error polling spectool device %s\n",
					   spectool_phy_getname(dev));
				printf("%s\n", spectool_get_error(dev));
				exit(1);
			} else if ((r & SPECTOOL_POLL_SWEEPCOMPLETE)) {
				sb = spectool_phy_getsweep(dev);
				if (sb == NULL)
					continue;

				spectool_cache_append(sweepcache, sb);

				min_db_draw =
					SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm,
									   sb->min_rssi_seen > 2 ?
									   sb->min_rssi_seen - 1 : sb->min_rssi_seen);

			}
		} while ((r & SPECTOOL_POLL_ADDITIONAL));

		/* Redraw the windows */
		werase(sigwin);
		mvwaddstr(sigwin, 0, 0, " dBm ");

		start_db = 0;
		for (x = base_db_offset - 1; x > min_db_draw; x--) {
			if (x % 10 == 0) {
				start_db = 0;
				break;
			}
		}

		if (start_db == 0)
			start_db = base_db_offset;

		for (x = start_db; x > min_db_draw; x -= 10) {
			int py;

			py = (float) (LINES - 4) * 
				(float) ((float) (abs(x) + base_db_offset) /
						 (float) (abs(min_db_draw) + base_db_offset));

			snprintf(errstr, SPECTOOL_ERROR_MAX, "%d", x);
			mvwaddstr(sigwin, py + 1, 0, errstr);
		}

		werase(window);

		if (sweepcache->latest == NULL) {
			wrefresh(window);
			wrefresh(sigwin);
			refresh();
			continue;
		}

		/* Interpolate the data down into an appropriate graph */
		mod = ceilf((float) sweepcache->avg->num_samples / (float) (COLS - 7));
		group = mod * 2;

		r = 0;
		for (x = 0; x < (COLS - 7); x++) {
			int py, pyc;

			avg = 0;
			avgc = 0;
			nuse = 0;

			r = ((float) x / (float) (COLS - 7)) * 
				(float) sweepcache->peak->num_samples;

			for (pos = -1 * (group / 2); pos < (group / 2); pos++) {
				if (r + pos >= sweepcache->peak->num_samples || r + pos < 0)
					continue;

				avg += sweepcache->peak->sample_data[r + pos];
				avgc += sweepcache->latest->sample_data[r + pos];
				nuse++;
			}

			if (nuse == 0)
				continue;

			avg = SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm, (avg / nuse));
			avgc = SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm, (avgc / nuse));

			py = (float) (LINES - 4) *
				(float) ((float) (abs(avg) + base_db_offset) /
						 (float) (abs(min_db_draw) + base_db_offset));

			pyc = (float) (LINES - 4) *
				(float) ((float) (abs(avgc) + base_db_offset) /
						 (float) (abs(min_db_draw) + base_db_offset));

			for (y = 0; y < (LINES - 4); y++) {
				if (pyc == y) {
					if (py > y)
						wcolor_set(window, 4, NULL);
					else
						wcolor_set(window, 5, NULL);

					mvwaddstr(window, y + 1, x + 1, "#");
					continue;
				}

				if (py > y)
					continue;

				wcolor_set(window, 2, NULL);
				mvwaddstr(window, y + 1, x + 1, " ");
			}
		}

		r = 0;
		for (x = 0; x < (COLS - 7); x++) {
			int py, pyc;

			avg = 0;
			avgc = 0;
			nuse = 0;

			r = ((float) x / (float) (COLS - 7)) * 
				(float) sweepcache->avg->num_samples;

			for (pos = -1 * (group / 2); pos < (group / 2); pos++) {
				if (r + pos >= sweepcache->avg->num_samples || r + pos < 0)
					continue;

				avg += sweepcache->avg->sample_data[r + pos];
				avgc += sweepcache->latest->sample_data[r + pos];
				nuse++;
			}

			if (nuse == 0)
				continue;

			avg = SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm, (avg / nuse));
			avgc = SPECTOOL_RSSI_CONVERT(amp_offset_mdbm, amp_res_mdbm, (avgc / nuse));

			py = (float) (LINES - 4) *
				(float) ((float) (abs(avg) + base_db_offset) /
						 (float) (abs(min_db_draw) + base_db_offset));

			pyc = (float) (LINES - 4) *
				(float) ((float) (abs(avgc) + base_db_offset) /
						 (float) (abs(min_db_draw) + base_db_offset));

			for (y = 0; y < (LINES - 4); y++) {
				if (pyc == y && y >= py) {
					wcolor_set(window, 6, NULL);

					mvwaddstr(window, y + 1, x + 1, "#");
					continue;
				}

				if (py > y)
					continue;

				wcolor_set(window, 3, NULL);
				mvwaddstr(window, y + 1, x + 1, "m");
			}
		}

		wcolor_set(window, 0, NULL);
		box(window, 0, 0);

		wrefresh(window);
		wrefresh(sigwin);
		refresh();
	}

	endwin();
	return 0;
}	

