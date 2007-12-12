/*
 * Spectools hildonized interface
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

#include <hildon/hildon-program.h>
#include <gtk/gtkmain.h>
#include <gdk/gdkkeysyms.h>

#include "config.h"

#include "spectool_gtk.h"
#include "spectool_container.h"
#include "spectool_gtk_planar.h"
#include "spectool_gtk_spectral.h"
#include "spectool_gtk_topo.h"
#include "spectool_gtk_channel.h"

#define GETTEXT_PACKAGE	"spectool_gtk"
#define LOCALEDIR		"/usr/share/locale/spectool_gtk"

/* One of the few globals */
int fullscreen;

void Wispy_Alert_Dialog(char *text) {
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

void Wispy_Help_Dialog(char *title, char *text) {
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
	wispy_device_registry *wdr;
	GtkWidget *main_vbox, *nodev_vbox;

	GtkWidget *planar, *spectral, *topo, *channel;
	GtkWidget *mi_planar, *mi_spectral, *mi_topo, *mi_close;

	WispyChannelOpts *chanopts;

	wispy_phy *phydev;
	int wdr_slot;
	GList *wdr_menu;
} wg_aux;

static void kick_display_items(gpointer *aux) {
	wg_aux *auxptr = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(auxptr->phydev != NULL);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(auxptr->mi_planar)))
		gtk_widget_show(auxptr->planar);
	else
		gtk_widget_hide(auxptr->planar);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(auxptr->mi_topo)))
		gtk_widget_show(auxptr->topo);
	else
		gtk_widget_hide(auxptr->topo);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(auxptr->mi_spectral)))
		gtk_widget_show(auxptr->spectral);
	else
		gtk_widget_hide(auxptr->spectral);

	gtk_widget_show(auxptr->channel);
}

static void main_devopen(int slot, void *aux) {
	wg_aux *auxptr = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);

	wispy_widget_bind_dev(auxptr->planar, auxptr->wdr, slot);
	wispy_widget_bind_dev(auxptr->topo, auxptr->wdr, slot);
	wispy_widget_bind_dev(auxptr->spectral, auxptr->wdr, slot);
	wispy_widget_bind_dev(auxptr->channel, auxptr->wdr, slot);

	auxptr->phydev = wdr_get_phy(auxptr->wdr, slot);

	gtk_widget_hide(auxptr->nodev_vbox);

	gtk_widget_set_sensitive(auxptr->mi_close, 1);

	kick_display_items(aux);
}

/* Picks an existing device from the dynamic WDR list in the 
 * popup menu */
static void main_menu_devpicker(gpointer *aux) {
	wdr_menu_rec *r;
	wg_aux *wgaux;

	g_return_if_fail(aux != NULL);

	/* We're opening a known device, call it directly */
	r = (wdr_menu_rec *) aux;
	wgaux = (wg_aux *) r->aux;

	if (r->slot < 0) {
		wdr_devpicker_spawn(r->wdr, main_devopen, wgaux);
		return;
	}

	main_devopen(r->slot, wgaux);
}

/* Spawns the device picker window from the popup menu */
static void main_menu_spawnpicker(gpointer *aux) {
	wg_aux *wgaux = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);

	wdr_devpicker_spawn(wgaux->wdr, main_devopen, wgaux);
}

/* Spawns the network picker window from the popup menu */
static void main_menu_spawnnetpicker(gpointer *aux) {
	wg_aux *wgaux = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);

	wdr_netmanager_spawn(wgaux->wdr,
			main_devopen, wgaux);
}

/* Called when the popup button menu goes away, responsible for freeing the
 * dynamic wdr menu items (which in turn decrements the use counter so we can
 * release devices if we need to) */
static void main_menu_destroy(gpointer *aux) {
	wg_aux *wgaux = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);

	wdr_free_menu(wgaux->wdr, wgaux->wdr_menu);
	wgaux->wdr_menu = NULL;
}

static gboolean main_nodev_menu_button_press(gpointer *aux,
											 GdkEvent *event) {
	GtkWidget *menu;
	wg_aux *wgaux = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);

	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *bevent = (GdkEventButton *) event;

		menu = gtk_menu_new();
		gtk_widget_show(menu);

		/* Add the WDR generated menus, if any */
		wgaux->wdr_menu = 
			wdr_populate_menu(wgaux->wdr,
							  GTK_WIDGET(menu),
							  0, 1,
							  G_CALLBACK(main_menu_devpicker),
							  wgaux);

		/* Set the cleanup function for the dynamic menu generation */
		g_signal_connect_swapped(G_OBJECT(menu), "selection-done",
								 G_CALLBACK(main_menu_destroy),
								 wgaux);

		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 
					   bevent->button, bevent->time);

		return TRUE;
	}

	return FALSE;
}

/* Wedge function to make the spectools components, unlinked from a
 * device.  Used to init new, and close existing, windows */
void create_spectools(wg_aux *auxptr) {
	/* No device */
	auxptr->phydev = NULL;

	/* Make the inactive devices */
	auxptr->chanopts = (WispyChannelOpts *) malloc(sizeof(WispyChannelOpts));
	wispychannelopts_init(auxptr->chanopts);

	auxptr->channel = wispy_channel_new();
	wispy_widget_link_channel(auxptr->channel, auxptr->chanopts);
	gtk_box_pack_end(GTK_BOX(auxptr->main_vbox), auxptr->channel, FALSE, FALSE, 0);

	auxptr->planar = wispy_planar_new();
	wispy_widget_link_channel(auxptr->planar, auxptr->chanopts);
	gtk_box_pack_end(GTK_BOX(auxptr->main_vbox), auxptr->planar, TRUE, TRUE, 0);

	auxptr->topo = wispy_topo_new();
	wispy_widget_link_channel(auxptr->topo, auxptr->chanopts);
	gtk_box_pack_end(GTK_BOX(auxptr->main_vbox), auxptr->topo, TRUE, TRUE, 0);

	auxptr->spectral = wispy_spectral_new();
	wispy_widget_link_channel(auxptr->spectral, auxptr->chanopts);
	gtk_box_pack_end(GTK_BOX(auxptr->main_vbox), auxptr->spectral, TRUE, TRUE, 0);

	wispy_channel_append_update(auxptr->channel, auxptr->planar);
	wispy_channel_append_update(auxptr->channel, auxptr->topo);
	wispy_channel_append_update(auxptr->channel, auxptr->spectral);
}

void create_window(wg_aux *auxptr) {
	GtkWidget *temp, *hbox, *arrow, *closebutton, *closeicon;

	/* Make the device picker buttons and label */
	auxptr->nodev_vbox = gtk_vbox_new(FALSE, 0);
	temp = gtk_label_new("No device selected...");
	gtk_box_pack_start(GTK_BOX(auxptr->nodev_vbox), temp, FALSE, FALSE, 4);
	gtk_widget_show(temp);

	/* Build the arrow for using an open device */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(auxptr->nodev_vbox), hbox, FALSE, FALSE, 2);

	temp = gtk_button_new_with_label("Open Device");
	g_signal_connect_swapped(G_OBJECT(temp), "clicked",
			G_CALLBACK(main_menu_spawnpicker), 
			auxptr);
	gtk_box_pack_start(GTK_BOX(hbox), temp, TRUE, TRUE, 0);
	gtk_widget_show(temp);

	temp = gtk_button_new();
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(temp), arrow);
	g_signal_connect_swapped(G_OBJECT(temp), "event", 
			G_CALLBACK(main_nodev_menu_button_press),
			auxptr);
	gtk_box_pack_start(GTK_BOX(hbox), temp, FALSE, FALSE, 0);
	gtk_widget_show(temp);
	gtk_widget_show(hbox);
	gtk_widget_show(arrow);

	temp = gtk_button_new_with_label("Open Network Device");
	g_signal_connect_swapped(G_OBJECT(temp), "clicked",
			G_CALLBACK(main_menu_spawnnetpicker), 
			auxptr);
	gtk_box_pack_start(GTK_BOX(auxptr->nodev_vbox), temp, FALSE, FALSE, 2);
	gtk_widget_show(temp);

	gtk_box_pack_start(GTK_BOX(auxptr->main_vbox), auxptr->nodev_vbox, FALSE, FALSE, 0);

	gtk_widget_show(auxptr->nodev_vbox);

	create_spectools(auxptr);
}

void reinit_window(gpointer *aux) {
	wg_aux *auxptr = (wg_aux *) aux;

	g_return_if_fail(aux != NULL);

	gtk_widget_destroy(auxptr->planar);
	gtk_widget_destroy(auxptr->spectral);
	gtk_widget_destroy(auxptr->topo);
	gtk_widget_destroy(auxptr->channel);

	/* WDR handles closing off the phydev when nothing else
	 * is using it, and we've destroyed everything using it */

	/* Show the nodev box again */
	gtk_widget_show(auxptr->nodev_vbox);

	gtk_widget_set_sensitive(auxptr->mi_close, 0);

	/* Recreate the spectools components */
	create_spectools(auxptr);
}

void enable_host_cb(gpointer *aux) {
	GtkWidget *dialog, *label;

	label = gtk_label_new("This will attempt to place the USB chipset of your "
						  "Nokia into HOST mode.");
	dialog = gtk_dialog_new_with_buttons ("USB", NULL,
										  GTK_DIALOG_MODAL, 
										  GTK_STOCK_OK,
										  GTK_RESPONSE_ACCEPT,
										  GTK_STOCK_CANCEL,
										  GTK_RESPONSE_CANCEL,
										  NULL);

	gtk_window_set_default_size(GTK_WINDOW (dialog), 350, 100);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);

	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		system("/usr/libexec/usbcontrol host");
	}

	gtk_widget_destroy(dialog);
}

void enable_periph_cb(gpointer *aux) {
	GtkWidget *dialog, *label;

	label = gtk_label_new("This will attempt to place the USB chipset of your "
						  "Nokia into PERIPHERAL (standard) mode.");
	dialog = gtk_dialog_new_with_buttons ("USB", NULL,
										  GTK_DIALOG_MODAL, 
										  GTK_STOCK_OK,
										  GTK_RESPONSE_ACCEPT,
										  GTK_STOCK_CANCEL,
										  GTK_RESPONSE_CANCEL,
										  NULL);

	gtk_window_set_default_size(GTK_WINDOW (dialog), 350, 100);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);

	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		system("/usr/libexec/usbcontrol periph");
	}

	gtk_widget_destroy(dialog);
}

/* Callback for hardware keys */
gboolean key_press_cb(GtkWidget *widget, GdkEventKey *event,
					  HildonWindow *window)
{
	switch (event->keyval) {
		case GDK_F6:
			if (fullscreen) {
				gtk_window_unfullscreen(GTK_WINDOW(window));
				fullscreen = 0;
			} else {
				gtk_window_fullscreen(GTK_WINDOW(window));
				fullscreen = 1;
			}

			return TRUE;
	}

	return FALSE;
}

int main(int argc, char *argv[]) {
	HildonProgram *program;
	HildonWindow *window;
	GtkWidget *vbox, *graph_vbox;
	wg_aux *auxptr;

	GtkWidget *main_menu, *mn_usb, *mn_view, *mi_usb, *mi_view;
	GtkWidget *mi_usb_host, *mi_usb_periph, 
			  *mi_network, *mi_planar, *mi_spectral, *mi_topo, 
			  *mi_close, *mi_sep, *mi_quit;

	wispy_device_registry wdr;

	int x;

	char errstr[WISPY_ERROR_MAX];

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	fullscreen = 0;

	gtk_init(&argc, &argv);

	auxptr = (wg_aux *) malloc(sizeof(wg_aux));
	printf("debug - auxptr created as %p\n", auxptr);

	program = HILDON_PROGRAM(hildon_program_get_instance());
	g_set_application_name("Spectool");

	main_menu = gtk_menu_new();
	mn_usb = gtk_menu_new();
	mn_view = gtk_menu_new();

	mi_usb = gtk_menu_item_new_with_label("USB");
	mi_usb_host = gtk_menu_item_new_with_label("Host mode");
	mi_usb_periph = gtk_menu_item_new_with_label("Client mode");

	mi_view = gtk_menu_item_new_with_label("View");
	mi_spectral = gtk_check_menu_item_new_with_label("Spectral");
	mi_topo = gtk_check_menu_item_new_with_label("Topo");
	mi_planar = gtk_check_menu_item_new_with_label("Planar");

	mi_close = gtk_menu_item_new_with_label("Close");
	mi_sep = gtk_separator_menu_item_new();
	mi_quit = gtk_menu_item_new_with_label("Quit");

	gtk_menu_append(main_menu, mi_usb);
	gtk_menu_append(main_menu, mi_view);
	gtk_menu_append(main_menu, mi_close);
	gtk_menu_append(main_menu, mi_sep);
	gtk_menu_append(main_menu, mi_quit);

	gtk_menu_append(mn_usb, mi_usb_host);
	gtk_menu_append(mn_usb, mi_usb_periph);

	gtk_menu_append(mn_view, mi_spectral);
	gtk_menu_append(mn_view, mi_topo);
	gtk_menu_append(mn_view, mi_planar);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_spectral), FALSE);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_topo), TRUE);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi_planar), TRUE);

	g_signal_connect_swapped(G_OBJECT(mi_spectral), "activate",
			G_CALLBACK(kick_display_items), auxptr);
	g_signal_connect_swapped(G_OBJECT(mi_topo), "activate",
			G_CALLBACK(kick_display_items), auxptr);
	g_signal_connect_swapped(G_OBJECT(mi_planar), "activate",
			G_CALLBACK(kick_display_items), auxptr);

	gtk_widget_set_sensitive(mi_close, 0);
	g_signal_connect_swapped(G_OBJECT(mi_close), "activate",
							 G_CALLBACK(reinit_window), auxptr);

	g_signal_connect_swapped(G_OBJECT(mi_usb_host), "activate",
							 G_CALLBACK(enable_host_cb), NULL);
	g_signal_connect_swapped(G_OBJECT(mi_usb_periph), "activate",
							 G_CALLBACK(enable_periph_cb), NULL);

	g_signal_connect(G_OBJECT(mi_quit), "activate",
			GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

	gtk_widget_show(main_menu);
	gtk_widget_show(mn_view);
	gtk_widget_show(mn_usb);
	gtk_widget_show(mi_usb);
	gtk_widget_show(mi_view);
	gtk_widget_show(mi_usb_host);
	gtk_widget_show(mi_usb_periph);
	gtk_widget_show(mi_spectral);
	gtk_widget_show(mi_planar);
	gtk_widget_show(mi_topo);
	gtk_widget_show(mi_close);
	gtk_widget_show(mi_sep);
	gtk_widget_show(mi_quit);

	wdr_init(&wdr);

	/* Turn on broadcast autodetection */
	if (wdr_enable_bcast(&wdr, errstr) < 0) {
		Wispy_Alert_Dialog(errstr);
	}

	window = HILDON_WINDOW(hildon_window_new());
	hildon_program_add_window(program, window);

	hildon_window_set_menu(HILDON_WINDOW(window), GTK_MENU(main_menu));
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_usb), mn_usb);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_view), mn_view);

	g_signal_connect(G_OBJECT (window), "delete_event",
					 G_CALLBACK (gtk_main_quit), NULL);

    g_signal_connect(G_OBJECT(window), 
        "key_press_event", G_CALLBACK(key_press_cb), window);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox); 

	gtk_widget_show(vbox);

	auxptr->wdr = &wdr;
	auxptr->main_vbox = vbox;

	auxptr->mi_spectral = mi_spectral;
	auxptr->mi_planar = mi_planar;
	auxptr->mi_topo = mi_topo;
	auxptr->mi_close = mi_close;

	gtk_widget_show_all(GTK_WIDGET(window));

	create_window(auxptr);

	gtk_main();

	return 0;
}	

