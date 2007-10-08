
// include kismet headers
#include <config.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <sstream>

#include <globalregistry.h>
#include <plugintracker.h>
#include <timetracker.h>
#include <kis_netframe.h>

extern "C" {
#include "wispy_container.h"
#include "wispy_hw_gen1.h"
}

class KisWispyCore : public Pollable {
public:
	KisWispyCore() { fprintf(stderr, "FATAL OOPS: KisWispyCore()\n"); exit(1); }
	KisWispyCore(GlobalRegistry *in_globalreg);
	virtual ~KisWispyCore();

	virtual unsigned int MergeSet(unsigned int in_max_fd, fd_set *out_rset,
								  fd_set *out_wset);
	virtual int Poll(fd_set& in_rset, fd_set& in_wset);

	virtual void BlitDevices(int in_fd);
	virtual void BlitSweeps(int in_fd);

	virtual int ActivateHw();
	virtual int DeactivateHw();

	virtual int ActivateHwCmd(CLIENT_PARMS);
	virtual int DeactivateHwCmd(CLIENT_PARMS);

	typedef struct {
		wispy_phy *phydev;
		int blit;
		vector<wispy_sweep_cache *> sweepcache;
	} phy_sweep;

protected:
	vector<phy_sweep> physweep_vec;

	int act_cmdid, deact_cmdid;
};

// Wispy network proto reference
int wispydproto_ref = -1;
int wispysproto_ref = -1;
int wispytimerid = -1;

// Core class
KisWispyCore *wcore;

int wispy_register(GlobalRegistry *);
int wispy_unregister(GlobalRegistry *);
int wispy_timer(TIMEEVENT_PARMS);

// This has to be an extern "c" decl for the symbols to be found
extern "C" {
	int kis_plugin_info(plugin_usrdata *data) {
		data->pl_name = "WiSPY";
		data->pl_version = "1.0.0";
		data->pl_description = "Provides WiSPY device support for Kismet";
		data->pl_unloadable = 1;
		data->plugin_register = wispy_register;
		data->plugin_unregister = wispy_unregister;

		return 1;
	}
}

// Device protocol
enum WISPYDPROTO_fields {
	WISPYDPROTO_id, WISPYDPROTO_name, WISPYDPROTO_version, WISPYDPROTO_flags,
	WISPYDPROTO_maxfield
};
char *WISPYDPROTO_fields_text[] = {
	"id", "name", "version", "flags",
	NULL
};

// Sweep protocol
enum WISPYSPROTO_fields {
	WISPYSPROTO_devid, WISPYSPROTO_startkhz, WISPYSPROTO_endkhz, WISPYSPROTO_reskhz,
	WISPYSPROTO_minsample, WISPYSPROTO_minsigrep, WISPYSPROTO_maxsample,
	WISPYSPROTO_numsamples,
	WISPYSPROTO_startsec, WISPYSPROTO_startusec, WISPYSPROTO_endsec, WISPYSPROTO_endusec,
	WISPYSPROTO_lastsamples, WISPYSPROTO_peaksamples, WISPYSPROTO_avgsamples,
	WISPYSPROTO_maxfield
};
char *WISPYSPROTO_fields_text[] = {
	"devid", "startkhz", "endkhz", "reskhz",
	"minsample", "minsigrep", "maxsample", "numsamples",
	"startsec", "startusec", "endsec", "endusec",
	"lastsamples", "peaksamples", "avgsamples",
	NULL
};

int Protocol_WISPYDPROTO(PROTO_PARMS) {
	wispy_phy *wphy = (wispy_phy *) data;
	ostringstream osstr;

	// If it isn't configured, don't blit it
	if (wispy_get_state(wphy) <= WISPY_STATE_CONFIGURING)
		return 0;

	cache->Filled(field_vec->size());

	for (unsigned int x = 0; x < field_vec->size(); x++) {
		unsigned int fnum = (*field_vec)[x];

		if (fnum >= WISPYDPROTO_maxfield) {
			out_string = "Unknown field requests";
			return -1;
		}

		osstr.str("");

		if (cache->Filled(fnum)) {
			out_string += cache->GetCache(fnum) + " ";
			continue;
		}

		switch(fnum) {
			case WISPYDPROTO_id:
				osstr << wphy->device_spec->device_id;
				cache->Cache(fnum, osstr.str());
				break;
			case WISPYDPROTO_name:
				cache->Cache(fnum, 
							 "\001" + string(wphy->device_spec->device_name) + "\001");
				break;
			case WISPYDPROTO_version:
				osstr << (int) wphy->device_spec->device_version;
				cache->Cache(fnum, osstr.str());
				break;
			case WISPYDPROTO_flags:
				osstr << (int) wphy->device_spec->device_flags;
				cache->Cache(fnum, osstr.str());
				break;
		}

		out_string += cache->GetCache(fnum) + " ";
	}

	return 1;
}

void Protocol_WISPYDPROTO_Enable(PROTO_ENABLE_PARMS) {
	((KisWispyCore *) data)->BlitDevices(in_fd);
	return;
}

int Protocol_WISPYSPROTO(PROTO_PARMS) {
	wispy_sweep_cache *wswp = (wispy_sweep_cache *) data;
	ostringstream osstr;

	cache->Filled(field_vec->size());

	for (unsigned int x = 0; x < field_vec->size(); x++) {
		unsigned int fnum = (*field_vec)[x];

		if (fnum >= WISPYSPROTO_maxfield) {
			out_string = "Unknown field requests";
			return -1;
		}

		osstr.str("");

		if (cache->Filled(fnum)) {
			out_string += cache->GetCache(fnum) + " ";
			continue;
		}

		switch(fnum) {
			case WISPYSPROTO_devid:
				osstr << wswp->device_id;
				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_startkhz:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->start_khz;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_endkhz:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->end_khz;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_reskhz:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->res_khz;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_minsample:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->min_sample;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_minsigrep:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->min_sig_report;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_maxsample:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->max_sample;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_numsamples:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->num_samples;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_startsec:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->tm_start.tv_sec;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_startusec:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->tm_start.tv_usec;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_endsec:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->tm_end.tv_sec;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_endusec:
				if (wswp->avg == NULL)
					osstr.str("0");
				else
					osstr << wswp->avg->tm_end.tv_usec;

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_lastsamples:
				if (wswp->pos < 0) {
					osstr.str("\001\001");
				} else {
					osstr << "\001";
					for (unsigned int sp = 0; sp < wswp->sweeplist[wswp->pos]->num_samples;
						 sp++) {
						osstr << (int) wswp->sweeplist[wswp->pos]->sample_data[sp];
						if (sp < wswp->sweeplist[wswp->pos]->num_samples - 1)
							osstr << ",";
					}
					osstr << "\001";
				}

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_peaksamples:
				if (wswp->peak == NULL) {
					osstr.str("\001\001");
				} else {
					osstr << "\001";
					for (unsigned int sp = 0; sp < wswp->peak->num_samples; sp++) {
						osstr << (int) wswp->peak->sample_data[sp];
						if (sp < wswp->peak->num_samples - 1)
							osstr << ",";
					}
					osstr << "\001";
				}

				cache->Cache(fnum, osstr.str());
				break;
			case WISPYSPROTO_avgsamples:
				if (wswp->avg == NULL) {
					osstr.str("\001\001");
				} else {
					osstr << "\001";
					for (unsigned int sp = 0; sp < wswp->avg->num_samples; sp++) {
						osstr << (int) wswp->avg->sample_data[sp];
						if (sp < wswp->avg->num_samples - 1)
							osstr << ",";
					}
					osstr << "\001";
				}

				cache->Cache(fnum, osstr.str());
				break;
		}

		out_string += cache->GetCache(fnum) + " ";
	}

	return 1;
}

int wispy_timer(TIMEEVENT_PARMS) {
	((KisWispyCore *) parm)->BlitSweeps(-1);
	return 1;
}

int wispy_register(GlobalRegistry *in_globalreg) {
	GlobalRegistry *globalreg = in_globalreg;
	ostringstream osstr;

	if (in_globalreg->kisnetserver == NULL) {
		printf("PLUGINDEBUG - Got called before netserver exists\n");
		return 0;
	}

	// Allocate the core class and let it go
	wcore = new KisWispyCore(globalreg);

	// We always register the protocols
	wispydproto_ref = 
		in_globalreg->kisnetserver->RegisterProtocol("WISPYDEV", 0, 0,
													 WISPYDPROTO_fields_text,
													 &Protocol_WISPYDPROTO,
													 &Protocol_WISPYDPROTO_Enable,
													 wcore);
	wispysproto_ref = 
		in_globalreg->kisnetserver->RegisterProtocol("WISPYSWEEP", 0, 0,
													 WISPYSPROTO_fields_text,
													 &Protocol_WISPYSPROTO,
													 NULL,
													 wcore);

	// And the timer
	wispytimerid =
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC,
											  NULL, 1, &wispy_timer, wcore);
};

int wispy_unregister(GlobalRegistry *in_globalreg) {
	if (in_globalreg->kisnetserver != NULL) {
		in_globalreg->kisnetserver->RemoveProtocol(wispydproto_ref);
		in_globalreg->kisnetserver->RemoveProtocol(wispysproto_ref);
	}

	if (wcore != NULL)
		delete wcore;

	in_globalreg->timetracker->RemoveTimer(wispytimerid);

	return 1;
}

int wispy_clicmd_activate_hook(CLIENT_PARMS) {
	return ((KisWispyCore *) auxptr)->ActivateHwCmd(in_clid, framework, globalreg, 
													errstr, cmdline, parsedcmdline, 
													auxptr);
}

int wispy_clicmd_deactivate_hook(CLIENT_PARMS) {
	return ((KisWispyCore *) auxptr)->DeactivateHwCmd(in_clid, framework, globalreg, 
													  errstr, cmdline, parsedcmdline, 
													  auxptr);
}

KisWispyCore::KisWispyCore(GlobalRegistry *in_globalreg) {
	globalreg = in_globalreg;

	globalreg->RegisterPollableSubsys(this);

	act_cmdid =
		globalreg->kisnetserver->RegisterClientCommand("WISPYENABLE",
													   wispy_clicmd_activate_hook,
													   this);
	deact_cmdid =
		globalreg->kisnetserver->RegisterClientCommand("WISPYDISABLE",
													   wispy_clicmd_deactivate_hook,
													   this);
	return;
}

KisWispyCore::~KisWispyCore() {
	globalreg->RemovePollableSubsys(this);
	globalreg->kisnetserver->RemoveClientCommand("WISPYENABLE");
	globalreg->kisnetserver->RemoveClientCommand("WISPYDISABLE");
}

int KisWispyCore::ActivateHw() {
	// Search for devices (currently wispy 1 only)
	int nw1 = 0, x = 0, r = 0;
	wispy_device_list devlist;

	wispy_phy *dev;

	nw1 = wispy_device_scan(&devlist);

	if (nw1 == 0) {
		_MSG("No wispy devices found on USB bus", MSGFLAG_ERROR);
		return physweep_vec.size();
	}

	for (unsigned int y = 0; y < physweep_vec.size(); y++) {
		for (x = 0; x < nw1; x++) {
			if (devlist.list[x].device_id == 
				physweep_vec[y].phydev->device_spec->device_id)
				devlist.list[x].device_id = 0;
		}
	}

	for (x = 0; x < nw1; x++) {
		if (devlist.list[x].device_id == 0)
			continue;

		dev = (wispy_phy *) malloc(WISPY_PHY_SIZE);

		_MSG("Initializing wispy device " + string(devlist.list[x].name), 
			 MSGFLAG_INFO);

		if (wispy_device_init(dev, &(devlist.list[x])) < 0) {
			_MSG("Error initializing wispy device " + string(devlist.list[x].name) + 
				 ": " + string(wispy_get_error(dev)), MSGFLAG_ERROR);
			free(dev);
			continue;
		}

		if (wispy_phy_open(dev) < 0) {
			_MSG("Error opening wispy device " + string(devlist.list[x].name) + 
				 ": " + string(wispy_get_error(dev)), MSGFLAG_ERROR);
			free(dev);
			continue;
		}

		wispy_phy_setcalibration(dev, 1);

		KisWispyCore::phy_sweep ps;
		ps.blit	= 0;
		ps.phydev = dev;

		physweep_vec.push_back(ps);
	}

	wispy_device_scan_free(&devlist);

	physweep_vec.size();
}

int KisWispyCore::DeactivateHw() {
	for (unsigned int x = 0; x < physweep_vec.size(); x++) {
		wispy_phy_close(physweep_vec[x].phydev);
		free(physweep_vec[x].phydev);
	}

	physweep_vec.clear();

	return 1;
}

int KisWispyCore::ActivateHwCmd(CLIENT_PARMS) {
	if (ActivateHw() <= 0) {
		snprintf(errstr, 1024, "Activating WiSPY hardware failed");
		return -1;
	}

	return 1;
}

int KisWispyCore::DeactivateHwCmd(CLIENT_PARMS) {
	if (DeactivateHw() <= 0) {
		snprintf(errstr, 1024, "Activating WiSPY hardware failed");
		return -1;
	}

	return 1;
}

unsigned int KisWispyCore::MergeSet(unsigned int in_max_fd, fd_set *out_rset,
									fd_set *out_wset) {
	if (globalreg->spindown)
		return in_max_fd;

	unsigned int max = in_max_fd;

	for (unsigned int x = 0; x < physweep_vec.size(); x++) {
		int fd = wispy_phy_getpollfd(physweep_vec[x].phydev);

		if (fd > max)
			max = fd;

		FD_SET(fd, out_rset);
	}

	return max;
}

int KisWispyCore::Poll(fd_set& in_rset, fd_set& in_wset) {
	for (unsigned int x = 0; x < physweep_vec.size(); x++) {
		int fd = wispy_phy_getpollfd(physweep_vec[x].phydev);
		int r;

		if (!FD_ISSET(fd, &in_rset))
			continue;

		do {
			r = wispy_phy_poll(physweep_vec[x].phydev);

			if (r == WISPY_POLL_ERROR) {
				_MSG("Error polling wispy device " + 
					 string(physweep_vec[x].phydev->device_spec->device_name) +
					 ": " + string(wispy_get_error(physweep_vec[x].phydev)), 
					 MSGFLAG_ERROR);
				wispy_phy_close(physweep_vec[x].phydev);
				free(physweep_vec[x].phydev);
				physweep_vec.erase(physweep_vec.begin() + x);
				return Poll(in_rset, in_wset);
			} else if ((r & WISPY_POLL_CONFIGURED)) {
				globalreg->kisnetserver->SendToAll(wispydproto_ref,
												   (void *) (physweep_vec[x].phydev));
			} else if ((r & WISPY_POLL_SWEEPCOMPLETE)) {
				int alloced = 0;
				for (unsigned int y = 0; y < physweep_vec[x].sweepcache.size(); y++) {
					wispy_sweep_cache *c = physweep_vec[x].sweepcache[y];
					wispy_sample_sweep *s = wispy_phy_getsweep(physweep_vec[x].phydev);
					if (c->avg->num_samples == s->num_samples &&
						c->avg->start_khz == s->start_khz &&
						c->avg->end_khz == s->end_khz) {
						alloced = 1;
						wispy_cache_append(c, s);
					}
				}

				if (alloced == 0) {
					wispy_sweep_cache *cache = wispy_cache_alloc(64, 1, 1);
					wispy_cache_append(cache, wispy_phy_getsweep(physweep_vec[x].phydev));
					cache->device_id = physweep_vec[x].phydev->device_spec->device_id;
					physweep_vec[x].sweepcache.push_back(cache);
				}
			}
		} while ((r & WISPY_POLL_ADDITIONAL));
	}

	return 0;
}

void KisWispyCore::BlitDevices(int in_fd) {
	for (unsigned int x = 0; x < physweep_vec.size(); x++) {
		kis_protocol_cache cache;
		globalreg->kisnetserver->SendToClient(in_fd, wispydproto_ref, 
											  (void *) physweep_vec[x].phydev, 
											  &cache);
	}
}

void KisWispyCore::BlitSweeps(int in_fd) {
	for (unsigned int x = 0; x < physweep_vec.size(); x++) {
		for (unsigned int y = 0; y < physweep_vec[x].sweepcache.size(); y++) {
			if (in_fd == -1)
				globalreg->kisnetserver->SendToAll(wispysproto_ref,
												  (void *) (physweep_vec[x].sweepcache[y]));
		}
	}
}

