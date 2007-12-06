/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <config.h>

#include <stdio.h>

#include <string>
#include <sstream>

#include <kismet/globalregistry.h>
#include <kismet/kis_panel_plugin.h>
#include <kismet/kis_panel_frontend.h>
#include <kismet/kis_panel_windows.h>
#include <kismet/kis_panel_network.h>
#include <kismet/kis_panel_preferences.h>

class WispyNetClient : public Pollable {
public:
	WispyNetClient() {
		fprintf(stderr, "fatal oops - called wispynetclient();\n");
		exit(1);
	}

	WispyNetClient(GlobalRegistry *in_globalreg) {
		sr = NULL;
	}

	int OpenHost(string in_url) {
		char errstr[WISPY_ERROR_MAX];

		sr = (spectool_server *) malloc(sizeof(spectool_server));

		if (spectool_netcli_init(sr, in_url.c_str(), errstr)  < 0) {
			_MSG("SPECTOOL - Error initializing network connection: " +
				 string(errstr), MSGFLAG_ERROR);
			free(sr);
			sr = NULL;
			return -1;
		}

		if (spectool_netcli_connect(sr, errstr) < 0) {
			_MSG("SPECTOOL - Error connecting to " + in_url + ": " + 
				 string(errstr), MSGFLAG_ERROR);
			free(sr);
			sr = NULL;
			return -1;
		}
	}

	virtual ~WispyNetClient() {
		globalreg->RemovePollableSubsys(this);

		if (sr != NULL) {
			spectool_netcli_close(sr);
			sr = NULL;
		}
	}

	virtual unsigned int MergeSet(unsigned int in_max_fd, fd_set *out_rset,
								  fd_set *out_wset) {
		if (sr == NULL)
			return in_max_fd;

		FD_SET(spectool_netcli_getpollfd(sr));

		if (spectool_netcli_getwritepend(sr) > 0)
			FD_SET(spectool_netcli_getpollfd(sr), sr);

		if ((int) in_max_fd < spectool_netcli_getpollfd(sr))
			return spectool_netcli_getpollfd(sr);

		return in_max_fd;
	}

	virtual int Poll(fd_set& in_rset, fd_set& in_wset) {
		char errstr[WISPY_ERROR_MAX];

		if (sr == NULL)
			return 0;

		if (FD_ISSET(spectool_netcli_getpollfd(sr), &in_rset)) {
			int ret = SPECTOOL_NETCLI_POLL_ADDITIONAL;

			while ((ret & SPECTOOL_NETCLI_POLL_ADDITIONAL)) {

				if ((ret = spectool_netcli_poll(sr, errstr)) < 0) {
					_MSG("SPECTOOL - Error polling network connection, " + 
						 string(errstr), MSG_ERROR);
					return -1;
				}

				if ((ret & SPECTOOL_NETCLI_POLL_NEWDEVS)) {
					spectool_net_dev *ndi = sr.devlist;
					while (ndi != NULL) {
						_MSG("SPECTOOL - Enabling network device " +
							 string(ndi->device_name));


		}

	}

protected:
	spectool_server *sr;

}

int wispy_menu_callback(void *auxptr);

#define KIS_WISPY_DFIELDS	"id,name,version"
#define KIS_WISPY_SFIELDS	"devid,startkhz,endkhz,reskhz,minsample,minsigrep,maxsample," \
	"numsamples,lastsamples,peaksamples,avgsamples"
#define KIS_WISPY_SFIELDS_NUM	11

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
	/* Width of channels */
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
struct wispy_channels channel_list[] = {
	{ "802.11b/g", 2400000, 2484000, 14, chan_freqs_24, 12, chan_text_24 },
	{ NULL, 0, 0, 0, NULL, 0, NULL }
};

class Kis_Wispy_Specan : public Kis_Panel_Component {
public:
	Kis_Wispy_Specan() { fprintf(stderr, "FATAL OOPS: Kis_Wispy_Specan()\n"); exit(1); }
	Kis_Wispy_Specan(GlobalRegistry *in_globalreg, Kis_Panel *in_panel);
	virtual ~Kis_Wispy_Specan();
	
	virtual void DrawComponent();
	virtual void Activate(int subcomponent);
	virtual void Deactivate();
	virtual int KeyPress(int in_key);
	virtual void SetPosition(int isx, int isy, int iex, int iey);

	// Network protocol handlers
	void NetClientConfigure(KisNetClient *in_cli, int in_recon);
	void NetClientAdd(KisNetClient *in_cli, int add);
	void Proto_WISPYSWEEP(CLIPROTO_CB_PARMS);

	typedef struct {
		int devid;
		int startkhz, endkhz, reskhz;
		int minsample, minsigrep, maxsample;
		int numsamples;
		vector<int> last_samples, peak_samples, avg_samples;
	} kis_wispy_sample;

protected:
	KisPanelInterface *kpinterface;

	int addcli_ref;

	int color_bg, color_peak, color_cur, color_avg;

	map<int, vector<kis_wispy_sample> > dev_sample_map;

	int startsampleoft;

	vector<KisNetClient *> knc_vec;
};

class Kis_Wispy_Panel : public Kis_Panel {
public:
	Kis_Wispy_Panel() { fprintf(stderr, "FATAL OOPS: Kis_Wispy_Panel()\n"); exit(1); }
	Kis_Wispy_Panel(GlobalRegistry *in_globalreg, KisPanelInterface *in_intf);
	virtual ~Kis_Wispy_Panel();

	virtual void Position(int in_sy, int in_sx, int in_y, int in_x);
	virtual void DrawPanel();
	virtual int KeyPress(int in_key);

protected:
	// More to come
	int mn_wispy, mi_close;
	int mn_prefs, mi_disable, mi_colors;

	Kis_Wispy_Specan *sa;

	int disable_on_close;

	friend class Kis_Wispy_Specan;
};

extern "C" {
int panel_plugin_init(GlobalRegistry *globalreg, KisPanelPluginData *pdata) {
	_MSG("Initializing Kismet-WiSPY client plugin", MSGFLAG_INFO);

	pdata->mainpanel->AddPluginMenuItem("WiSPY Spec-An", wispy_menu_callback,
										pdata);

	return 1;
}
}

void WispySpecan_Configured(CLICONF_CB_PARMS) {
	((Kis_Wispy_Specan *) auxptr)->NetClientConfigure(kcli, recon);
}

void WispySpecan_AddCli(KPI_ADDCLI_CB_PARMS) {
	((Kis_Wispy_Specan *) auxptr)->NetClientAdd(netcli, add);
}

void WispySpecan_WISPYSWEEP(CLIPROTO_CB_PARMS) {
	((Kis_Wispy_Specan *) auxptr)->Proto_WISPYSWEEP(globalreg, proto_string,
													proto_parsed, srccli, auxptr);
}

Kis_Wispy_Specan::Kis_Wispy_Specan(GlobalRegistry *in_globalreg, 
								   Kis_Panel *in_panel) :
	Kis_Panel_Component(in_globalreg, in_panel) {
	kpinterface = in_panel->FetchPanelInterface();

	color_bg = color_peak = color_cur = color_avg = 0;

	addcli_ref = kpinterface->Add_NetCli_AddCli_CB(WispySpecan_AddCli, (void *) this);

	startsampleoft = 0;
}

Kis_Wispy_Specan::~Kis_Wispy_Specan() {
	kpinterface->Remove_Netcli_AddCli_CB(addcli_ref);
	kpinterface->Remove_AllNetcli_ProtoHandler("WISPYSWEEP", 
											   WispySpecan_WISPYSWEEP, this);
	if (((Kis_Wispy_Panel *) parent_panel)->disable_on_close) {
		for (unsigned int x = 0; x < knc_vec.size(); x++) {
			knc_vec[x]->InjectCommand("WISPYDISABLE");
		}
	}
}

void Kis_Wispy_Specan::NetClientConfigure(KisNetClient *in_cli, int in_recon) {
	if (in_recon)
		return;

	// Enable the device
	in_cli->InjectCommand("WISPYENABLE");

	if (in_cli->RegisterProtoHandler("WISPYSWEEP", KIS_WISPY_SFIELDS,
									 WispySpecan_WISPYSWEEP, this) < 0) {
		_MSG("Could not register WISPYSWEEP protocol with remote server, is "
			 "the WiSPY plugin running?", MSGFLAG_ERROR);
	}
}

void Kis_Wispy_Specan::NetClientAdd(KisNetClient *in_cli, int add) {
	if (add == 0)
		return;

	in_cli->AddConfCallback(WispySpecan_Configured, 1, this);
}

void Kis_Wispy_Specan::SetPosition(int isx, int isy, int iex, int iey) {
	Kis_Panel_Component::SetPosition(isx, isy, iex, iey);
}

void Kis_Wispy_Specan::Proto_WISPYSWEEP(CLIPROTO_CB_PARMS) {
	if (proto_parsed->size() < KIS_WISPY_SFIELDS_NUM)
		return;

	kis_wispy_sample samp;
	int fnum = 0;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.devid)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.startkhz)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.endkhz)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.reskhz)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.minsample)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.minsigrep)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.maxsample)) != 1)
		return;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &(samp.numsamples)) != 1)
		return;

	vector<string> strvec;
	strvec = StrTokenize((*proto_parsed)[fnum++].word, ",");
	if (strvec.size() != samp.numsamples)
		return;
	for (unsigned int x = 0; x < strvec.size(); x++) {
		int t;
		if (sscanf(strvec[x].c_str(), "%d", &t) != 1)
			return;
		samp.last_samples.push_back(t);
	}

	strvec = StrTokenize((*proto_parsed)[fnum++].word, ",");
	if (strvec.size() != samp.numsamples)
		return;
	for (unsigned int x = 0; x < strvec.size(); x++) {
		int t;
		if (sscanf(strvec[x].c_str(), "%d", &t) != 1)
			return;
		samp.peak_samples.push_back(t);
	}

	strvec = StrTokenize((*proto_parsed)[fnum++].word, ",");
	if (strvec.size() != samp.numsamples)
		return;
	for (unsigned int x = 0; x < strvec.size(); x++) {
		int t;
		if (sscanf(strvec[x].c_str(), "%d", &t) != 1)
			return;
		samp.avg_samples.push_back(t);
	}

	if (dev_sample_map.find(samp.devid) == dev_sample_map.end()) {
		vector<kis_wispy_sample> v;
		v.push_back(samp);
		dev_sample_map[samp.devid] = v;
	} else {
		int consumed = 0;
		for (unsigned int x = 0; x < dev_sample_map[samp.devid].size(); x++) {
			kis_wispy_sample s = dev_sample_map[samp.devid][x];
			if (s.numsamples == samp.numsamples &&
				s.startkhz == samp.startkhz && s.endkhz == samp.endkhz &&
				s.reskhz == samp.reskhz) {
				dev_sample_map[samp.devid][x] = samp;
				consumed = 1;
				break;
			}
		}

		if (consumed == 0)
			dev_sample_map[samp.devid].push_back(samp);
	}
}

void Kis_Wispy_Specan::DrawComponent() {
	if (visible == 0)
		return;

	parent_panel->InitColorPref("wispy_sa_bg_color", "cyan,cyan");
	parent_panel->InitColorPref("wispy_sa_av_color", "green,green");
	parent_panel->InitColorPref("wispy_sa_lt_color", "yellow,yellow");
	parent_panel->InitColorPref("wispy_sa_pk_color", "black,black");
	parent_panel->ColorFromPref(color_bg, "wispy_sa_bg_color");
	parent_panel->ColorFromPref(color_avg, "wispy_sa_av_color");
	parent_panel->ColorFromPref(color_peak, "wispy_sa_pk_color");
	parent_panel->ColorFromPref(color_cur, "wispy_sa_lt_color");

	int text_color = 0;
	parent_panel->ColorFromPref(text_color, "panel_text_color");

	// For now we only take 1 device, 1 sweep range ...
	// TODO - better return condition
	kis_wispy_sample samp;
	map<int, vector<kis_wispy_sample> >::iterator dsmi;
	if (dev_sample_map.size() == 0) {
		mvwaddnstr(window, sy + 1, sx, "No WiSPY devices reported by server...", ex);
		return;
	}

	dsmi = dev_sample_map.begin();
	if (dsmi->second.size() == 0) {
		mvwaddnstr(window, sy + 1, sx, "No WiSPY sweep records reported by server...", ex);
		return;
	}
	samp = dsmi->second[0];

	// Room for levels
	int drawboxw = (ex - sx) - 4;

	// Room for height
	int drawboxh = (ey - sy) - 2;

	// Plot the background
	wattrset(window, color_bg);
	string s = string(drawboxw, ' ');
	for (int l = 0; l < drawboxh; l++) 
		mvwaddnstr(window, sy + l, sx, s.c_str(), ex);

	// Match to a set of channels
	wispy_channels *chanset = NULL;

	for (int x = 0; channel_list[x].name != NULL; x++) {
		if (channel_list[x].startkhz == samp.startkhz &&
			channel_list[x].endkhz == samp.endkhz) {
			chanset = &(channel_list[x]);
			break;
		}
	}

	// Plot the channels
	if (chanset != NULL) {
		int c = 0;
		for (int w = startsampleoft; w < samp.numsamples && w < drawboxw; w++) {
			// Catch up to the channel allocation
			if (chanset->chan_freqs[c] < samp.startkhz + (samp.reskhz * w)) {
				for (c; c < chanset->chan_num; c++) {
					if (chanset->chan_freqs[c] >= samp.startkhz + (samp.reskhz * w))
						break;
				}
			}

			if (chanset->chan_freqs[c] == samp.startkhz + (samp.reskhz * w)) {
				wattrset(window, text_color);

				mvwaddch(window, ey - 2, sx + w, chanset->chan_text[c][0]);
				if (strlen(chanset->chan_text[c]) > 1)
					mvwaddch(window, ey - 1, sx + w, chanset->chan_text[c][1]);
			}
		}
	}

#if 0
	// Plot the dbm scale, we want to plot in 10dB jumps
	for (int d = abs(samp.maxsample); d <= abs(samp.minsample); d += 10) {
		float graphperc;
		char db[4];

		graphperc = (float) drawboxh * 
			(float) ((float) (d + samp.maxsample) /
					 (float) (abs(samp.minsample) + samp.maxsample));
		snprintf(db, 4, "-%d",	d);
		
		wattrset(window, text_color);
		mvwaddstr(window, sy + (int) graphperc, ex - 3, db);
	}
#endif

	// Plot the results
	int db = abs(samp.maxsample);
	float graphperc;
	char dbs[4];
	for (int l = 0; l < drawboxh; l++) {
		int ul = 0;

		graphperc = (float) drawboxh * 
			(float) ((float) (db + samp.maxsample) /
					 (float) (abs(samp.minsample) + samp.maxsample));

		if ((int) graphperc == l) {
			snprintf(dbs, 4, "-%d",	db);
			wattrset(window, text_color);
			mvwaddstr(window, sy + (int) graphperc, ex - 3, dbs);
			db += 10;
			ul = 1;
		}

		for (int w = startsampleoft; w < samp.numsamples && w < drawboxw; w++) {
			graphperc = (float) drawboxh * 
				(float) ((float) (abs(samp.peak_samples[w]) + samp.maxsample) /
						 (float) (abs(samp.minsample) + samp.maxsample));

			if (graphperc <= l && graphperc != 0) {
				wattrset(window, color_peak);
				if (ul)
					wattron(window, WA_UNDERLINE);
				mvwaddch(window, sy + l, sx + w, ' ');
			}

			graphperc = (float) drawboxh * 
				(float) ((float) (abs(samp.avg_samples[w]) + samp.maxsample) /
						 (float) (abs(samp.minsample) + samp.maxsample));

			if (graphperc <= l && graphperc != 0) {
				wattrset(window, color_avg);
				if (ul)
					wattron(window, WA_UNDERLINE);
				mvwaddch(window, sy + l, sx + w, ' ');
			}
		}
	}
}

void Kis_Wispy_Specan::Activate(int subcomponent) {
	active = 1;
}

void Kis_Wispy_Specan::Deactivate() {
	active = 0;
}

int Kis_Wispy_Specan::KeyPress(int in_key) {
	if (visible == 0)
		return 0;

	return 0;
}

Kis_Wispy_Panel::Kis_Wispy_Panel(GlobalRegistry *in_globalreg,
								 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	menu = new Kis_Menu(globalreg, this);

	mn_wispy = menu->AddMenu("Wispy", 0);

	mn_prefs = menu->AddSubMenuItem("Preferences", mn_wispy, 'P');
	mi_colors = menu->AddMenuItem("Colors...", mn_prefs, 'O');
	mi_disable = menu->AddMenuItem("Auto-Disable WiSPY", mn_prefs, 'D');

	if (kpinterface->prefs.FetchOpt("WISPY_DISABLE") != "0") {
		menu->SetMenuItemChecked(mi_disable, 1);
		disable_on_close = 1;
	} else {
		menu->SetMenuItemChecked(mi_disable, 0);
		disable_on_close = 0;
	}

	mi_close = menu->AddMenuItem("Close", mn_wispy, 'C');

	menu->Show();

	sa = new Kis_Wispy_Specan(globalreg, this);

	sa->Show();
	comp_vec.push_back(sa);
}

Kis_Wispy_Panel::~Kis_Wispy_Panel() {

}

void Kis_Wispy_Panel::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	menu->SetPosition(1, 0, 0, 0);
	sa->SetPosition(in_sx + 2, in_sy + 2, in_x - 2, in_y - 3);
}

void Kis_Wispy_Panel::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);
	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	menu->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_Wispy_Panel::KeyPress(int in_key) {
	int ret;

	ret = menu->KeyPress(in_key);

	if (ret == 0)
		return 0;

	if (ret > 0) {
		if (ret == mi_colors) {
			Kis_ColorPref_Panel *cpp = new Kis_ColorPref_Panel(globalreg, 
															   kpinterface);
			cpp->AddColorPref("wispy_sa_bg_color", "Graph BG");
			cpp->AddColorPref("wispy_sa_av_color", "Graph Avg");
			cpp->AddColorPref("wispy_sa_pk_color", "Graph Peak");
			cpp->Position((LINES / 2) - 5, (COLS / 2) - 20, 10, 40);
			kpinterface->AddPanel(cpp);
		} else if (ret == mi_disable) {
			if (disable_on_close) {
				disable_on_close = 0;
				kpinterface->prefs.SetOpt("WISPY_DISABLE", "0", 0);
				menu->SetMenuItemChecked(mi_disable, 0);
			} else {
				disable_on_close = 1;
				kpinterface->prefs.SetOpt("WISPY_DISABLE", "1", 0);
				menu->SetMenuItemChecked(mi_disable, 1);
			}
		} else if (ret == mi_close) {
			globalreg->panel_interface->KillPanel(this);
		}
	}

	return 0;
}

int wispy_menu_callback(void *auxptr) {
	KisPanelPluginData *pdata = (KisPanelPluginData *) auxptr;
	GlobalRegistry *globalreg = pdata->globalreg;

	Kis_Wispy_Panel *wp = new Kis_Wispy_Panel(globalreg, pdata->kpinterface);
	wp->Position(0, 0, LINES, COLS);
	pdata->kpinterface->AddPanel(wp);

	return 1;
}

