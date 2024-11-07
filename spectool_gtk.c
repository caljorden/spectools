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

#include <stdio.h>
#include <usb.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <locale.h>

#include "config.h"

#include "spectool_gtk.h"
#include "spectool_container.h"
#include "spectool_gtk_planar.h"
#include "spectool_gtk_spectral.h"
#include "spectool_gtk_topo.h"
#include "spectool_gtk_channel.h"

#define GETTEXT_PACKAGE	"spectool_gtk"
#define LOCALEDIR		"/usr/share/locale/spectool_gtk"

void Spectool_Alert_Dialog(char *text) {
	GtkWidget *dialog, *okbutton, *label;

	label = gtk_label_new(text);
	dialog = gtk_dialog_new_with_buttons ("SpecTool", NULL,
										  GTK_DIALOG_MODAL, NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 300, 100);
	okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog), 
									  GTK_STOCK_OK, GTK_RESPONSE_NONE);
	g_signal_connect_swapped (GTK_OBJECT (dialog), 
							  "response", G_CALLBACK (gtk_widget_destroy), 
							  GTK_OBJECT (dialog));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_widget_grab_focus(okbutton);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);
	gtk_widget_show_all(dialog);
}

void Spectool_Help_Dialog(char *title, char *text) {
	GtkWidget *dialog, *scroll, *okbutton, *label;

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), text);
	dialog = gtk_dialog_new_with_buttons (title, NULL,
										  GTK_DIALOG_MODAL, NULL);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 300, 100);
	okbutton = gtk_dialog_add_button (GTK_DIALOG (dialog), 
									  GTK_STOCK_OK, GTK_RESPONSE_NONE);
	g_signal_connect_swapped (GTK_OBJECT (dialog), 
							  "response", G_CALLBACK (gtk_widget_destroy), 
							  GTK_OBJECT (dialog));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_widget_grab_focus(okbutton);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);
	gtk_widget_show_all(dialog);
}

typedef struct _wg_aux {
	spectool_device_registry *wdr;
	GtkWidget *main_vbox;
	GtkWidget *main_window;
	GtkWidget *notebook;
	int num_tabs;
} wg_aux;

typedef struct _nb_aux {
	GtkWidget *nbvbox, *nodev_vbox;
	GtkWidget *nblabel;
	wg_aux *auxptr;

	GtkWidget *planar, *spectral, *topo, *channel;
	SpectoolWidgetController *p_con, *s_con, *t_con;

	gint pagenum;

	SpectoolChannelOpts *chanopts;

	spectool_phy *phydev;
	int wdr_slot;
	GList *wdr_menu;
} nb_aux;

/* fwd defs */
static nb_aux *build_nb_page(GtkWidget *notebook, wg_aux *auxptr);

static void main_devopen(int slot, void *aux) {
	nb_aux *nbaux = (nb_aux *) aux;

	g_return_if_fail(aux != NULL);

	spectool_widget_bind_dev(nbaux->planar, nbaux->auxptr->wdr, slot);
	spectool_widget_bind_dev(nbaux->topo, nbaux->auxptr->wdr, slot);
	spectool_widget_bind_dev(nbaux->spectral, nbaux->auxptr->wdr, slot);
	spectool_widget_bind_dev(nbaux->channel, nbaux->auxptr->wdr, slot);

	nbaux->phydev = wdr_get_phy(nbaux->auxptr->wdr, slot);

	gtk_label_set_text(GTK_LABEL(nbaux->nblabel), spectool_phy_getname(nbaux->phydev));

	gtk_widget_hide(nbaux->nodev_vbox);

	gtk_widget_show(nbaux->planar);
	gtk_widget_show(nbaux->topo);
	gtk_widget_show(nbaux->spectral);
	gtk_widget_show(nbaux->channel);
	gtk_widget_show(nbaux->p_con->evbox);
	gtk_widget_show(nbaux->t_con->evbox);
	gtk_widget_show(nbaux->s_con->evbox);
}

/* Picks an existing device from the dynamic WDR list in the 
 * popup menu */
static void main_menu_devpicker(gpointer *aux) {
	wdr_menu_rec *r;
	nb_aux *nbaux;

	g_return_if_fail(aux != NULL);

	/* We're opening a known device, call it directly */
	r = (wdr_menu_rec *) aux;
	nbaux = (nb_aux *) r->aux;

	if (r->slot < 0) {
		wdr_devpicker_spawn(r->wdr, main_devopen, nbaux);
		return;
	}

	main_devopen(r->slot, nbaux);
}

/* Spawns the device picker window from the popup menu */
static void main_menu_spawnpicker(gpointer *aux) {
	nb_aux *nbaux = (nb_aux *) aux;

	g_return_if_fail(aux != NULL);

	wdr_devpicker_spawn(nbaux->auxptr->wdr, main_devopen, nbaux);
}

/* Spawns the network picker window from the popup menu */
static void main_menu_spawnnetpicker(gpointer *aux) {
	nb_aux *nbaux = (nb_aux *) aux;

	g_return_if_fail(aux != NULL);

	wdr_netmanager_spawn(nbaux->auxptr->wdr,
						 main_devopen, nbaux);
}

/* Called when the popup button menu goes away, responsible for freeing the
 * dynamic wdr menu items (which in turn decrements the use counter so we can
 * release devices if we need to) */
static void main_menu_destroy(gpointer *aux) {
	nb_aux *nbaux = (nb_aux *) aux;

	g_return_if_fail(aux != NULL);

	wdr_free_menu(nbaux->auxptr->wdr, nbaux->wdr_menu);
	nbaux->wdr_menu = NULL;
}

static gboolean main_nodev_menu_button_press(gpointer *aux,
											 GdkEvent *event) {
	GtkWidget *menu;
	nb_aux *nbaux = (nb_aux *) aux;

	g_return_val_if_fail(aux != NULL, FALSE);

	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *bevent = (GdkEventButton *) event;

		menu = gtk_menu_new();
		gtk_widget_show(menu);

		/* Add the WDR generated menus, if any */
		nbaux->wdr_menu = 
			wdr_populate_menu(nbaux->auxptr->wdr,
							  GTK_WIDGET(menu),
							  0, 1,
							  G_CALLBACK(main_menu_devpicker),
							  nbaux);

		/* Set the cleanup function for the dynamic menu generation */
		g_signal_connect_swapped(G_OBJECT(menu), "selection-done",
								 G_CALLBACK(main_menu_destroy),
								 nbaux);

		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 
					   bevent->button, bevent->time);

		return TRUE;
	}

	return FALSE;
}

static void del_tab(nb_aux *page) {
	wg_aux *auxptr = page->auxptr;

	/* Tear down all the graphs */
	free(page->p_con);
	free(page->s_con);
	free(page->t_con);

	gtk_widget_destroy(page->planar);
	gtk_widget_destroy(page->spectral);
	gtk_widget_destroy(page->topo);
	gtk_widget_destroy(page->channel);

	/* delete the page */
	gtk_notebook_remove_page(GTK_NOTEBOOK(auxptr->notebook), page->pagenum);

	/* Delete the vbox, should cascade down the rest */
	gtk_widget_destroy(page->nbvbox);

	if (--auxptr->num_tabs == 0) {
		build_nb_page(auxptr->notebook, auxptr);
	}

	free(page);
}

static void close_nb_button(GtkWidget *widget, gpointer *aux) {
	nb_aux *nbaux = (nb_aux *) aux;

	del_tab(nbaux);
}

static nb_aux *build_nb_page(GtkWidget *notebook, wg_aux *auxptr) {
	nb_aux *nbaux = (nb_aux *) malloc(sizeof(nb_aux));
	GtkWidget *temp, *hbox, *arrow, *closebutton, *closeicon;

	nbaux->auxptr = auxptr;

	/* Default label for the tab, packed into a hbox */
	hbox = gtk_hbox_new(FALSE, 1);
	gtk_widget_show(hbox);
	nbaux->nblabel = gtk_label_new("No device");
	gtk_box_pack_start(GTK_BOX(hbox), nbaux->nblabel, FALSE, FALSE, 0);

	closebutton = gtk_button_new();
	closeicon = gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_container_add(GTK_CONTAINER(closebutton), closeicon);
	gtk_button_set_relief(GTK_BUTTON(closebutton), GTK_RELIEF_NONE);
	gtk_widget_show(closebutton);
	gtk_widget_show(closeicon);
	gtk_box_pack_end(GTK_BOX(hbox), closebutton, FALSE, FALSE, 0);

	gtk_signal_connect(GTK_OBJECT(closebutton), "clicked",
					   GTK_SIGNAL_FUNC(close_nb_button), nbaux);

	nbaux->nbvbox = gtk_vbox_new(FALSE, 1);
	nbaux->pagenum = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), nbaux->nbvbox, 
											  hbox);

	/* Make the device picker buttons and label */
	nbaux->nodev_vbox = gtk_vbox_new(FALSE, 0);
	temp = gtk_label_new("No device selected...");
	gtk_box_pack_start(GTK_BOX(nbaux->nodev_vbox), temp, FALSE, FALSE, 4);
	gtk_widget_show(temp);

	/* Build the arrow for using an open device */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(nbaux->nodev_vbox), hbox, FALSE, FALSE, 2);

	temp = gtk_button_new_with_label("Open Device");
	g_signal_connect_swapped(G_OBJECT(temp), "clicked",
							 G_CALLBACK(main_menu_spawnpicker), 
							 nbaux);
	gtk_box_pack_start(GTK_BOX(hbox), temp, TRUE, TRUE, 0);
	gtk_widget_show(temp);

	temp = gtk_button_new();
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(temp), arrow);
	g_signal_connect_swapped(G_OBJECT(temp), "event", 
							 G_CALLBACK(main_nodev_menu_button_press),
							 nbaux);
	gtk_box_pack_start(GTK_BOX(hbox), temp, FALSE, FALSE, 0);
	gtk_widget_show(temp);
	gtk_widget_show(hbox);
	gtk_widget_show(arrow);

	temp = gtk_button_new_with_label("Open Network Device");
	g_signal_connect_swapped(G_OBJECT(temp), "clicked",
							 G_CALLBACK(main_menu_spawnnetpicker), 
							 nbaux);
	gtk_box_pack_start(GTK_BOX(nbaux->nodev_vbox), temp, FALSE, FALSE, 2);
	gtk_widget_show(temp);

	/*
	temp = gtk_button_new_with_label("Close Tab");
	g_signal_connect_swapped(G_OBJECT(temp), "clicked",
							 G_CALLBACK(gtk_widget_destroy), G_OBJECT());
	gtk_box_pack_start(GTK_BOX(nbaux->nodev_vbox), temp, FALSE, FALSE, 2);
	gtk_widget_show(temp);
	*/

	gtk_box_pack_start(GTK_BOX(nbaux->nbvbox), nbaux->nodev_vbox, FALSE, FALSE, 0);

	gtk_widget_show(nbaux->nodev_vbox);

	gtk_widget_show(nbaux->nblabel);

	/* Make the inactive devices */
	nbaux->chanopts = (SpectoolChannelOpts *) malloc(sizeof(SpectoolChannelOpts));
	spectoolchannelopts_init(nbaux->chanopts);

	nbaux->channel = spectool_channel_new();
	spectool_widget_link_channel(nbaux->channel, nbaux->chanopts);
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->channel, FALSE, FALSE, 0);

	nbaux->planar = spectool_planar_new();
	spectool_widget_link_channel(nbaux->planar, nbaux->chanopts);
	nbaux->p_con = spectool_widget_buildcontroller(GTK_WIDGET(nbaux->planar));
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->planar, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->p_con->evbox, FALSE, FALSE, 2);

	nbaux->topo = spectool_topo_new();
	spectool_widget_link_channel(nbaux->topo, nbaux->chanopts);
	nbaux->t_con = spectool_widget_buildcontroller(GTK_WIDGET(nbaux->topo));
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->topo, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->t_con->evbox, FALSE, FALSE, 2);

	nbaux->spectral = spectool_spectral_new();
	spectool_widget_link_channel(nbaux->spectral, nbaux->chanopts);
	nbaux->s_con = spectool_widget_buildcontroller(GTK_WIDGET(nbaux->spectral));
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->spectral, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(nbaux->nbvbox), nbaux->s_con->evbox, FALSE, FALSE, 2);

	spectool_channel_append_update(nbaux->channel, nbaux->planar);
	spectool_channel_append_update(nbaux->channel, nbaux->topo);
	spectool_channel_append_update(nbaux->channel, nbaux->spectral);

	gtk_widget_show(nbaux->nbvbox);

	auxptr->num_tabs++;

	return nbaux;
}

static void add_tab(gpointer *data, gpointer *aux) {
	wg_aux *auxptr = (wg_aux *) data;
	nb_aux *nbaux;

	nbaux = build_nb_page(auxptr->notebook, auxptr);
}

static GtkItemFactoryEntry main_menu_items[] = {
	{ "/_SpecAn",			NULL,			NULL,	0, "<Branch>" },
	{ "/SpecAn/Add Device", "<control>A", add_tab, 0, "<Item>" },
	{ "/SpecAn/_Quit",		"<control>Q",	gtk_main_quit, 0, "<Item>" },
};

static gint nmain_menu_items = 
	sizeof (main_menu_items) / sizeof (main_menu_items[0]);

int main(int argc, char *argv[]) {
	GtkWidget *window, *vbox, *notebook;
	wg_aux auxptr;
	nb_aux *nbfirst;

#if 0
	GtkWidget *menubar, *menu, *mn_spectool, *menuitem;
#endif

	GtkWidget *menubar;

	GtkItemFactory *item_factory;
	GtkAccelGroup *accel_group;

	spectool_device_registry wdr;

	int x;

	char errstr[SPECTOOL_ERROR_MAX];

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	gtk_init(&argc, &argv);

	wdr_init(&wdr);

	/* Turn on broadcast autodetection */
	if (wdr_enable_bcast(&wdr, errstr) < 0) {
		Spectool_Alert_Dialog(errstr);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(window), "WiSPY");

	gtk_window_set_default_size(GTK_WINDOW(window), 750, 650);

	g_signal_connect(G_OBJECT (window), "delete_event",
					 G_CALLBACK (gtk_main_quit), NULL);
	g_signal_connect(G_OBJECT (window), "destroy",
					 G_CALLBACK (gtk_main_quit), NULL);

	accel_group = gtk_accel_group_new();

	item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
										accel_group);
	gtk_item_factory_create_items(item_factory, nmain_menu_items,
								  main_menu_items, &auxptr);
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
	menubar = gtk_item_factory_get_widget(item_factory, "<main>");

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox); 

	gtk_widget_show(vbox);

	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
	gtk_widget_show(menubar);

	/* Make a notebook */
	notebook = gtk_notebook_new();
	gtk_box_pack_end(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	gtk_widget_show(notebook);

	auxptr.wdr = &wdr;
	auxptr.main_vbox = vbox;
	auxptr.main_window = window;
	auxptr.notebook = notebook;
	auxptr.num_tabs = 0;

	nbfirst = build_nb_page(notebook, &auxptr);

	gtk_widget_show(window);

	gtk_main();

	return 0;
}	

