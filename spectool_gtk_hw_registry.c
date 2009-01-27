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

#include <stdlib.h>
#include "config.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk.h"
#include "spectool_net.h"
#include "spectool_net_client.h"

void wdr_init(wispy_device_registry *wdr) {
	int x = 0;

	wdr->max_dev = WDR_MAX_DEV - 1;
	wdr->cur_dev = 0;

	wdr->max_srv = WDR_MAX_NET - 1;
	wdr->cur_srv = 0;

	for (x = 0; x < WDR_MAX_DEV; x++) {
		wdr->devices[x] = NULL;
	}

	for (x = 0; x < WDR_MAX_NET; x++) {
		wdr->netservers[x] = NULL;
	}

	wdr->bcastsock = -1;
}

void wdr_free(wispy_device_registry *wdr) {
	int x = 0;

	for (x = 0; x < WDR_MAX_DEV; x++) {
		if (wdr->devices[x] == NULL)
			continue;

		/* Close it */
		wispy_phy_close(wdr->devices[x]->phydev);

		g_list_free(wdr->devices[x]->sweep_cb_l);

		/* remove the polling event */
		gdk_input_remove(wdr->devices[x]->poll_tag);
		free(wdr->devices[x]->poll_rec);

		free(wdr->devices[x]);
	}
}

int wdr_open_add(wispy_device_registry *wdr, wispy_device_rec *devrec,
				 char *errstr) {
	int x;
	wispy_phy *phydev;
	wdr_reg_dev *regdev;

	for (x = 0; x < wdr->max_dev; x++) {
		if (wdr->devices[x] == NULL)
			continue;

		if (wdr->devices[x]->phydev->device_spec->device_id ==
			devrec->device_id) {
			return x;
		}
	}

	if (wdr->cur_dev >= wdr->max_dev) {
		snprintf(errstr, WISPY_ERROR_MAX, "WDR registry full");
		return -1;
	}

	phydev = (wispy_phy *) malloc(WISPY_PHY_SIZE);

	if (wispy_device_init(phydev, devrec) < 0) {
		printf("deug - wdr devinit failed\n");
		snprintf(errstr, WISPY_ERROR_MAX, "Error initializing WiSPY device %s: %s",
				 devrec->name, wispy_get_error(phydev));
		free(phydev);
		return -1;
	}

	if (wispy_phy_open(phydev) < 0) {
		snprintf(errstr, WISPY_ERROR_MAX, "Error opening WiSPY device %s: %s",
				 devrec->name, wispy_get_error(phydev));
		free(phydev);
		return -1;
	}

	/* We always want to calibrate it, imho */
	wispy_phy_setcalibration(phydev, 1);

	/* Set the position to 0 for now */
	wispy_phy_setposition(phydev, 0, 0, 0);

	regdev = (wdr_reg_dev *) malloc(sizeof(wdr_reg_dev));
	regdev->phydev = phydev;
	regdev->sweep_cb_l = NULL;
	/* The open callback used by the picker/menu needs to claim
	 * a reference to this */
	regdev->refcount = 0;

	/* Queue it for polling */
	regdev->poll_rec = (wdr_poll_rec *) malloc(sizeof(wdr_poll_rec));
	((wdr_poll_rec *) regdev->poll_rec)->wdr = wdr;

	regdev->poll_tag = gdk_input_add(wispy_phy_getpollfd(phydev), 
									 GDK_INPUT_READ, 
									 wdr_poll, regdev->poll_rec);

	((wdr_poll_rec *) regdev->poll_rec)->poll_tag = regdev->poll_tag;

	for (x = 0; x < wdr->max_dev; x++) {
		if (wdr->devices[x] != NULL)
			continue;

		wdr->devices[x] = regdev;
		wdr->cur_dev++;
		((wdr_poll_rec *) regdev->poll_rec)->slot = x;

		return x;
	}

	return x;
}

int wdr_open_phy(wispy_device_registry *wdr, wispy_phy *phydev, char *errstr) {
	int x;
	wdr_reg_dev *regdev;

	for (x = 0; x < wdr->max_dev; x++) {
		if (wdr->devices[x] == NULL)
			continue;

		if (wdr->devices[x]->phydev->device_spec->device_id ==
			wispy_phy_getdevid(phydev)) {
			return x;
		}
	}

	if (wdr->cur_dev >= wdr->max_dev) {
		snprintf(errstr, WISPY_ERROR_MAX, "WDR registry full");
		return -1;
	}

	regdev = (wdr_reg_dev *) malloc(sizeof(wdr_reg_dev));
	regdev->phydev = phydev;
	regdev->sweep_cb_l = NULL;
	/* The open callback used by the picker/menu needs to claim
	 * a reference to this */
	regdev->refcount = 0;

	/* Queue it for polling */
	regdev->poll_rec = (wdr_poll_rec *) malloc(sizeof(wdr_poll_rec));
	((wdr_poll_rec *) regdev->poll_rec)->wdr = wdr;

	regdev->poll_tag = gdk_input_add(wispy_phy_getpollfd(phydev), 
									 GDK_INPUT_READ, 
									 wdr_poll, regdev->poll_rec);

	((wdr_poll_rec *) regdev->poll_rec)->poll_tag = regdev->poll_tag;

	for (x = 0; x < wdr->max_dev; x++) {
		if (wdr->devices[x] != NULL)
			continue;

		wdr->devices[x] = regdev;
		wdr->cur_dev++;
		((wdr_poll_rec *) regdev->poll_rec)->slot = x;

		return x;
	}

	return x;
}

int wdr_open_net(wispy_device_registry *wdr, char *url, char *errstr) {
	int x;
	spectool_server *netptr;

	netptr = (spectool_server *) malloc(sizeof(spectool_server));

	if (spectool_netcli_init(netptr, url, errstr) < 0)
		return -1;

	if ((x = wdr_open_netptr(wdr, netptr, errstr)) < 0) {
		free(netptr);
	}

	return x;
}

int wdr_open_netptr(wispy_device_registry *wdr, spectool_server *netptr, 
					char *errstr) {
	int x;
	wdr_reg_srv *wrs;

	for (x = 0; x < wdr->max_srv; x++) {
		if (wdr->netservers[x] == NULL)
			continue;

		if (spectool_netcli_getaddr(wdr->netservers[x]->srv) ==
			spectool_netcli_getaddr(netptr) &&
			spectool_netcli_getport(wdr->netservers[x]->srv) ==
			spectool_netcli_getport(netptr)) {
			return x;
		}
	}

	if (wdr->cur_srv >= wdr->max_srv) {
		snprintf(errstr, WISPY_ERROR_MAX, "WDR registry full");
		return -1;
	}

	wrs = (wdr_reg_srv *) malloc(sizeof(wdr_reg_srv));

	wrs->iowtag = -1;
	wrs->iortag = -1;

	wrs->srv = netptr;

	if (spectool_netcli_connect(wrs->srv, errstr) < 0) {
		return -1;
	}

	spectool_netcli_setbufferwrite(wrs->srv, 0);

	wrs->poll_rec = (wdr_poll_rec *) malloc(sizeof(wdr_poll_rec));

	((wdr_poll_rec *) wrs->poll_rec)->wdr = wdr;

	wrs->ioch = g_io_channel_unix_new(spectool_netcli_getpollfd(wrs->srv));
	wrs->iortag = g_io_add_watch(wrs->ioch, G_IO_IN, wdr_netrpoll, wrs->poll_rec);

	((wdr_poll_rec *) wrs->poll_rec)->poll_tag = wrs->iortag;

	wrs->tree_row_ref = NULL;

	for (x = 0; x < wdr->max_srv; x++) {
		if (wdr->netservers[x] != NULL)
			continue;

		wdr->netservers[x] = wrs;
		wdr->cur_srv++;
		((wdr_poll_rec *) wrs->poll_rec)->slot = x;

		return x;
	}

	return x;
}

void wdr_close_net(wispy_device_registry *wdr, int slot) {
	if (slot < 0 || slot > wdr->max_srv)
		return;

	if (wdr->netservers[slot] == NULL)
		return;

	spectool_netcli_close(wdr->netservers[slot]->srv);

	/* remove the polling event */
	g_io_channel_shutdown(wdr->netservers[slot]->ioch, TRUE, NULL);

	free(wdr->netservers[slot]->poll_rec);
	free(wdr->netservers[slot]);

	wdr->netservers[slot] = NULL;
	wdr->cur_srv--;
}

int wdr_enable_bcast(wispy_device_registry *wdr, char *errstr) {
	if ((wdr->bcastsock = spectool_netcli_initbroadcast(WISPY_NET_DEFAULT_PORT,
														errstr)) < 0)
		return -1;

	wdr->bcioc = g_io_channel_unix_new(wdr->bcastsock);
	wdr->bcioc_rtag = 
		g_io_add_watch(wdr->bcioc, G_IO_IN, wdr_bcpoll, wdr);

	return 1;
}

void wdr_disable_bcast(wispy_device_registry *wdr) {
	if (wdr->bcastsock >= 0) {
		close(wdr->bcastsock);

		g_io_channel_shutdown(wdr->bcioc, TRUE, NULL);
	}

	wdr->bcastsock = -1;
}

gboolean wdr_bcpoll(GIOChannel *ioch, GIOCondition cond, gpointer data) {
	wispy_device_registry *wdr = (wispy_device_registry *) data;
	char bcasturl[SPECTOOL_NETCLI_URL_MAX];
	char errstr[WISPY_ERROR_MAX];
	char reperr[WISPY_ERROR_MAX];
	int ret;

	if ((ret = spectool_netcli_pollbroadcast(wdr->bcastsock, 
											 bcasturl, errstr)) < 0) {
		snprintf(reperr, WISPY_ERROR_MAX, "Error processing broadcast packet, disabling "
				 "broadcast detection: %s", errstr);
		Wispy_Alert_Dialog(reperr);
		wdr_disable_bcast(wdr);
		return FALSE;
	} else if (ret > 0) {
		wdr_open_net(wdr, bcasturl, errstr);
	}

	return TRUE;
}

wispy_phy *wdr_get_phy(wispy_device_registry *wdr, int slot) {
	if (slot < 0 || slot > wdr->max_dev)
		return;

	if (wdr->devices[slot] == NULL)
		return;

	return wdr->devices[slot]->phydev;
}

void wdr_add_ref(wispy_device_registry *wdr, int slot) {
	if (slot < 0 || slot > wdr->max_dev)
		return;

	if (wdr->devices[slot] == NULL)
		return;

	wdr->devices[slot]->refcount++;
}

void wdr_del_ref(wispy_device_registry *wdr, int slot) {
	if (slot < 0 || slot > wdr->max_dev)
		return;

	if (wdr->devices[slot] == NULL)
		return;

	wdr->devices[slot]->refcount--;

	if (wdr->devices[slot]->refcount > 0)
		return;

	printf("debug - wdr_del_ref removing device\n");

	wispy_phy_close(wdr->devices[slot]->phydev);

	g_list_free(wdr->devices[slot]->sweep_cb_l);

	/* remove the polling event */
	gdk_input_remove(wdr->devices[slot]->poll_tag);
	free(wdr->devices[slot]->poll_rec);

	free(wdr->devices[slot]);

	wdr->devices[slot] = NULL;
	wdr->cur_dev--;
}

void wdr_add_sweepcb(wispy_device_registry *wdr, int slot,
					 void (*cb)(int, int, wispy_sample_sweep *, void *),
					 int nagg, void *aux) {
	wdr_reg_sweep_cb *wdrcb;

	if (slot < 0 || slot > wdr->max_dev)
		return;

	if (wdr->devices[slot] == NULL)
		return;

	wdrcb = (wdr_reg_sweep_cb *) malloc(sizeof(wdr_reg_sweep_cb));

	wdrcb->aux = aux;
	wdrcb->cb = cb;

	if (nagg == 0) {
		wdrcb->agg_sweep = NULL;
	} else {
		wdrcb->agg_sweep = wispy_cache_alloc(nagg, 1, 0);
	}
	wdrcb->num_agg = nagg;
	wdrcb->pos_agg = 0;

	wdr->devices[slot]->sweep_cb_l = 
		g_list_append(wdr->devices[slot]->sweep_cb_l, wdrcb);
}

void wdr_del_sweepcb(wispy_device_registry *wdr, int slot,
					 void (*cb)(int, int, wispy_sample_sweep *, void *),
					 void *aux) {
	GList *iter;

	if (slot < 0 || slot > wdr->max_dev)
		return;

	if (wdr->devices[slot] == NULL)
		return;

	iter = wdr->devices[slot]->sweep_cb_l;

	/* Simple iterative search */
	while (iter != NULL) {
		if (((wdr_reg_sweep_cb *) iter->data)->cb == cb &&
			((wdr_reg_sweep_cb *) iter->data)->aux == aux) {
			wdr->devices[slot]->sweep_cb_l =
				g_list_remove(wdr->devices[slot]->sweep_cb_l,
							  iter->data);
			return;
		}

		iter = g_list_next(iter);
	}
}

void wdr_sweep_update(wispy_device_registry *wdr, int slot, 
					  int mode, wispy_sample_sweep *sweep) {
	GList *iter;
	wdr_reg_sweep_cb *scb;

	if (slot < 0 || slot > wdr->max_dev)
		return;

	if (wdr->devices[slot] == NULL)
		return;

	iter = wdr->devices[slot]->sweep_cb_l;

	/* Simple iterative search */
	while (iter != NULL) {
		scb = (wdr_reg_sweep_cb *) iter->data;

		/* Only pass along the peak if we've filled the aggregate or
		 * we've got no aggregation */
		if (scb->agg_sweep == NULL || sweep == NULL) {
			(*(scb->cb))(slot, mode, sweep, scb->aux);
		} else {
			wispy_cache_append(scb->agg_sweep, sweep);
			scb->pos_agg++;

			if (scb->pos_agg == scb->num_agg) {
				(*(scb->cb))(slot, mode, scb->agg_sweep->peak, scb->aux);
				wispy_cache_clear(scb->agg_sweep);
				scb->pos_agg = 0;
			}
		}

		// (*(scb->cb))(slot, mode, sweep, scb->aux);
		iter = g_list_next(iter);
	}
}

void wdr_poll(gpointer data, gint source, GdkInputCondition condition) {
	wdr_poll_rec *wdrpr = (wdr_poll_rec *) data;
	wispy_phy *phydev;
	int r;
	char err[WISPY_ERROR_MAX];
	wispy_sample_sweep *sweep;

	if (wdrpr->slot < 0 || wdrpr->slot > wdrpr->wdr->max_dev)
		return;

	if (wdrpr->wdr->devices[wdrpr->slot] == NULL)
		return;

	phydev = wdrpr->wdr->devices[wdrpr->slot]->phydev;

	do {
		r = wispy_phy_poll(phydev);

		if ((r & WISPY_POLL_ERROR)) {
			snprintf(err, WISPY_ERROR_MAX, "Error polling WiSPY device %s: %s",
					 wispy_phy_getname(phydev), wispy_get_error(phydev));
			/* wispy_phy_close(phydev); */

			/* Push the error, this should result in all the graphs removing
			 * their references to us */
			wdr_sweep_update(wdrpr->wdr, wdrpr->slot, r, NULL);

			/* Pop up the error */
			Wispy_Alert_Dialog(err);

			/* remove us from being pollable, free our tracker ref.  No-one should
			 * reference us anymore since we told them there was an error, so we
			 * can drop out entirely */
			gdk_input_remove(wdrpr->poll_tag);
			return;
		} else if ((r & WISPY_POLL_CONFIGURED)) {
			wdr_sweep_update(wdrpr->wdr, wdrpr->slot, r, NULL);
			return;
		} else if ((r & WISPY_POLL_SWEEPCOMPLETE)) {
			sweep = wispy_phy_getsweep(phydev);

			if (sweep == NULL)
				continue;

			wdr_sweep_update(wdrpr->wdr, wdrpr->slot, r, sweep);

			continue;
		}

	} while ((r & WISPY_POLL_ADDITIONAL));

	/* We do nothing otherwise, no events for partial polls */
}

gboolean wdr_netrpoll(GIOChannel *ioch, GIOCondition cond, gpointer data) {
	wdr_poll_rec *wdrpr = (wdr_poll_rec *) data;
	spectool_server *sr;
	int r;
	char err[WISPY_ERROR_MAX];

	if (wdrpr->slot < 0 || wdrpr->slot > wdrpr->wdr->max_dev)
		return;

	if (wdrpr->wdr->netservers[wdrpr->slot] == NULL)
		return;

	sr = wdrpr->wdr->netservers[wdrpr->slot]->srv;

	do {
		r = spectool_netcli_poll(sr, err);

		if (r < 0) {
			Wispy_Alert_Dialog(err);

			wdr_close_net(wdrpr->wdr, wdrpr->slot);
			return;
		}

	} while ((r & SPECTOOL_NETCLI_POLL_ADDITIONAL));
}

GList *wdr_populate_menu(wispy_device_registry *wdr, GtkWidget *menu,
						 int sep, int fillnodev,
						 GCallback cb, void *aux) {
	wdr_menu_rec *r;
	GtkWidget *mi;
	int x;
	GList *cbr = NULL;

	if (wdr->cur_dev <= 0) {
		if (fillnodev) {
			mi = gtk_menu_item_new_with_label("No devices open");
			gtk_widget_set_sensitive(GTK_WIDGET(mi), 0);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
			gtk_widget_show(mi);
			return NULL;
		}

		return NULL;
	}

	for (x = 0; x < wdr->max_dev; x++) {
		if (wdr->devices[x] == NULL)
			continue;

		/* Build the aux */
		r = (wdr_menu_rec *) malloc(sizeof(wdr_menu_rec));
		r->slot = x;
		r->aux = aux;
		r->wdr = wdr;

		/* Add us to the lists */
		cbr = g_list_append(cbr, r);

		/* Increase our refcount - we have to do this so that there is
		 * no race for devices if a graph is being closed */
		wdr_add_ref(wdr, x);

		mi = gtk_menu_item_new_with_label(wispy_phy_getname(wdr->devices[x]->phydev));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
		g_signal_connect_swapped(G_OBJECT(mi), "activate", 
								 G_CALLBACK(cb), r);
		gtk_widget_show(mi);
	}

	if (sep) {
		mi = gtk_separator_menu_item_new();
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
		gtk_widget_show(mi);
	}

	return cbr;
}

void wdr_free_menu_fe(gpointer *data, gpointer *user) {
	wdr_menu_rec *r = (wdr_menu_rec *) data;

	/* Unref the device */
	wdr_del_ref(r->wdr, r->slot);

	/* Free the data hunk */
	free(r);
}

void wdr_free_menu(wispy_device_registry *wdr, GList *gl) {
	g_list_foreach(gl, (GFunc) wdr_free_menu_fe, NULL);
	g_list_free(gl);
}

void wdr_devpicker_onrowactivated(GtkTreeView *treeview, GtkTreePath *path,
								  GtkTreeViewColumn *column, gpointer *aux) {
	GtkTreeModel *model;
	GtkTreeIter iter;
	wdr_gtk_devpicker_aux *dpaux = (wdr_gtk_devpicker_aux *) aux;
	char err[WISPY_ERROR_MAX];

	model = gtk_tree_view_get_model(treeview);

	if (gtk_tree_model_get_iter(model, &iter, path)) {
		void *phyptr;
		int r;

		gtk_tree_model_get(model, &iter, 1, &phyptr, -1);

		/* Open it and add it to the registry */
		r = wdr_open_add(dpaux->wdr, phyptr, err);

		/* Error alert */
		if (r < 0) {
			Wispy_Alert_Dialog(err);
		} else {
			/* Call their callback saying we've picked a device */
			if (dpaux->pickcb != NULL)
				(*(dpaux->pickcb))(r, dpaux->cbaux);
		}

		gtk_widget_destroy(dpaux->picker_win);
	}
}

void wdr_devpicker_destroy(GtkWidget *widget, gpointer *aux) {
	wdr_gtk_devpicker_aux *dpaux = (wdr_gtk_devpicker_aux *) aux;

	wispy_device_scan_free(&(dpaux->devlist));

	free(aux);
}

void wdr_devpicker_populate(wdr_gtk_devpicker_aux *dpaux) {
	int ndevs, x;
	GtkTreeIter iter;
	GtkListStore *model;
	GtkTreeSelection *selection;

	model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
	gtk_tree_view_set_model(GTK_TREE_VIEW(dpaux->treeview), GTK_TREE_MODEL(model));

	/* So that when we replace it in the tree view it gets released */
	g_object_unref(model);

	/*
	if (dpaux->treemodellist != NULL)
		g_free(dpaux->treemodellist);
	*/

	dpaux->treemodellist = model;

	wispy_device_scan_free(&(dpaux->devlist));

	/* Scan for Wispy V1 devices */
	ndevs = wispy_device_scan(&(dpaux->devlist));

	if (ndevs <= 0) {
		gtk_list_store_append(GTK_LIST_STORE(dpaux->treemodellist), &iter);
		gtk_list_store_set(GTK_LIST_STORE(dpaux->treemodellist), &iter, 
						   0, "No WiSPY Devices", 
						   1, NULL,
						   -1);
		gtk_widget_set_sensitive(dpaux->okbutton, 0);
		gtk_widget_set_sensitive(dpaux->scrolled_win, 0);
	} else {
		for (x = 0; x < dpaux->devlist.num_devs; x++) {
			gtk_list_store_append(GTK_LIST_STORE(dpaux->treemodellist), &iter);
			gtk_list_store_set(GTK_LIST_STORE(dpaux->treemodellist), &iter, 
							   0, dpaux->devlist.list[x].name, 
							   1, &(dpaux->devlist.list[x]),
							   -1);

			/* Handle making sure the first item is always selected by
			 * unselecting them all, then selecting one */
			if (x == 0) {
				selection = 
					gtk_tree_view_get_selection(GTK_TREE_VIEW(dpaux->treeview));
				gtk_tree_selection_unselect_all(selection);
				gtk_tree_selection_select_iter(selection, &iter);
			}
		}
		gtk_widget_set_sensitive(dpaux->okbutton, 1);
		gtk_widget_set_sensitive(dpaux->scrolled_win, 1);
	}
}

void wdr_devpicker_button(GtkWidget *widget, gpointer *aux) {
	wdr_gtk_devpicker_aux *dpaux = (wdr_gtk_devpicker_aux *) aux;

	if (widget == dpaux->cancelbutton) {
		gtk_widget_destroy(dpaux->picker_win);
		return;
	}

	if (widget == dpaux->okbutton) {
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GtkTreeIter iter;
		char err[WISPY_ERROR_MAX];

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dpaux->treeview));
		if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
			void *phyptr;
			int r;
			gtk_tree_model_get(model, &iter, 1, &phyptr, -1);

			/* Open it and add it to the registry */
			r = wdr_open_add(dpaux->wdr, phyptr, err);

			if (r < 0) {
				Wispy_Alert_Dialog(err);
			} else {
				/* Call their callback saying we've picked a device */
				if (dpaux->pickcb != NULL)
					(*(dpaux->pickcb))(r, dpaux->cbaux);
			}
		}

		gtk_widget_destroy(dpaux->picker_win);
		return;
	}

	if (widget == dpaux->rescanbutton) {
		wdr_devpicker_populate(dpaux);
		return;
	}
}

void wdr_devpicker_spawn(wispy_device_registry *wdr, 
						 void (*cb)(int, void *), void *aux) {
	GtkWidget *top_window, *scrolled_window;

	GtkWidget *vbox, *hbox;
	GtkWidget *tree_view;

	GtkTreeIter iter;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	int x, ndevs;

	wdr_gtk_devpicker_aux *dpaux = 
		(wdr_gtk_devpicker_aux *) malloc(sizeof(wdr_gtk_devpicker_aux));

	dpaux->wdr = wdr;
	dpaux->pickcb = cb;
	dpaux->cbaux = aux;

	wispy_device_scan_init(&(dpaux->devlist));

	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
								   GTK_POLICY_AUTOMATIC,
								   GTK_POLICY_AUTOMATIC);

	tree_view = gtk_tree_view_new();
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), 
										  tree_view);

	dpaux->treemodellist = NULL;
	dpaux->scrolled_win = scrolled_window;

	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("USB Devices",
													  cell,
													  "text", 0,
													  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view),
								GTK_TREE_VIEW_COLUMN(column));

	g_signal_connect(tree_view, "row-activated", 
					 (GCallback) wdr_devpicker_onrowactivated, dpaux);

	dpaux->treeview = GTK_TREE_VIEW(tree_view);
	dpaux->treecolumn = column;

	top_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(top_window), "WiSPY USB Selection");
	gtk_container_set_border_width(GTK_CONTAINER(top_window), 10);
	gtk_widget_set_size_request(GTK_WIDGET(top_window), 500, 250);

	dpaux->picker_win = top_window;

	g_signal_connect(G_OBJECT(top_window), "destroy",
					 G_CALLBACK(wdr_devpicker_destroy), dpaux);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(top_window), vbox);
	gtk_widget_show(vbox);

	gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 2);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);

	dpaux->okbutton = gtk_toggle_button_new_with_label("Enable");
	gtk_signal_connect(GTK_OBJECT(dpaux->okbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_devpicker_button), dpaux);

	dpaux->cancelbutton = gtk_toggle_button_new_with_label("Cancel");
	gtk_signal_connect(GTK_OBJECT(dpaux->cancelbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_devpicker_button), dpaux);

	dpaux->rescanbutton = gtk_toggle_button_new_with_label("Re-Scan");
	gtk_signal_connect(GTK_OBJECT(dpaux->rescanbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_devpicker_button), dpaux);

	gtk_box_pack_start(GTK_BOX(hbox), dpaux->okbutton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), dpaux->cancelbutton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), dpaux->rescanbutton, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	wdr_devpicker_populate(dpaux);

	gtk_widget_show(tree_view);
	gtk_widget_show(dpaux->okbutton);
	gtk_widget_show(dpaux->cancelbutton);
	gtk_widget_show(dpaux->rescanbutton);

	gtk_widget_show(scrolled_window);
	gtk_widget_show(top_window);
}

void wdr_netmanager_destroy(GtkWidget *widget, gpointer *aux) {
	wdr_gtk_netmanager_aux *nmaux = (wdr_gtk_netmanager_aux *) aux;
	int x = 0;

	nmaux->wdr->netmanager = NULL;

	if (nmaux->timer_ref > 0)
		g_source_remove(nmaux->timer_ref);

	for (x = 0; x < nmaux->wdr->max_srv; x++) {
		if (nmaux->wdr->netservers[x] != NULL) {
			gtk_tree_row_reference_free(nmaux->wdr->netservers[x]->tree_row_ref);
			nmaux->wdr->netservers[x]->tree_row_ref = NULL;
		}
	}

	free(nmaux);
}

gint wdr_netmanager_populate(wdr_gtk_netmanager_aux *nmaux) {
	int nsrv, x;
	GtkTreeIter iter, child;
	GtkTreeStore *store;
	GtkTreeSelection *selection;
	spectool_net_dev *sni = NULL;
	GtkTreePath *path;
	gboolean expanded;

	GList *devrows, *dri;

	if (nmaux == NULL)
		return FALSE;

	if (nmaux->treestore == NULL) {
		store = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_UINT);
		gtk_tree_view_set_model(GTK_TREE_VIEW(nmaux->treeview), GTK_TREE_MODEL(store));
		g_object_unref(store);
		nmaux->treestore = store;
		nmaux->noservers = NULL;
	} else {
		store = nmaux->treestore;
	}

	if (nmaux->wdr->cur_srv == 0) {
		if (nmaux->noservers == NULL) {
			gtk_tree_store_append(store, &iter, NULL);
			gtk_tree_store_set(store, &iter,
							   0, "Not connected to any servers...",
							   1, -1,
							   2, 0,
							   -1);
			nmaux->noservers =
				gtk_tree_row_reference_new(GTK_TREE_MODEL(store),
							gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter));
		}
	} else if (nmaux->noservers != NULL) {
		/* Get rid of our "no servers" element */
		if (gtk_tree_row_reference_valid(nmaux->noservers) &&
			gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter,
					gtk_tree_row_reference_get_path(nmaux->noservers))) {
			gtk_tree_store_remove(store, &iter);

			gtk_tree_row_reference_free(nmaux->noservers);
			nmaux->noservers = NULL;
		}
	}

	for (x = 0; x < nmaux->wdr->max_srv; x++) {
		if (nmaux->wdr->netservers[x] != NULL) {
			if (nmaux->wdr->netservers[x]->tree_row_ref == NULL ||
				!gtk_tree_row_reference_valid(nmaux->wdr->netservers[x]->tree_row_ref)) {
				gtk_tree_store_append(store, &iter, NULL);
				gtk_tree_store_set(store, &iter,
					0, spectool_netcli_geturl(nmaux->wdr->netservers[x]->srv),
					1, x, 
	 				2, 0,
					-1);

				gtk_tree_row_reference_free(nmaux->wdr->netservers[x]->tree_row_ref);
				path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
				nmaux->wdr->netservers[x]->tree_row_ref =
					gtk_tree_row_reference_new(GTK_TREE_MODEL(store), path);
				gtk_tree_path_free(path);
			} else {
				/* search the child nodes to see what devices are valid and if we
				 * need to redo them */
				gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter,
				gtk_tree_row_reference_get_path(nmaux->wdr->netservers[x]->tree_row_ref));
			}

			sni = nmaux->wdr->netservers[x]->srv->devlist;

			expanded = 
				gtk_tree_view_row_expanded(nmaux->treeview,
										   gtk_tree_row_reference_get_path(
											nmaux->wdr->netservers[x]->tree_row_ref));

			/* If we have no devices, clear out the list and say something */
			if (sni == NULL) {
				while (gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &child, &iter)) {
					gtk_tree_store_remove(store, &child);
				}

				gtk_tree_store_append(store, &child, &iter);
				if (spectool_netcli_getstate(nmaux->wdr->netservers[x]->srv) == 
					SPECTOOL_NET_STATE_CONNECTED) {
					gtk_tree_store_set(store, &child,
									   0, "Waiting for device list (Press update)...",
									   1, -1,
									   2, 0,
									   -1);
				} else if (spectool_netcli_getstate(nmaux->wdr->netservers[x]->srv) == 
						   SPECTOOL_NET_STATE_ERROR) {
					gtk_tree_store_set(store, &child,
									   0, "Server error...",
									   1, -1,
									   2, 0,
									   -1);
				} else {
					gtk_tree_store_set(store, &child,
									   0, "No devices found...",
									   1, -1,
									   2, 0,
									   -1);
				}

				if (expanded) 
					gtk_tree_view_expand_row(nmaux->treeview,
											 gtk_tree_row_reference_get_path(
										nmaux->wdr->netservers[x]->tree_row_ref),
											 TRUE);

				continue;
			} else {
				/* Look for devices we need to remove */
				devrows = NULL;
				if (gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &child, &iter)) {
					unsigned int devid;

					do {
						int matched = 0;
						gtk_tree_model_get(GTK_TREE_MODEL(store), &child, 
										   2, &devid,
										   -1);


						sni = nmaux->wdr->netservers[x]->srv->devlist;
						while (sni != NULL) {
							if (sni->device_id == devid) {
								matched = 1;
								break;
							}

							sni = sni->next;
						}

						/* Make a reference to it */
						if (matched == 0) {
							path = 
								gtk_tree_model_get_path(GTK_TREE_MODEL(store), &child);
							devrows = g_list_append(devrows,
									gtk_tree_row_reference_new(GTK_TREE_MODEL(store),
															   path));
							gtk_tree_path_free(path);
						}
					} while(gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &child));

					/* remove all the references */
					dri = devrows;
					while (devrows) {
						gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &child,
								gtk_tree_row_reference_get_path(devrows->data));

						gtk_tree_store_remove(store, &child);

						gtk_tree_row_reference_free(devrows->data);
						devrows = g_list_next(devrows);
					}
					g_list_free(dri);
				}

				/* Add devices we don't have */
				sni = nmaux->wdr->netservers[x]->srv->devlist;
				while (sni != NULL) {
					int matched = 0;
					if (gtk_tree_model_iter_children(GTK_TREE_MODEL(store), 
													 &child, &iter)) {
						unsigned int devid;

						do {
							gtk_tree_model_get(GTK_TREE_MODEL(store), &child, 
											   2, &devid,
											   -1);

							if (sni->device_id == devid) {
								matched = 1;
								break;
							}

						} while(gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &child));
					}

					if (matched == 0) {
						gtk_tree_store_append(store, &child, &iter);
						gtk_tree_store_set(store, &child,
										   0, sni->device_name,
										   1, x,
										   2, sni->device_id,
										   -1);
					}

					sni = sni->next;
				}

				if (expanded) 
					gtk_tree_view_expand_row(nmaux->treeview,
											 gtk_tree_row_reference_get_path(
										nmaux->wdr->netservers[x]->tree_row_ref),
											 TRUE);

			}
		}
	}

	return TRUE;
}

gboolean wdr_netmanager_selection(GtkTreeSelection *selection,
								  GtkTreeModel     *model,
								  GtkTreePath      *path,
								  gboolean          path_currently_selected,
								  gpointer          aux) {
	GtkTreeIter iter;
	wdr_gtk_netmanager_aux *nmaux = (wdr_gtk_netmanager_aux *) aux;
	int srvid;
	unsigned int devid;

	if (gtk_tree_model_get_iter(model, &iter, path)) {
		gtk_tree_model_get(model, &iter, 1, &srvid, 2, &devid, -1);

		if (!path_currently_selected) {
			if (srvid >= 0 && devid == 0) {
				gtk_widget_set_sensitive(GTK_WIDGET(nmaux->dconbutton), 1);
				gtk_widget_set_sensitive(GTK_WIDGET(nmaux->openbutton), 0);
			} else if (srvid >= 0) {
				gtk_widget_set_sensitive(GTK_WIDGET(nmaux->dconbutton), 0);
				gtk_widget_set_sensitive(GTK_WIDGET(nmaux->openbutton), 1);
			} else {
				return FALSE;
			}
		}
	}

	return TRUE;
}

void wdr_netmanager_button(GtkWidget *widget, gpointer *aux) {
	wdr_gtk_netmanager_aux *nmaux = (wdr_gtk_netmanager_aux *) aux;

	if (widget == nmaux->closebutton) {
		gtk_widget_destroy(nmaux->picker_win);
		return;
	} else if (widget == nmaux->addbutton) {
		wdr_netentry_spawn(nmaux->wdr);
	} else if (widget == nmaux->openbutton) {
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GtkTreeIter iter;
		char err[WISPY_ERROR_MAX];

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(nmaux->treeview));
		if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
			wispy_phy *phyptr;
			int srvid;
			unsigned int devid;
			int r;

			gtk_tree_model_get(model, &iter, 1, &srvid, 2, &devid, -1);

			if (srvid < 0 || devid == 0)
				return;

			/* Open it and add it to the registry */
			phyptr = 
				spectool_netcli_enabledev(nmaux->wdr->netservers[srvid]->srv,
										  devid, err);

			if (phyptr == NULL) {
				Wispy_Alert_Dialog(err);
			} else {
				r = wdr_open_phy(nmaux->wdr, phyptr, err);

				if (r < 0) {
					Wispy_Alert_Dialog(err);
				} else {
					/* Call their callback saying we've picked a device */
					if (nmaux->pickcb != NULL)
						(*(nmaux->pickcb))(r, nmaux->cbaux);
				}
			}

			gtk_widget_destroy(nmaux->picker_win);
			return;
		}
	}
}

void wdr_netmanager_onrowactivated(GtkTreeView *treeview, GtkTreePath *path,
								  GtkTreeViewColumn *column, gpointer *aux) {
	wdr_gtk_netmanager_aux *nmaux = (wdr_gtk_netmanager_aux *) aux;

	wdr_netmanager_button(nmaux->openbutton, aux);
}

void wdr_netmanager_spawn(wispy_device_registry *wdr,
						 void (*cb)(int, void *),
						 void *aux) {
	GtkWidget *top_window, *scrolled_window;

	GtkWidget *vbox, *hbox;
	GtkWidget *tree_view;

	GtkTreeIter iter;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;

	int x, ndevs;

	wdr_gtk_netmanager_aux *nmaux = 
		(wdr_gtk_netmanager_aux *) malloc(sizeof(wdr_gtk_netmanager_aux));

	nmaux->wdr = wdr;
	nmaux->pickcb = cb;
	nmaux->cbaux = aux;

	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
								   GTK_POLICY_AUTOMATIC,
								   GTK_POLICY_AUTOMATIC);

	tree_view = gtk_tree_view_new();
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), 
										  tree_view);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
	gtk_tree_selection_set_select_function(selection, wdr_netmanager_selection, 
										   nmaux, NULL);

	nmaux->treestore = NULL;
	nmaux->scrolled_win = scrolled_window;

	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Network Devices",
													  cell,
													  "text", 0,
													  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view),
								GTK_TREE_VIEW_COLUMN(column));

	g_signal_connect(tree_view, "row-activated", 
					 (GCallback) wdr_netmanager_onrowactivated, nmaux);

	nmaux->treeview = GTK_TREE_VIEW(tree_view);
	nmaux->treecolumn = column;

	top_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(top_window), "Spectool Network Devices");
	gtk_container_set_border_width(GTK_CONTAINER(top_window), 10);
	gtk_widget_set_size_request(GTK_WIDGET(top_window), 500, 250);

	nmaux->picker_win = top_window;

	g_signal_connect(G_OBJECT(top_window), "destroy",
					 G_CALLBACK(wdr_netmanager_destroy), nmaux);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(top_window), vbox);
	gtk_widget_show(vbox);

	gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 2);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);

	nmaux->addbutton = gtk_button_new_with_label("Add Server");
	gtk_signal_connect(GTK_OBJECT(nmaux->addbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_netmanager_button), nmaux);

	nmaux->dconbutton = gtk_button_new_with_label("Disconnect Server");
	gtk_signal_connect(GTK_OBJECT(nmaux->dconbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_netmanager_button), nmaux);

	nmaux->openbutton = gtk_button_new_with_label("Open Device");
	gtk_signal_connect(GTK_OBJECT(nmaux->openbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_netmanager_button), nmaux);

	nmaux->closebutton = gtk_button_new_with_label("Close");
	gtk_signal_connect(GTK_OBJECT(nmaux->closebutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_netmanager_button), nmaux);

	gtk_widget_set_sensitive(nmaux->dconbutton, 0);
	gtk_widget_set_sensitive(nmaux->openbutton, 0);

	gtk_box_pack_start(GTK_BOX(hbox), nmaux->addbutton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), nmaux->dconbutton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), nmaux->openbutton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), nmaux->closebutton, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	wdr_netmanager_populate(nmaux);

	gtk_widget_show(tree_view);
	gtk_widget_show(nmaux->addbutton);
	gtk_widget_show(nmaux->dconbutton);
	gtk_widget_show(nmaux->closebutton);
	gtk_widget_show(nmaux->openbutton);

	gtk_widget_show(scrolled_window);
	gtk_widget_show(top_window);

	wdr->netmanager = nmaux;

	nmaux->timer_ref = g_timeout_add(2000,
									 (GSourceFunc) wdr_netmanager_populate,
									 nmaux);
}

void wdr_netentry_destroy(GtkWidget *widget, gpointer *aux) {
	wdr_gtk_netentry_aux *npaux = (wdr_gtk_netentry_aux *) aux;

	free(aux);
}

void wdr_netentry_button(GtkWidget *widget, gpointer *aux) {
	wdr_gtk_netentry_aux *npaux = (wdr_gtk_netentry_aux *) aux;

	if (widget == npaux->cancelbutton) {
		gtk_widget_destroy(npaux->picker_win);
		return;
	}

	if (widget == npaux->okbutton) {
		char err[WISPY_ERROR_MAX];
		char url[1024];
		int r;

		snprintf(url, 1024, "tcp://%s:%s",
				 gtk_entry_get_text(GTK_ENTRY(npaux->hostentry)),
				 gtk_entry_get_text(GTK_ENTRY(npaux->portentry)));

		if (wdr_open_net(npaux->wdr, url, err) < 0) {
			Wispy_Alert_Dialog(err);
		}

		wdr_netmanager_populate(npaux->wdr->netmanager);

		gtk_widget_destroy(npaux->picker_win);
		return;
	}
}

void wdr_netentry_spawn(wispy_device_registry *wdr) {
	GtkWidget *top_window;

	GtkWidget *vbox, *hbox;

	GtkWidget *label;

	char port[16];

	wdr_gtk_netentry_aux *npaux = 
		(wdr_gtk_netentry_aux *) malloc(sizeof(wdr_gtk_netentry_aux));

	npaux->wdr = wdr;

	top_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(top_window), "Spectool Network");
	gtk_container_set_border_width(GTK_CONTAINER(top_window), 10);
	gtk_widget_set_size_request(GTK_WIDGET(top_window), 500, 250);

	npaux->picker_win = top_window;

	g_signal_connect(G_OBJECT(top_window), "destroy",
					 G_CALLBACK(wdr_netentry_destroy), npaux);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(top_window), vbox);
	gtk_widget_show(vbox);

	label = gtk_label_new("Host:");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 2);

	npaux->hostentry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(npaux->hostentry), "host");
	gtk_entry_set_editable(GTK_ENTRY(npaux->hostentry), 1);
	gtk_widget_show(npaux->hostentry);
	gtk_box_pack_start(GTK_BOX(vbox), npaux->hostentry, FALSE, TRUE, 2);

	label = gtk_label_new("Port:");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 2);

	npaux->portentry = gtk_entry_new();
	snprintf(port, 16, "%hd", WISPY_NET_DEFAULT_PORT);
	gtk_entry_set_text(GTK_ENTRY(npaux->portentry), port);
	gtk_entry_set_editable(GTK_ENTRY(npaux->portentry), 1);
	gtk_widget_show(npaux->portentry);
	gtk_box_pack_start(GTK_BOX(vbox), npaux->portentry, FALSE, TRUE, 2);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);

	npaux->okbutton = gtk_toggle_button_new_with_label("Enable");
	gtk_signal_connect(GTK_OBJECT(npaux->okbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_netentry_button), npaux);

	npaux->cancelbutton = gtk_toggle_button_new_with_label("Cancel");
	gtk_signal_connect(GTK_OBJECT(npaux->cancelbutton), "clicked",
					   GTK_SIGNAL_FUNC(wdr_netentry_button), npaux);

	gtk_box_pack_start(GTK_BOX(hbox), npaux->okbutton, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), npaux->cancelbutton, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	gtk_widget_show(npaux->okbutton);
	gtk_widget_show(npaux->cancelbutton);

	gtk_widget_show(top_window);
}

