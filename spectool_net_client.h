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

#ifndef __WISPY_NET_CLIENT_H__
#define __WISPY_NET_CLIENT_H__

#include "config.h"

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

#include "spectool_container.h"
#include "spectool_net.h"

#define CLI_BUF_SZ				16384

#define SPECTOOL_NETCLI_URL_MAX	300

/* Aux struct holding network device info.  In many ways a duplicate of the
 * phydev dev spec, however this has to hold annotations for devies which
 * are advertised but not activated */
typedef struct _spectool_net_dev {
	unsigned int device_version;
	unsigned int device_flags;

	unsigned int device_id;

	char device_name[256];

	int amp_offset_mdbm;
	unsigned int amp_res_mdbm;
	unsigned int rssi_max;

	unsigned int def_start_khz;
	unsigned int def_res_hz;
	unsigned int def_num_samples;

	unsigned int start_khz;
	unsigned int res_hz;
	unsigned int num_samples;

	/* Local attributes if we're an activated device */
	wispy_phy *phydev;

	struct _spectool_net_dev *next;
} spectool_net_dev;

/* Advertised servers we've seen & the info we have about them */
typedef struct _spectool_adv_server {
	unsigned int proto_version;

	struct sockaddr addr;
	socklen_t addr_len;

	unsigned short int port;

	time_t last_advertised;

	struct _spectool_adv_server *next;
} spectool_adv_server;

/* Struct that handles tracking a server we've connected to */
typedef struct _spectool_server {
	int sock;

	char *url;

	char hostname[256];
	short unsigned int port;

	unsigned int conaddr;

	struct hostent *host;

	int bufferwrite;

	uint8_t wbuf[CLI_BUF_SZ];
	uint8_t rbuf[CLI_BUF_SZ];

	int write_pos, read_pos, write_fill, read_fill;

	int state;

	spectool_net_dev *devlist;
} spectool_server;

#define SPECTOOL_NET_STATE_NONE			0
#define SPECTOOL_NET_STATE_CONNECTED	1
#define SPECTOOL_NET_STATE_CONFIGURED	2
#define SPECTOOL_NET_STATE_ERROR		255

/* Auxptr struct attached to a wispy_net phydev */
typedef struct _wispy_net_dev_aux {
	spectool_server *server;
	spectool_net_dev *netdev;
	wispy_sample_sweep *sweep;
	int new_sweep;
	int spipe[2];
} wispy_net_dev_aux;

/* Server manipulation commands - one server can have many phydevs linked to it,
 * and an app can conceivably have many servers. */
int spectool_netcli_init(spectool_server *sr, char *url, char *errstr);
int spectool_netcli_connect(spectool_server *sr, char *errstr);
int spectool_netcli_close(spectool_server *sr);
unsigned int spectool_netcli_getaddr(spectool_server *sr);
unsigned short int spectool_netcli_getport(spectool_server *sr);
char *spectool_netcli_geturl(spectool_server *sr);
int spectool_netcli_getstate(spectool_server *sr);
int spectool_netcli_getpollfd(spectool_server *sr);
void spectool_netcli_setbufferwrite(spectool_server *sr, int buf);
int spectool_netcli_getwritepend(spectool_server *sr);
int spectool_netcli_getwritefd(spectool_server *sr);
int spectool_netcli_poll(spectool_server *sr, char *errstr);
wispy_phy *spectool_netcli_enabledev(spectool_server *sr, unsigned int dev_id,
									 char *errstr);
int spectool_netcli_disabledev(spectool_server *sr, wispy_phy *dev);

/* Initialize a broadcast listening socket, retval is the socket */
int spectool_netcli_initbroadcast(short int port, char *errstr);
/* Poll a listening socket, and return a host URL if we found one,
 * expected to fit spectool_url_max */
int spectool_netcli_pollbroadcast(int sock, char *ret_url, char *errstr);

/* Return mask from poll */
#define SPECTOOL_NETCLI_POLL_NONE			0
/* new devices have been detected */
#define SPECTOOL_NETCLI_POLL_NEWDEVS		1
/* additional data has been read, poll should be called again */
#define SPECTOOL_NETCLI_POLL_ADDITIONAL		2
/* sweep data has been read, devices should be checked */
#define SPECTOOL_NETCLI_POLL_NEWSWEEPS		4

/* Parsers */
int spectool_netcli_block_netdev(spectool_server *sr, wispy_fr_header *header,
								 char *errstr);
int spectool_netcli_block_sweep(spectool_server *sr, wispy_fr_header *header,
								char *errstr);
/* Block management */
int spectool_netcli_append(spectool_server *sr, uint8_t *data, 
						   int len, char *errstr);

/* Phydev hooks */
void spectool_net_setcalibration(wispy_phy *phydev, int in_calib);
int spectool_net_poll(wispy_phy *phydev);
int spectool_net_getpollfd(wispy_phy *phydev);
int spectool_net_open(wispy_phy *phydev);
int spectool_net_close(wispy_phy *phydev);
wispy_sample_sweep *spectool_net_getsweep(wispy_phy *phydev);
int spectool_net_setposition(wispy_phy *phydev, int start_khz, int res_hz);
wispy_sample_sweep *spectool_net_getsweep(wispy_phy *phydev);

#endif

