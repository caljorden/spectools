/* 
 * Mike Kershaw/Dragorn <dragorn@kismetwireless.net>
 *
 * Spectool/Chanalyzer network protocol server
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>

#include "config.h"
#include "spectool_container.h"
#include "spectool_net.h"

/* Size of the client buffer - a packet should never be this large since
 * it would fragment all over, so this should be fine */
#define CLI_BUF_SZ		2048

typedef struct _wispy_tcpcli_dev {
	uint32_t device_id;
	struct _wispy_tcpcli_dev *next;
} wispy_tcpcli_dev;

typedef struct _wispy_tcpcli {
	int fd;
	uint8_t wbuf[CLI_BUF_SZ];
	uint8_t rbuf[CLI_BUF_SZ];
	int write_pos;
	int read_pos;

	/* Naive model, we assume that we can write faster than we can fill.
	 * Also, we don't mind so much if we lose sweep data, which is the 
	 * only data which ought to really build up over time */
	int write_fill, read_fill;

	/* List of devices we send sweep data for */
	wispy_tcpcli_dev *devlist;

	struct _wispy_tcpcli *next;
} wispy_tcpcli;

typedef struct _wispy_tcpserv_dev {
	wispy_phy phydev;
	int lock_fd;
} wispy_tcpserv_dev;

typedef struct _wispy_tcpserv {
	short int port;
	unsigned int maxclients;
	struct sockaddr_in serv_addr;
	int bindfd;
	fd_set master_fds;
	unsigned int maxfd;

	wispy_tcpcli *cli_list;

	wispy_tcpserv_dev *devs;

	int ndev;
} wispy_tcpserv;

int wts_init(wispy_tcpserv *wts) {
	wts->port = 0;
	wts->maxclients = 0;
	wts->bindfd = 0;
	FD_ZERO(&(wts->master_fds));
	memset(&(wts->serv_addr), 0, sizeof(wts->serv_addr));
	wts->maxfd = 0;
	wts->cli_list = NULL;
	wts->devs = NULL;
	wts->ndev = 0;
	return 1;
}

int wts_bind(wispy_tcpserv *wts, char *addr, short int port, char *errstr) {
	int sz = 2;

	/* TODO - support binding to an address */

	memset(&(wts->serv_addr), 0, sizeof(wts->serv_addr));

	wts->port = port;

	wts->serv_addr.sin_family = AF_INET;
	wts->serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	wts->serv_addr.sin_port = htons(port);

	if ((wts->bindfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX, "socket() failed %s", strerror(errno));
		return -1;
	}

	if (setsockopt(wts->bindfd, SOL_SOCKET, SO_REUSEADDR, &sz, sizeof(sz)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX, "setsockopt() failed %s", strerror(errno));
		return -1;
	}

	if (bind(wts->bindfd, (struct sockaddr *) &(wts->serv_addr), 
			 sizeof(wts->serv_addr)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX, "bind() failed %s", strerror(errno));
		return -1;
	}

	if (listen(wts->bindfd, 10) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX, "listen() failed %s", strerror(errno));
		return -1;
	}

	FD_SET(wts->bindfd, &(wts->master_fds));

	if (wts->maxfd < wts->bindfd)
		wts->maxfd = wts->bindfd;

	return 1;
}

int wts_cli_append(wispy_tcpcli *tci, uint8_t *data, int len, char *errstr) {
	if (tci->write_fill + len >= CLI_BUF_SZ) {
		snprintf(errstr, WISPY_ERROR_MAX, "client write buffer %d can't fit %d bytes, "
				 "%d of %d full", tci->fd, len, tci->write_fill, CLI_BUF_SZ);
		return -1;
	}

	memcpy(&(tci->wbuf[tci->write_fill]), data, len);
	tci->write_fill += len;

	return 1;
}

int wts_send_devblock(wispy_tcpserv *wts, wispy_tcpcli *tci, char *errstr) {
	wispy_fr_header *hdr;
	wispy_fr_device *dev;
	wispy_fr_sweep *sweep;

	int devblen = 0, x = 0, r = 0;
	wispy_sample_sweep *ran;

	/* Number of devices */
	devblen = wispy_fr_device_size() * wts->ndev;

	/* Plus one more device for the end block */
	devblen += wispy_fr_device_size();

	/* Big allocation of the entire block */
	hdr = (wispy_fr_header *) malloc(wispy_fr_header_size() +
									 devblen);

	hdr->sentinel = htonl(WISPY_NET_SENTINEL);
	hdr->frame_len = htons(wispy_fr_header_size() + devblen);
	hdr->proto_version = WISPY_NET_PROTO_VERSION;
	hdr->block_type = WISPY_NET_FRAME_DEVICE;
	hdr->num_blocks = wts->ndev + 1; /* ndevs + lastdev */

	int lastpos = 0;
	for (x = 0; x < wts->ndev; x++) {
		wispy_tcpserv_dev *d = &(wts->devs[x]);

		dev = (wispy_fr_device *) &(hdr->data[lastpos]);

		lastpos += wispy_fr_device_size();

		dev->frame_len = htons(wispy_fr_device_size());
		dev->device_version = d->phydev.device_spec->device_version;
		dev->device_flags = htons(d->phydev.device_spec->device_flags);
		dev->device_id = htonl(d->phydev.device_spec->device_id);
		dev->device_name_len = strlen(wispy_phy_getname(&(d->phydev)));
		snprintf(dev->device_name, 256, "%s", wispy_phy_getname(&(d->phydev)));

		dev->amp_offset_mdbm = 
			htonl(d->phydev.device_spec->default_range.amp_offset_mdbm * -1);
		dev->amp_res_mdbm =
			htonl(d->phydev.device_spec->default_range.amp_res_mdbm);
		dev->rssi_max = 
			htons(d->phydev.device_spec->default_range.rssi_max);

		dev->def_start_khz =
			htonl(d->phydev.device_spec->default_range.start_khz);
		dev->def_res_hz =
			htonl(d->phydev.device_spec->default_range.res_hz);
		dev->def_num_samples =
			htons(d->phydev.device_spec->default_range.num_samples);

		if (d->phydev.device_spec->num_sweep_ranges == 0) {
			dev->start_khz = dev->def_start_khz;
			dev->res_hz = dev->def_res_hz;
			dev->num_samples = dev->def_num_samples;
		} else {
			dev->start_khz =
				htonl(d->phydev.device_spec->supported_ranges[0].start_khz);
			dev->res_hz =
				htonl(d->phydev.device_spec->supported_ranges[0].res_hz);
			dev->num_samples =
				htons(d->phydev.device_spec->supported_ranges[0].num_samples);
		}
	}

	/* Set the last device sentinel -- nothing needs to be set except the 
	 * length of the block and the device type */
	dev = (wispy_fr_device *) &(hdr->data[lastpos]);
	memset(dev, 0, wispy_fr_device_size());
	dev->frame_len = htons(wispy_fr_device_size());
	dev->device_version = WISPY_NET_DEVTYPE_LASTDEV;

	if (wts_cli_append(tci, (uint8_t *) hdr,
					   wispy_fr_header_size() + devblen,
					   errstr) < 0)
		return -1;

	free(hdr);
	
	return 1;
}

int wts_send_devblock_all(wispy_tcpserv *wts, char *errstr) {
	wispy_tcpcli *tci = wts->cli_list;

	while (tci != NULL) {
		wts_send_devblock(wts, tci, errstr);

		tci = tci->next;
	}

	return 1;
}

wispy_tcpcli *wts_accept(wispy_tcpserv *wts, char *errstr) {
	int newfd;
	wispy_tcpcli *tc;
	struct sockaddr_in client_addr;
	socklen_t client_len;
	char inhost[16];
	int save_mode;

	memset(&client_addr, 0, sizeof(struct sockaddr_in));
	client_len = sizeof(struct sockaddr_in);

	if ((newfd = accept(wts->bindfd, (struct sockaddr *) &client_addr,
						&client_len)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX, "accept() failed %s", strerror(errno));
		return NULL;
	}

	tc = (wispy_tcpcli *) malloc(sizeof(wispy_tcpcli));

	tc->fd = newfd;

	tc->write_pos = 0;
	tc->read_pos = 0;
	tc->write_fill = 0;
	tc->read_fill = 0;
	tc->devlist = NULL;

	tc->next = wts->cli_list;
	wts->cli_list = tc;

	save_mode = fcntl(tc->fd, F_GETFL, 0);
	fcntl(tc->fd, F_SETFL, save_mode | O_NONBLOCK);

	FD_SET(newfd, &(wts->master_fds));

	return tc;
}

void wts_remove(wispy_tcpserv *wts, wispy_tcpcli *tc, char *errstr) {
	wispy_tcpcli *tci = wts->cli_list;
	wispy_tcpcli *tcb = NULL;
	int x, dchange;

	/* Unlock any devices they controlled */
	dchange = 0;
	for (x = 0; x < wts->ndev; x++) {
		if (wts->devs[x].lock_fd == tc->fd && tc->fd >= 0) {
			dchange = 1;
			wts->devs[x].lock_fd = -1;
		}
	}

	if (tc->fd >= 0) {
		FD_CLR(tc->fd, &(wts->master_fds));
		close(tc->fd);
	}

	if (tc == tci) {
		wts->cli_list = tci->next;
		free(tc);
		return;
	}

	while (tci != NULL) {
		tcb = tci;

		tci = tci->next;

		if (tci == tc) {
			tcb->next = tci->next;
			free(tci);
		}
	}

	/* Update everyone */
	if (dchange)
		wts_send_devblock_all(wts, errstr);
}

int wts_fdset(wispy_tcpserv *wts, fd_set *rfd, fd_set *wfd) {
	wispy_tcpcli *tci = wts->cli_list;
	int x;

	FD_SET(wts->bindfd, rfd);

	/* tcp clients */
	while (tci != NULL) {
		FD_SET(tci->fd, rfd);

		if (tci->write_fill > 0) {
			FD_SET(tci->fd, wfd);
		}

		if (tci->fd > wts->maxfd)
			wts->maxfd = tci->fd;

		tci = tci->next;
	}

	/* wispy devices */
	for (x = 0; x < wts->ndev; x++) {
		FD_SET(wispy_phy_getpollfd(&(wts->devs[x].phydev)), rfd);

		if (wispy_phy_getpollfd(&(wts->devs[x].phydev)) > wts->maxfd)
			wts->maxfd = wispy_phy_getpollfd(&(wts->devs[x].phydev));
	}

	return 1;
}

int wts_send_sweepblock(wispy_tcpserv *wts, 
						wispy_phy *phydev, wispy_sample_sweep *sweep, 
						char *errstr) {
	wispy_fr_header *hdr;
	wispy_fr_sweep *fsweep;
	wispy_tcpcli *tci = NULL;
	int x;

	/* Big allocation */
	hdr = (wispy_fr_header *) malloc(wispy_fr_header_size() +
									 wispy_fr_sweep_size(sweep->num_samples));

	hdr->sentinel = htonl(WISPY_NET_SENTINEL);
	hdr->frame_len = htons(wispy_fr_header_size() + 
						   wispy_fr_sweep_size(sweep->num_samples));
	hdr->proto_version = WISPY_NET_PROTO_VERSION;
	hdr->block_type = WISPY_NET_FRAME_SWEEP;
	hdr->num_blocks = 1;

	fsweep = (wispy_fr_sweep *) hdr->data;

	fsweep->frame_len = htons(wispy_fr_sweep_size(sweep->num_samples));
	fsweep->device_id = htonl(phydev->device_spec->device_id);

	fsweep->sweep_type = WISPY_NET_SWEEPTYPE_CUR;

	fsweep->start_sec = htonl(sweep->tm_start.tv_sec);
	fsweep->start_usec = htonl(sweep->tm_start.tv_usec);

	for (x = 0; x < sweep->num_samples; x++)
		fsweep->sample_data[x] = sweep->sample_data[x];

	tci = wts->cli_list;
	while (tci != NULL) {
		int send = 0;

		wispy_tcpcli_dev *di = tci->devlist;
		while (di != NULL) {
			if (di->device_id == phydev->device_spec->device_id) {
				send = 1;
				break;
			}

			di = di->next;
		}

		if (send) {
			if (wts_cli_append(tci, (uint8_t *) hdr,
							   wispy_fr_header_size() + 
							   wispy_fr_sweep_size(sweep->num_samples),
							   errstr) < 0)
				printf("Failure to send\n");
		}

		tci = tci->next;
	}

	free(hdr);
	
	return 1;
}

int wts_handle_command(wispy_tcpserv *wts, wispy_tcpcli *tci, 
					   wispy_fr_header *frh) {
	int blk, offt = 0;
	wispy_fr_command *ch;
	wispy_tcpcli_dev *di, *pdi;
	int did, x;

	if (ntohs(frh->frame_len) < wispy_fr_command_size(0)) {
		fprintf(stderr, "Short command frame, something is wrong, "
				"total len < cmd header\n");
		return -1;
	}

	for (blk = 0; blk < frh->num_blocks; blk++) {
		ch = (wispy_fr_command *) &(frh->data[offt]);

		offt += ntohs(ch->frame_len);

		if (ch->command_id == WISPY_NET_COMMAND_NULL) {
			/* No action */
			continue;
		} else if (ch->command_id == WISPY_NET_COMMAND_ENABLEDEV) {
			wispy_fr_command_enabledev *ce;
			int matched = 0;

			/* Find the device and activate it */
			if (ntohs(ch->frame_len) < 
				wispy_fr_command_size(wispy_fr_command_enabledev_size())) {
				fprintf(stderr, "Short enabledev frame, something is wrong, skipping\n");
				continue;
			}

			ce = (wispy_fr_command_enabledev *) ch->command_data;

			for (x = 0; x < wts->ndev; x++) {
				if (wispy_phy_getdevid(&(wts->devs[x].phydev)) == 
					ntohl(ce->device_id)) {
					matched = 1;
					break;
				}
			}

			/* Fail on an enable we don't understand */
			if (matched == 0) {
				fprintf(stderr, "Enabledev trying to enable device we don't understand\n");
				continue;
			}

			di = tci->devlist;
			matched = 0;
			while (di != NULL) {
				if (di->device_id == ntohl(ce->device_id)) {
					matched = 1;
					break;
				}

				di = di->next;
			}

			/* We don't do anything with dupe enables */
			if (matched)
				continue;

			/* Make a new record */
			di = (wispy_tcpcli_dev *) malloc(sizeof(wispy_tcpcli_dev));
			di->next = tci->devlist;
			di->device_id = ntohl(ce->device_id);
			tci->devlist = di;

		} else if (ch->command_id == WISPY_NET_COMMAND_DISABLEDEV) {
			wispy_fr_command_disabledev *cd;
			int matched = 0;

			if (ntohs(ch->frame_len) < 
				wispy_fr_command_size(wispy_fr_command_disabledev_size())) {
				fprintf(stderr, "Short disabledev frame, something is wrong, skipping\n");
				continue;
			}

			cd = (wispy_fr_command_disabledev *) ch->command_data;

			/* Shortcut removing the only device */
			if (tci->devlist != NULL && tci->devlist->device_id == ntohl(cd->device_id)) {
				free(tci->devlist);
				tci->devlist = NULL;
				continue;
			}

			di = tci->devlist;
			pdi = di;
			while (di != NULL) {
				pdi = di;
				di = di->next;

				if (di->device_id == ntohl(cd->device_id)) {
					pdi->next = di->next;
					free(di);
					break;
				}
			}

		} else if (ch->command_id == WISPY_NET_COMMAND_SETSCAN) {
			if (ntohs(ch->frame_len) < 
				wispy_fr_command_size(wispy_fr_command_setscan_size())) {
				fprintf(stderr, "Short setscan frame, something is wrong, skipping\n");
				continue;
			}
		}
	}

	return 1;
}

int wts_poll(wispy_tcpserv *wts, fd_set *rfd, fd_set *wfd, char *errstr) {
	wispy_tcpcli *tci = NULL, *tcb = NULL;
	int x = 0, r = 0;

	tci = wts->cli_list;
	while (tci != NULL) {
		tcb = tci;
		tci = tci->next;

		if (FD_ISSET(tcb->fd, wfd)) {
			int res;

			if ((res = write(tcb->fd, &(tcb->wbuf[tcb->write_pos]),
							 tcb->write_fill - tcb->write_pos)) < 0) {
				snprintf(errstr, WISPY_ERROR_MAX, "write() failed on fd %d %s",
						 tcb->fd, strerror(errno));
				wts_remove(wts, tcb, errstr);
				return 0;
			}

			tcb->write_pos += res;

			if (tcb->write_pos >= tcb->write_fill) {
				tcb->write_fill = 0;
				tcb->write_pos = 0;
			}
		}

		if (FD_ISSET(tcb->fd, rfd)) {
			int res;

			if ((res = read(tcb->fd, 
							&(tcb->rbuf[tcb->read_fill]),
							CLI_BUF_SZ - tcb->read_fill)) <= 0) {
				if (errno == EAGAIN)
					continue;

				snprintf(errstr, WISPY_ERROR_MAX, 
						 "fd %d read error %s\n", tcb->fd, strerror(errno));

				wts_remove(wts, tcb, errstr);
				return 0;
			}

			tcb->read_fill += res;

			/* Process incoming packets */
			while (tcb->read_pos < tcb->read_fill &&
				   tcb->read_fill - tcb->read_pos > wispy_fr_header_size()) {
				wispy_fr_header *frh = (wispy_fr_header *) &(tcb->rbuf[tcb->read_pos]);

				if (ntohl(frh->sentinel) != WISPY_NET_SENTINEL) {
					tcb->read_fill = 0;
					tcb->read_pos = 0;
					/* Yes, I know, 'goto'.  Go away. */
					goto read_done;
				}

				/* Look for a complete frame */
				if (ntohs(frh->frame_len) > (tcb->read_fill - tcb->read_pos)) {
					goto read_done;
				}

				/* advance to the end of the frame */
				tcb->read_pos += ntohs(frh->frame_len);

				if (frh->block_type == WISPY_NET_FRAME_COMMAND) {
					wts_handle_command(wts, tcb, frh);
				}

				/* Ignore other block types */

			}
		}

		/* Yes, goto is bad.  But it also makes sense here when we have multiply 
		 * nested loops processing data blocks */
read_done:
		/* reset the frame buffer if we've processed everything */
		if (tcb->read_pos >= tcb->read_fill) {
			tcb->read_pos = 0;
			tcb->read_fill = 0;
		}
	}

	if (FD_ISSET(wts->bindfd, rfd)) {
		if ((tci = wts_accept(wts, errstr)) == NULL)
			return -1;

		/* Send them a device block */
		if (wts_send_devblock(wts, tci, errstr) < 0)
			return -1;
	}

	for (x = 0; x < wts->ndev; x++) {
		if (wispy_get_state(&(wts->devs[x].phydev)) == WISPY_STATE_ERROR) {
			snprintf(errstr, WISPY_ERROR_MAX, "Wispy phy %d in error state: %s", 
					 x, wispy_get_error(&(wts->devs[x].phydev)));
			return -1;
		}

		if (FD_ISSET(wispy_phy_getpollfd(&(wts->devs[x].phydev)), rfd) == 0)
			continue;

		do {
			r = wispy_phy_poll(&(wts->devs[x].phydev));

			if ((r & WISPY_POLL_ERROR)) {
				snprintf(errstr, WISPY_ERROR_MAX, "Wispy phy %d poll failed: %s", 
						 x, wispy_get_error(&(wts->devs[x].phydev)));
				return -1;
			}

			if ((r & WISPY_POLL_SWEEPCOMPLETE)) {
				if (wts_send_sweepblock(wts, &(wts->devs[x].phydev),
										wispy_phy_getsweep(&(wts->devs[x].phydev)),
										errstr) < 0)
					return -1;
			}
		} while ((r & WISPY_POLL_ADDITIONAL));
	}

	return 1;
}

void wts_shutdown(wispy_tcpserv *wts) {
	wispy_tcpcli *tci = wts->cli_list;

	while (tci != NULL) {
		wispy_tcpcli *tcb = tci;
		close(tci->fd);
		tci = tci->next;
		free(tcb);
	}

	close(wts->bindfd);
}

int wts_init_bcast(char *errstr, int port) {
	int sock;
	int x;
	struct sockaddr_in lin, sin;

	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_BROADCAST;

	memset(&lin, 0, sizeof(struct sockaddr_in));
	lin.sin_family = AF_INET;
	lin.sin_port = htons(0);
	lin.sin_addr.s_addr = INADDR_ANY;

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX,
				 "Could not make bcast socket: %s", strerror(errno));
		return -1;
	}

	x = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &x, sizeof(x)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX,
				 "Could not set socket broadcast option: %s", strerror(errno));
		close(sock);
		return -1;
	}

	if (bind(sock, (struct sockaddr *) &lin, sizeof(lin)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX,
				 "Could not bind bcast socket: %s", strerror(errno));
		close(sock);
		return -1;
	}

	if (connect(sock, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX,
				 "Could not connect bcast socket: %s", strerror(errno));
		close(sock);
		return -1;
	}

	return sock;
}

int wts_send_bcast(int sock, short int port, char *errstr) {
	wispy_fr_broadcast bp;

	bp.sentinel = htonl(WISPY_NET_SENTINEL);
	bp.version = WISPY_NET_PROTO_VERSION;
	bp.server_port = htons(port);

	if (send(sock, &bp, sizeof(wispy_fr_broadcast), 0) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX,
				 "Could not send broadcast frame: %s", strerror(errno));
		return -1;
	}

	return 1;
}

void Usage() {

}

void sigcatch(int sig) {
	if (sig == SIGPIPE)
		return;
}

int main(int argc, char *argv[]) {
	wispy_tcpserv wts;
	char errstr[WISPY_ERROR_MAX];
	fd_set sel_r_fds, sel_w_fds;
	struct timeval tm;
	
	wispy_device_list list;
	wispy_tcpserv_dev *devs = NULL;
	int ndev = 0;

	int x = 0, r = 0;

	static struct option long_options[] = {
		{ "port", required_argument, 0, 'p' },
		{ "bindaddr", required_argument, 0, 'a' },
		{ "broadcast", required_argument, 0, 'b' },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};
	int option_index;

	char *bindaddr = NULL;
	short int bindport = WISPY_NET_DEFAULT_PORT;

	int broadcast = 0, bcast_sock = -1;
	time_t last_bcast = 0;

	while (1) {
		int o = getopt_long(argc, argv, "p:a:b:h",
							long_options, &option_index);

		if (o < 0)
			break;

		if (o == 'h') {
			Usage();
			exit(-1);
		} else if (o == 'a') {
			bindaddr = strdup(optarg);
			continue;
		} else if (o == 'p') {
			if (sscanf(optarg, "%hd", &bindport) != 1) {
				fprintf(stderr, "Expected port number\n");
				Usage();
				exit(-1);
			}
		} else if (o == 'b') {
			if (sscanf(optarg, "%d", &broadcast) != 1) {
				fprintf(stderr, "Expected broadcast time in seconds\n");
				Usage();
				exit(-1);
			}
		}
	}

	ndev = wispy_device_scan(&list);

	if (ndev <= 0) {
		printf("No wispy devices found, bailing\n");
		exit(1);
	}

	signal(SIGPIPE, &sigcatch);

	fprintf(stderr, "Found %d wispy devices...\n", ndev);

	/* devs = (wispy_phy *) malloc(WISPY_PHY_SIZE * ndev); */
	devs = (wispy_tcpserv_dev *) malloc(sizeof(wispy_tcpserv_dev) * ndev);

	for (x = 0; x < ndev; x++) {
		fprintf(stderr, "Initializing WiSPY device %s id %u\n", 
				list.list[x].name, list.list[x].device_id);

		devs[x].lock_fd = -1;

		if (wispy_device_init(&(devs[x].phydev), &(list.list[x])) < 0) {
			fprintf(stderr, "Error initializing WiSPY device %s id %u\n",
					list.list[x].name, list.list[x].device_id);
			fprintf(stderr, "%s\n", wispy_get_error(&(devs[x].phydev)));
			exit(1);
		}

		if (wispy_phy_open(&(devs[x].phydev)) < 0) {
			fprintf(stderr, "Error opening WiSPY device %s id %u\n",
					list.list[x].name, list.list[x].device_id);
			fprintf(stderr, "%s\n", wispy_get_error(&(devs[x].phydev)));
			exit(1);
		}

		wispy_phy_setcalibration(&(devs[x].phydev), 1);
	}
	wispy_device_scan_free(&list);

	wts_init(&wts);

	if (broadcast > 0) {
		if ((bcast_sock = wts_init_bcast(errstr, bindport)) < 0) {
			fprintf(stderr, "Broadcast init failed: %s\n", errstr);
			exit(1);
		}

		last_bcast = time(0);
	}

	wts.devs = devs;
	wts.ndev = ndev;

	if (wts_bind(&wts, bindaddr, bindport, errstr) < 0) {
		fprintf(stderr, "TCP bind failed: %s\n", errstr);
		exit(1);
	}

	fprintf(stderr, "TCP server listening on %s port %hd\n",
			bindaddr == NULL ? "(any)" : bindaddr,
			bindport);

	if (broadcast) {
		fprintf(stderr, "Broadcast server announcing on port %hd, %d seconds\n",
				bindport, broadcast);
	}

	while (1) {
		FD_ZERO(&sel_r_fds);
		FD_ZERO(&sel_w_fds);

		wts_fdset(&wts, &sel_r_fds, &sel_w_fds);

		tm.tv_sec = 0;
		tm.tv_usec = 100000;

		if (broadcast > 0 && time(0) - last_bcast > broadcast) {
			if (wts_send_bcast(bcast_sock, bindport, errstr) < 0) {
				fprintf(stderr, "Sending broadcast packet failed, %s\n", errstr);
				wts_shutdown(&wts);
				exit(1);
			}

			last_bcast = time(0);
		}

		if (select(wts.maxfd + 1, &sel_r_fds, &sel_w_fds, NULL, &tm) < 0) {
			fprintf(stderr, "Select() failed: %s\n", strerror(errno));
			wts_shutdown(&wts);
			exit(1);
		}

		if (wts_poll(&wts, &sel_r_fds, &sel_w_fds, errstr) < 0) {
			fprintf(stderr, "Polling failed: %s\n", errstr);
			wts_shutdown(&wts);
			exit(1);
		}
	}
}

