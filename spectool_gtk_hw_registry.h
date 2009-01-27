/* Metageek WiSPY interface 
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

#include "config.h"

#ifndef __WISPY_GTK_HW_REGISTRY__
#define __WISPY_GTK_HW_REGISTRY__

#ifdef HAVE_GTK

#include <spectool_container.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "spectool_net_client.h"

#define WDR_MAX_DEV		32
#define WDR_MAX_NET		32

/* Sweep callback functions take a sweep and do (something) with it,
 * and so need:
 * int slot - slot # of device which triggered
 * wispy_sample_sweep sweep - the sweep data
 * void *aux - auxptr they provided (probably their main struct ref
 *
 * if sweep is NULL an error occurred and the device should be released.
 */
typedef struct _wdr_reg_sweep_cb {
	void *aux;
	void (*cb)(int, int, wispy_sample_sweep *, void *);
	/* Local queue buf and number to aggregate together */
	wispy_sweep_cache *agg_sweep;
	int num_agg;
	int pos_agg;
} wdr_reg_sweep_cb;

/* Item in the device registry */
typedef struct _wdr_reg_dev {
	/* Physical device */
	wispy_phy *phydev;
	/* List of sweep caches we write into */
	GList *sweep_cb_l;
	/* Reference count for this hw dev */
	int refcount;
	int poll_tag;
	void *poll_rec;
} wdr_reg_dev;

typedef struct _wdr_reg_srv {
	spectool_server *srv;
	void *poll_rec;

	int iowtag, iortag;
	GIOChannel *ioch;

	/* GTK tracking bits for the network selector */
	GtkTreeRowReference *tree_row_ref;

} wdr_reg_srv;

/* Fixed-size device registry.  I don't think it's unreasonable to 
 * assume a user will never have more than 32 devices on a system,
 * and if I'm wrong, we can mod it easily enough.  Saves a whole lot
 * of glist mess */
typedef struct _wispy_device_registry {
	wdr_reg_dev *devices[WDR_MAX_DEV];
	int max_dev;
	int cur_dev;

	wdr_reg_srv *netservers[WDR_MAX_NET];
	int max_srv;
	int cur_srv;

	void *netmanager;

	int bcastsock;
	GIOChannel *bcioc;
	int bcioc_rtag;

} wispy_device_registry;

/* Passed to gdk_input_add as aux pointer struct */
typedef struct _wdr_poll_rec {
	wispy_device_registry *wdr;
	int slot;
	int poll_tag;
	int poll_wtag;
	GIOChannel *ioch;
} wdr_poll_rec;

void wdr_init(wispy_device_registry *wdr);
void wdr_free(wispy_device_registry *wdr);

int wdr_open_add(wispy_device_registry *wdr, wispy_device_rec *devrec, int pos,
				 char *errstr);

int wdr_open_phy(wispy_device_registry *wdr, wispy_phy *phydev, char *errstr);

int wdr_open_net(wispy_device_registry *wdr, char *url, char *errstr);
int wdr_open_netptr(wispy_device_registry *wdr, spectool_server *netptr, char *errstr);
void wdr_close_net(wispy_device_registry *wdr, int slot);

int wdr_enable_bcast(wispy_device_registry *wdr, char *errstr);
void wdr_disable_bcast(wispy_device_registry *wdr);
gboolean wdr_bcpoll(GIOChannel *ioch, GIOCondition cond, gpointer data);

wispy_phy *wdr_get_phy(wispy_device_registry *wdr, int slot);

void wdr_add_ref(wispy_device_registry *wdr, int slot);
void wdr_del_ref(wispy_device_registry *wdr, int slot);

void wdr_add_sweepcb(wispy_device_registry *wdr, int slot,
					 void (*cb)(int, int, wispy_sample_sweep *, void *),
					 int nagg, void *aux);
void wdr_del_sweepcb(wispy_device_registry *wdr, int slot,
					 void (*cb)(int, int, wispy_sample_sweep *, void *),
					 void *aux);

/* Polling function suitable for calling from gdk_input */
void wdr_poll(gpointer data, gint source, GdkInputCondition condition);
gboolean wdr_netrpoll(GIOChannel *ioch, GIOCondition cond, gpointer data);

/* Struct used in menu callbacks from wdr-assisted popup
 * menus.  Needed to be able to unref all the devices in
 * the menu, etc.  Contains the slot # of the selected
 * device plus enough data to unref the phys */
typedef struct _wdr_menu_rec {
	int slot;
	void *aux;
	wispy_device_registry *wdr;
} wdr_menu_rec;

/* Populate a menu and return a reference we have to make sure to
 * clean up */
GList *wdr_populate_menu(wispy_device_registry *wdr, GtkWidget *menu,
						 int sep, int fillnodev, GCallback cb, void *aux);
void wdr_free_menu(wispy_device_registry *wdr, GList *gl);

/* Device picker aux data */
typedef struct _wdr_gtk_devpicker_aux {
	wispy_device_registry *wdr;

	wispy_device_list devlist;

	GtkWidget *picker_win;

	GtkWidget *okbutton, *cancelbutton, *rescanbutton,
			  *scrolled_win;
	GtkTreeView *treeview; 
	GtkTreeViewColumn *treecolumn; 
	GtkListStore *treemodellist;

	/* Callback accepts slot * and its own callback ptr */
	void (*pickcb)(int, void *);
	void *cbaux;
} wdr_gtk_devpicker_aux;

/* Spawn a device picker window and call our CB when the user has picked one */
void wdr_devpicker_spawn(wispy_device_registry *wdr,
						 void (*cb)(int, void *),
						 void *aux);

typedef struct _wdr_gtk_netmanager_aux {
	wispy_device_registry *wdr;

	GtkWidget *picker_win;

	GtkWidget *addbutton, *dconbutton, *openbutton, *closebutton,
			  *scrolled_win;

	GtkTreeView *treeview; 
	GtkTreeViewColumn *treecolumn; 
	GtkTreeStore *treestore;
	
	GtkTreeRowReference *noservers;

	int timer_ref;

	/* Callback accepts slot * and its own callback ptr */
	void (*pickcb)(int, void *);
	void *cbaux;
} wdr_gtk_netmanager_aux;

void wdr_netmanager_spawn(wispy_device_registry *wdr,
						  void (*cb)(int, void *),
						  void *aux);

typedef struct _wdr_gtk_netentry_aux {
	wispy_device_registry *wdr;

	wispy_device_list devlist;

	GtkWidget *picker_win;
	GtkWidget *hostentry, *portentry;
	GtkWidget *okbutton, *cancelbutton;

} wdr_gtk_netentry_aux;

void wdr_netentry_spawn(wispy_device_registry *wdr);

#endif

#endif

