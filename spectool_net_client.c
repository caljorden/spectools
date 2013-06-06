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

#include <sys/types.h>
#include <sys/uio.h>

#include "config.h"
#include "spectool_net_client.h"

int spectool_netcli_init(spectool_server *sr, char *url, char *errstr) {
	int ret;

	sr->sock = -1;
	sr->hostname[0] = '\0';
	sr->port = 0;
	sr->url = NULL;

	sr->host = NULL;

	sr->bufferwrite = 1;

	memset(sr->wbuf, 0, CLI_BUF_SZ);
	memset(sr->rbuf, 0, CLI_BUF_SZ);

	sr->write_pos = 0;
	sr->read_pos = 0;
	sr->write_fill = 0;
	sr->read_fill = 0;

	sr->devlist = NULL;

	sr->state = SPECTOOL_NET_STATE_NONE;

	if ((ret = sscanf(url, "tcp://%256[^:]:%hd", sr->hostname, 
					  &(sr->port))) == 1) {
		sr->port = SPECTOOL_NET_DEFAULT_PORT;
	} else if (ret != 2) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Could not parse server URL, expected "
				 "tcp://host:port");
		sr->state = SPECTOOL_NET_STATE_ERROR;
		return -1;
	}

	sr->host = gethostbyname(sr->hostname);

	if (sr->host == NULL) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Could not resolve host '%s'",
				 sr->hostname);
		sr->state = SPECTOOL_NET_STATE_ERROR;
		return -1;
	}

	memcpy((char *) &(sr->conaddr), sr->host->h_addr_list[0],
		   sizeof(unsigned int) < sr->host->h_length ? 
		   		sizeof(unsigned int) : sr->host->h_length);

	sr->url = strdup(url);

	return 1;
}

int spectool_netcli_connect(spectool_server *sr, char *errstr) {
	int save_mode;

	struct sockaddr_in servaddr, localaddr;
	
	memset(&servaddr, 0, sizeof(struct sockaddr_in));
	servaddr.sin_family = sr->host->h_addrtype;
	memcpy((char *) &servaddr.sin_addr.s_addr,
		   sr->host->h_addr_list[0], sr->host->h_length);
	servaddr.sin_port = htons(sr->port);

	if ((sr->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Failed to create socket to server: %s",
				 strerror(errno));
		sr->state = SPECTOOL_NET_STATE_ERROR;
		return -1;
	}

	memset(&localaddr, 0, sizeof(struct sockaddr_in));
	localaddr.sin_family = AF_INET;
	localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	localaddr.sin_port = htons(0);

	if (bind(sr->sock, (struct sockaddr *) &localaddr, sizeof(localaddr)) < 0) {
		close(sr->sock);
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Failed to bind socket to server: %s",
				 strerror(errno));
		sr->state = SPECTOOL_NET_STATE_ERROR;
		return -1;
	}

	if (connect(sr->sock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		close(sr->sock);
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Failed to connect socket to server: %s",
				 strerror(errno));
		sr->state = SPECTOOL_NET_STATE_ERROR;
		return -1;
	}

	save_mode = fcntl(sr->sock, F_GETFL, 0);
	fcntl(sr->sock, F_SETFL, save_mode | O_NONBLOCK);

	sr->state = SPECTOOL_NET_STATE_CONNECTED;

	return 1;
}

int spectool_netcli_close(spectool_server *sr) {
	spectool_net_dev *di;

	while (sr->devlist != NULL) {
		di = sr->devlist->next;
		free(sr->devlist);
		sr->devlist = di;
	}

	if (sr->sock >= 0)
		close(sr->sock);

	if (sr->url != NULL)
		free(sr->url);

	return 1;
}

int spectool_netcli_getstate(spectool_server *sr) {
	return sr->state;
}

char *spectool_netcli_geturl(spectool_server *sr) {
	return sr->url;
}

int spectool_netcli_getpollfd(spectool_server *sr) {
	return sr->sock;
}

int spectool_netcli_getwritefd(spectool_server *sr) {
	return sr->sock;
}

unsigned int spectool_netcli_getaddr(spectool_server *sr) {
	return sr->conaddr;
}

unsigned short int spectool_netcli_getport(spectool_server *sr) {
	return sr->port;
}

void spectool_netcli_setbufferwrite(spectool_server *sr, int buf) {
	sr->bufferwrite = buf;
}

int spectool_netcli_getwritepend(spectool_server *sr) {
	return (sr->write_pos < sr->write_fill);
}

int spectool_netcli_poll(spectool_server *sr, char *errstr) {
	spectool_fr_header *header;
	int res;
	int ret = 0;

	if (sr->sock < 0)
		return -1;

	while (sr->read_fill - sr->read_pos >= spectool_fr_header_size()) {
		header = (spectool_fr_header *) &(sr->rbuf[sr->read_pos]);

		/* Nuke the buffer entirely and start over if we can't find a 
		 * sentinel */
		if (ntohl(header->sentinel) != SPECTOOL_NET_SENTINEL) {
			sr->read_fill = 0;
			sr->read_pos = 0;
			return 0;
		}

		if (ntohs(header->frame_len) > (sr->read_fill - sr->read_pos)) {
			return 0;
		}

		sr->read_pos += ntohs(header->frame_len);

		/* If we've finished the packet reset the buffer to the beginning */
		if (sr->read_pos >= sr->read_fill) {
			sr->read_pos = 0;
			sr->read_fill = 0;
		}

		/* We only care about device and sweeps, the rest get ignored (for now) */
		if (header->block_type == SPECTOOL_NET_FRAME_DEVICE) {
			if ((res = spectool_netcli_block_netdev(sr, header, errstr)) < 0) {
				return -1;
			}

			if (res > 0) {
				ret |= SPECTOOL_NETCLI_POLL_NEWDEVS;
			}

		} else if (header->block_type == SPECTOOL_NET_FRAME_SWEEP) {
			if ((res = spectool_netcli_block_sweep(sr, header, errstr)) < 0) {
				return -1;
			}

			if (res > 0) {
				ret |= SPECTOOL_NETCLI_POLL_NEWSWEEPS;
			}
		}
	}

	/* Read as much as we can */
	if ((res = read(sr->sock, &(sr->rbuf[sr->read_fill]),
					CLI_BUF_SZ - sr->read_fill)) <= 0) {
		if (errno == EAGAIN)
			return ret;

		snprintf(errstr, SPECTOOL_ERROR_MAX, "Failed to read from socket: %s",
				 strerror(errno));
		sr->state == SPECTOOL_NET_STATE_ERROR;
		return -1;
	}

	sr->read_fill += res;

	if (res > 0) {
		ret |= SPECTOOL_NETCLI_POLL_ADDITIONAL;
	}

	return ret;
}

int spectool_netcli_writepoll(spectool_server *sr, char *errstr) {
	int res;

	if ((res = write(sr->sock, &(sr->wbuf[sr->write_pos]),
					 sr->write_fill - sr->write_pos)) < 0) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, "write() failed on %s",
				 strerror(errno));
		return -1;
	}

	sr->write_pos += res;

	if (sr->write_pos >= sr->write_fill) {
		sr->write_fill = 0;
		sr->write_pos = 0;
	}

	return 1;
}

int spectool_netcli_block_netdev(spectool_server *sr, spectool_fr_header *header,
								 char *errstr) {
	spectool_fr_device *dev;
	spectool_net_dev *sni;
	int bsize = ntohs(header->frame_len) - spectool_fr_header_size();

	int num_devices;
	int x;

	num_devices = header->num_blocks;

	for (x = 0; x < header->num_blocks; x++) {
		/* If we can't fit, bail.  In the future this will have to be 
		 * rewritten to handle variable sized devices, maybe */
		if (bsize < spectool_fr_device_size() * (x + 1)) {
			return -1;
		}

		dev = (spectool_fr_device *) &(header->data[spectool_fr_device_size() * x]);

		/* Is this the last device? */
		if (dev->device_version == SPECTOOL_NET_DEVTYPE_LASTDEV) {
			sr->state == SPECTOOL_NET_STATE_CONFIGURED;
			return 1;
		}

		/* Does this device exist in the list?  If it does, just update it,
		 * otherwise we need to make a new one */
		sni = sr->devlist;
		while (sni != NULL) {
			if (ntohl(dev->device_id) == sni->device_id)
				break;

			sni = sni->next;
		}

		if (sni == NULL) {
			sni = (spectool_net_dev *) malloc(sizeof(spectool_net_dev));
			sni->phydev = NULL;
			sni->next = sr->devlist;
			sr->devlist = sni;
		}

		sni->device_version = dev->device_version;
		sni->device_flags = ntohs(dev->device_flags);
		sni->device_id = ntohl(dev->device_id);

		snprintf(sni->device_name, 256, "%s", dev->device_name);
		sni->device_name[dev->device_name_len] = '\0';

		sni->amp_offset_mdbm = ntohl(dev->amp_offset_mdbm) * -1;
		sni->amp_res_mdbm = ntohl(dev->amp_res_mdbm);
		sni->rssi_max = ntohs(dev->rssi_max);

		sni->def_start_khz = ntohl(dev->def_start_khz);
		sni->def_res_hz = ntohl(dev->def_res_hz);
		sni->def_num_samples = ntohs(dev->def_num_samples);

		sni->start_khz = ntohl(dev->start_khz);
		sni->res_hz = ntohl(dev->res_hz);
		sni->num_samples = ntohs(dev->num_samples);
	}

	return 0;
}

int spectool_netcli_block_sweep(spectool_server *sr, spectool_fr_header *header,
								char *errstr) {
	spectool_fr_sweep *sweep;
	int x, y;
	int bsize = ntohs(header->frame_len) - spectool_fr_header_size();
	int pos = 0;
	spectool_net_dev *sni;
	spectool_sample_sweep *auxsweep;

	for (x = 0; x < header->num_blocks; x++) {
		sweep = (spectool_fr_sweep *) &(header->data[pos]);

		if (bsize - pos < 2) {
			/* way too short, bail with error */
			snprintf(errstr, SPECTOOL_ERROR_MAX, "Got runt sweep frame, bailing");
			return -1;
		}

		if (ntohs(sweep->frame_len) < spectool_fr_sweep_size(0)) {
			/* Again, too short */
			snprintf(errstr, SPECTOOL_ERROR_MAX, "Got runt sweep frame, bailing");
			return -1;
		}

		sni = sr->devlist;
		while (sni != NULL) {
			if (ntohl(sweep->device_id) == sni->device_id)
				break;

			sni = sni->next;
		}

		if (sni == NULL) {
			snprintf(errstr, SPECTOOL_ERROR_MAX, "Got sweep frame for device which "
					 "was not advertised, discarding");
			return -1;
		}

		if (ntohs(sweep->frame_len) < 
			spectool_fr_sweep_size(sni->num_samples)) {
			snprintf(errstr, SPECTOOL_ERROR_MAX, "Got sweep frame too small to hold "
					 "indicated number of samples, bailing - %u samples %u < %u",
					 sni->num_samples,
					 ntohs(sweep->frame_len), 
					 spectool_fr_sweep_size(sni->num_samples));
			return -1;
		}


		pos += htons(sweep->frame_len);

		/* For now we don't bother tracking data for devices which don't have a 
		 * phydev linked to them -- we shouldn't get it, anyhow */
		if (sni->phydev == NULL)
			continue;

		if ((auxsweep = ((spectool_net_dev_aux *) (sni->phydev->auxptr))->sweep) != NULL) {
			free(auxsweep);
		}

		auxsweep = 
			(spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(sni->num_samples));

		/* Copy data out of our device record (this will change later when the
		 * spectool internals change) */
		auxsweep->start_khz = sni->start_khz;
		auxsweep->res_hz = sni->res_hz;
		auxsweep->num_samples = sni->num_samples;
		auxsweep->end_khz =
			((auxsweep->res_hz / 1000) * auxsweep->num_samples) +
			auxsweep->start_khz;

		auxsweep->amp_offset_mdbm = sni->amp_offset_mdbm;
		auxsweep->amp_res_mdbm = sni->amp_res_mdbm;
		auxsweep->rssi_max = sni->rssi_max;

		/* Lock start and end to the same */
		auxsweep->tm_start.tv_sec =
			auxsweep->tm_end.tv_sec =
			ntohl(sweep->start_sec);
		auxsweep->tm_start.tv_usec =
			auxsweep->tm_end.tv_usec =
			ntohl(sweep->start_usec);

		/* Copy the RSSI data */
		for (y = 0; y < sni->num_samples; y++) {
			auxsweep->sample_data[y] = sweep->sample_data[y];

			if (sni->phydev->min_rssi_seen > sweep->sample_data[y])
				sni->phydev->min_rssi_seen = sweep->sample_data[y];
		}

		auxsweep->min_rssi_seen = sni->phydev->min_rssi_seen;

		auxsweep->phydev = sni->phydev;

		/* Flag that we got a new frame */
		((spectool_net_dev_aux *) (sni->phydev->auxptr))->new_sweep = 1;
		((spectool_net_dev_aux *) (sni->phydev->auxptr))->sweep = auxsweep;
		write(((spectool_net_dev_aux *) (sni->phydev->auxptr))->spipe[1], "0", 1);
	}

	return 1;
}

int spectool_netcli_append(spectool_server *sr, uint8_t *data, int len, char *errstr) {
	if (sr->bufferwrite == 0) {
		if (write(sr->sock, data, len) < 0) {
			snprintf(errstr, SPECTOOL_ERROR_MAX, "write() failed on %s",
					 strerror(errno));
			return -1;
		}

		return 1;
	}

	if (sr->write_fill + len >= CLI_BUF_SZ) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Network client write buffer can't "
				 "fit %d bytes, %d of %d full", len, sr->write_fill, CLI_BUF_SZ);
		return -1;
	}

	memcpy(&(sr->wbuf[sr->write_fill]), data, len);
	sr->write_fill += len;

	return 1;
}

spectool_phy *spectool_netcli_enabledev(spectool_server *sr, unsigned int dev_id,
									 char *errstr) {
	spectool_phy *phyret;
	spectool_net_dev_aux *aux; 
	spectool_net_dev *sni;
	spectool_fr_header *header;
	spectool_fr_command *cmd;
	spectool_fr_command_enabledev *cmde;
	int sz;

	sni = sr->devlist;
	while (sni != NULL) {
		if (dev_id == sni->device_id)
			break;

		sni = sni->next;
	}

	if (sni == NULL) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, "Could not find device %u in list "
				 "from server.", dev_id);
		return NULL;
	}

	if (sni->phydev != NULL)
		return sni->phydev;

	sz = spectool_fr_header_size() +
		spectool_fr_command_size(spectool_fr_command_enabledev_size(0));

	header = (spectool_fr_header *) malloc(sz);
										
	cmd = (spectool_fr_command *) header->data;
	cmde = (spectool_fr_command_enabledev *) cmd->command_data;

	header->sentinel = htonl(SPECTOOL_NET_SENTINEL);
	header->frame_len = htons(sz);
	header->proto_version = SPECTOOL_NET_PROTO_VERSION;
	header->block_type = SPECTOOL_NET_FRAME_COMMAND;
	header->num_blocks = 1;

	cmd->frame_len = 
		htons(spectool_fr_command_size(spectool_fr_command_enabledev_size(0)));
	cmd->command_id = SPECTOOL_NET_COMMAND_ENABLEDEV;
	cmd->command_len = htons(spectool_fr_command_enabledev_size(0));

	cmde->device_id = htonl(dev_id);

	if (spectool_netcli_append(sr, (uint8_t *) header, sz, errstr) < 0) {
		free(header);
		return NULL;
	}

	free(header);

	phyret = (spectool_phy *) malloc(SPECTOOL_PHY_SIZE);
	aux = (spectool_net_dev_aux *) malloc(sizeof(spectool_net_dev_aux));
	phyret->auxptr = aux;

	aux->sweep = NULL;
	aux->new_sweep = 0;

	pipe(aux->spipe);
	fcntl(aux->spipe[0], F_SETFL, fcntl(aux->spipe[0], F_GETFL, 0) | O_NONBLOCK);

	phyret->device_spec = (spectool_dev_spec *) malloc(sizeof(spectool_dev_spec));

	phyret->state = SPECTOOL_STATE_CONFIGURING;
	phyret->min_rssi_seen = -1;

	phyret->device_spec->device_id = sni->device_id;
	phyret->device_spec->device_version = sni->device_version;
	phyret->device_spec->device_flags = sni->device_flags;

	phyret->device_spec->num_sweep_ranges = 1;

	phyret->device_spec->supported_ranges =
		(spectool_sample_sweep *) malloc(SPECTOOL_SWEEP_SIZE(0));

	phyret->device_spec->supported_ranges[0].num_samples = sni->def_num_samples;
	phyret->device_spec->supported_ranges[0].amp_offset_mdbm = sni->amp_offset_mdbm;
	phyret->device_spec->supported_ranges[0].amp_res_mdbm = sni->amp_res_mdbm;
	phyret->device_spec->supported_ranges[0].rssi_max = sni->rssi_max;

	phyret->device_spec->supported_ranges[0].start_khz = sni->start_khz;
	phyret->device_spec->supported_ranges[0].end_khz =
			((sni->res_hz / 1000) * sni->num_samples) +
			sni->start_khz;
	phyret->device_spec->supported_ranges[0].res_hz = sni->res_hz;

	phyret->device_spec->default_range = phyret->device_spec->supported_ranges;

	phyret->device_spec->cur_profile = 0;

	phyret->open_func = &spectool_net_open;
	phyret->close_func = &spectool_net_close;
	phyret->poll_func = &spectool_net_poll;
	phyret->pollfd_func = &spectool_net_getpollfd;
	phyret->setcalib_func = &spectool_net_setcalibration;
	phyret->getsweep_func = &spectool_net_getsweep;
	phyret->setposition_func = &spectool_net_setposition;

	phyret->draw_agg_suggestion = 1;

	snprintf(phyret->device_spec->device_name, SPECTOOL_PHY_NAME_MAX, 
			 "%s", sni->device_name);

	sni->phydev = phyret;

	return phyret;
}

int spectool_netcli_disabledev(spectool_server *sr, spectool_phy *dev) {
	spectool_net_dev_aux *aux; 
	spectool_net_dev *sni;
	spectool_fr_header *header;
	spectool_fr_command *cmd;
	spectool_fr_command_disabledev *cmdd;
	char errstr[SPECTOOL_ERROR_MAX];
	int sz;

	sni = sr->devlist;
	while (sni != NULL) {
		if (dev == sni->phydev)
			break;

		sni = sni->next;
	}

	if (sni == NULL) {
		return -1;
	}

	if (sni->phydev == NULL)
		return -1;

	sz = spectool_fr_header_size() +
		spectool_fr_command_size(spectool_fr_command_disabledev_size(0));

	header = (spectool_fr_header *) malloc(sz);
										
	cmd = (spectool_fr_command *) header->data;
	cmdd = (spectool_fr_command_disabledev *) cmd->command_data;

	header->sentinel = htonl(SPECTOOL_NET_SENTINEL);
	header->frame_len = htons(sz);
	header->proto_version = SPECTOOL_NET_PROTO_VERSION;
	header->block_type = SPECTOOL_NET_FRAME_COMMAND;
	header->num_blocks = 1;

	cmd->frame_len = 
		htons(spectool_fr_command_size(spectool_fr_command_disabledev_size(0)));
	cmd->command_id = SPECTOOL_NET_COMMAND_DISABLEDEV;
	cmd->command_len = htons(spectool_fr_command_disabledev_size(0));

	cmdd->device_id = htonl(sni->device_id);

	free(dev->auxptr);
	free(dev);
	sni->phydev = NULL;

	if (spectool_netcli_append(sr, (uint8_t *) header, sz, errstr) < 0) {
		free(header);
		return -1;
	}

	free(header);

	return 1;
}

void spectool_net_setcalibration(spectool_phy *phydev, int in_calib) {
	return;
}

int spectool_net_poll(spectool_phy *phydev) {
	int ret = SPECTOOL_POLL_NONE;
	char junk[8];

	read(((spectool_net_dev_aux *) phydev->auxptr)->spipe[0], junk, 8);

	if (phydev->state == SPECTOOL_STATE_CONFIGURING) {
		ret |= SPECTOOL_POLL_CONFIGURED;
		ret |= SPECTOOL_POLL_ADDITIONAL;
		phydev->state = SPECTOOL_STATE_RUNNING;
		return ret;
	}

	if (((spectool_net_dev_aux *) phydev->auxptr)->new_sweep) {
		((spectool_net_dev_aux *) phydev->auxptr)->new_sweep = 0;
		ret |= SPECTOOL_POLL_SWEEPCOMPLETE;
	}

	return ret;
}

int spectool_net_getpollfd(spectool_phy *phydev) {
	 return ((spectool_net_dev_aux *) phydev->auxptr)->spipe[0];
}

int spectool_net_open(spectool_phy *phydev) {
	return 1;
}

int spectool_net_close(spectool_phy *phydev) {
	/* TODO - fill this in w/a close */
	close(((spectool_net_dev_aux *) phydev->auxptr)->spipe[0]);
	close(((spectool_net_dev_aux *) phydev->auxptr)->spipe[1]);
	return 1;
}

spectool_sample_sweep *spectool_net_getsweep(spectool_phy *phydev) {
	return ((spectool_net_dev_aux *) phydev->auxptr)->sweep;
}

int spectool_net_setposition(spectool_phy *phydev, int in_profile, 
							 int start_khz, int res_hz) {
	/* todo - fill this in */
	return 1;
}

int spectool_netcli_initbroadcast(short int port, char *errstr) {
	struct sockaddr_in lsin;
	int sock;
	int x;

	memset(&lsin, 0, sizeof(struct sockaddr_in));
	lsin.sin_family = AF_INET;
	lsin.sin_port = htons(port);
	lsin.sin_addr.s_addr = INADDR_ANY;

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		snprintf(errstr, SPECTOOL_ERROR_MAX, 
				 "netcli broadcast listen socket failed: %s", strerror(errno));
		return -1;
	}

	/* we don't seem to need this in listen mode?
	x = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &x, sizeof(x)) < 0) {
		snprintf(errstr, SPECTOOL_ERROR_MAX,
				 "netcli broadcast listen socket bcast sockopt failed: %s",
				 strerror(errno));
		close(sock);
		return -1;
	}
	*/

	fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

	if (bind(sock, (struct sockaddr *) &lsin, sizeof(lsin)) < 0) {
		snprintf(errstr, SPECTOOL_ERROR_MAX,
				 "netcli broadcast listen socket bind failed: %s",
				 strerror(errno));
		close(sock);
		return -1;
	}

	return sock;
}

int spectool_netcli_pollbroadcast(int sock, char *ret_url, char *errstr) {
	struct msghdr rcv_msg;
	struct iovec iov;
	spectool_fr_broadcast buf;
	struct sockaddr_in recv_addr;

	iov.iov_base = &buf;
	iov.iov_len = sizeof(spectool_fr_broadcast);

	rcv_msg.msg_name = &recv_addr;
	rcv_msg.msg_namelen = sizeof(recv_addr);
	rcv_msg.msg_iov = &iov;
	rcv_msg.msg_iovlen = 1;
	rcv_msg.msg_control = NULL;
	rcv_msg.msg_controllen = 0;

	if (recvmsg(sock, &rcv_msg, 0) < 0) {
		snprintf(errstr, SPECTOOL_ERROR_MAX,
				 "netcli broadcast recv failed: %s", strerror(errno));
		return -1;
	}

	if (ntohl(buf.sentinel) == SPECTOOL_NET_SENTINEL) {
		snprintf(ret_url, SPECTOOL_NETCLI_URL_MAX, "tcp://%s:%hu",
				 inet_ntoa(recv_addr.sin_addr), ntohs(buf.server_port));
		return 1;
	}

	return 0;
}

