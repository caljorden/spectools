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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectool_gtk_planar.h"

char planar_help_txt[] = 
"<b>Planar View</b>\n\n"
"Traditional view of the spectrum showing average, peak, and current values.\n\n"
"Markers may be placed on the graph to show detailed information about specific "
"frequencies.\n\n"
"Channels may be highlighted by selecting the channel number from the listen "
"channels at the bottom of the graph.";

static void wispy_planar_class_init(WispyPlanarClass *class);
static void wispy_planar_init(WispyPlanar *graph);
static void wispy_planar_destroy(GtkObject *object);

static gint wispy_planar_configure(GtkWidget *widget, 
									 GdkEventConfigure *event);
static gboolean wispy_planar_expose(GtkWidget *widget, 
									  GdkEventExpose *event,
									  gpointer *aux);
static gboolean wispy_planar_button_press(GtkWidget *widget,
											GdkEventButton *event,
											gpointer *aux);
static gboolean wispy_planar_mouse_move(GtkWidget *widget,
										GdkEventMotion *event,
										gpointer *aux);

G_DEFINE_TYPE(WispyPlanar, wispy_planar, WISPY_TYPE_WIDGET);

void wispy_planar_draw(GtkWidget *widget, cairo_t *cr, WispyWidget *wwidget) {
	WispyPlanar *planar;
	cairo_text_extents_t extents;
	int x, chpix, maxcw;
	const double dash_onoff[] = {2, 4};
	const double dash_ononoff[] = {4, 2};

	char mtext[128];

	int chanmod;

	GList *mkr_iter;
	wispy_planar_marker *mkr;

	g_return_if_fail(widget != NULL);

	planar = WISPY_PLANAR(wwidget);

	cairo_save(cr);

	/* Render the peak points */
	if (planar->draw_peak) {
		cairo_save(cr);
		cairo_new_path(cr);
		cairo_move_to(cr, wwidget->g_start_x + 0.5, wwidget->g_end_y - 0.5);
		for (x = 0; x < wwidget->sweepcache->avg->num_samples; x++) {
			int px, py;
			int sdb = WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm,
										 wwidget->sweepcache->peak->sample_data[x]);

			chpix = x * wwidget->wbar;

			px = wwidget->g_start_x + chpix;
			py = (float) wwidget->g_len_y * 
				(float) ((float) (abs(sdb) + wwidget->base_db_offset) /
						 (float) (abs(wwidget->min_db_draw) + 
								  wwidget->base_db_offset));

			if (px < wwidget->g_start_x)
				px = wwidget->g_start_x;
			if (px > wwidget->g_end_x)
				px = wwidget->g_end_x;

			if (py < wwidget->g_start_y)
				py = wwidget->g_start_y;
			if (py > wwidget->g_end_y)
				py = wwidget->g_end_y;

			cairo_line_to(cr, px + 0.5, py + 0.5);
		}
		/* Close the path along the bottom */
		cairo_line_to(cr, wwidget->g_end_x - 0.5, wwidget->g_end_y - 0.5);
		cairo_line_to(cr, wwidget->g_start_x + 0.5, wwidget->g_end_y - 0.5);
		cairo_close_path(cr);
		/* Plot it - save the path so we can stroke and fill */
		cairo_set_source_rgb(cr, 0, 0, HC2CC(0xAA));
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		cairo_restore(cr);
	}

	/* Render the average points */
	if (planar->draw_avg) {
		cairo_save(cr);
		cairo_new_path(cr);
		cairo_move_to(cr, wwidget->g_start_x + 0.5, wwidget->g_end_y - 0.5);
		for (x = 0; x < wwidget->sweepcache->avg->num_samples; x++) {
			int px, py;
			int sdb = WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm,
										 wwidget->sweepcache->avg->sample_data[x]);
			chpix = x * wwidget->wbar;

			px = wwidget->g_start_x + chpix;
			py = (float) wwidget->g_len_y * 
				(float) ((float) (abs(sdb) + wwidget->base_db_offset) /
						 (float) (abs(wwidget->min_db_draw) + 
								  wwidget->base_db_offset));
			if (px < wwidget->g_start_x)
				px = wwidget->g_start_x;
			if (px > wwidget->g_end_x)
				px = wwidget->g_end_x;

			if (py < wwidget->g_start_y)
				py = wwidget->g_start_y;
			if (py > wwidget->g_end_y)
				py = wwidget->g_end_y;

			cairo_line_to(cr, px + 0.5, py + 0.5);
		}
		/* Close the path along the bottom */
		cairo_line_to(cr, wwidget->g_end_x - 0.5, wwidget->g_end_y - 0.5);
		cairo_line_to(cr, wwidget->g_start_x + 0.5, wwidget->g_end_y - 0.5);
		cairo_close_path(cr);
		/* Plot it */
		cairo_set_source_rgb(cr, HC2CC(0x11), HC2CC(0xBB), HC2CC(0x11));
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		cairo_restore(cr);
	}

	/* Render the latest points */
	if (planar->draw_cur) {
		cairo_save(cr);
		cairo_new_path(cr);
		cairo_move_to(cr, wwidget->g_start_x + 0.5, wwidget->g_end_y - 0.5);
		for (x = 0; x < wwidget->sweepcache->avg->num_samples; x++) {
			int px, py;
			int sdb = WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm,
				wwidget->sweepcache->latest->sample_data[x]);
			chpix = x * wwidget->wbar;

			px = wwidget->g_start_x + chpix;
			py = (float) wwidget->g_len_y * 
				(float) ((float) (abs(sdb) + wwidget->base_db_offset) /
						 (float) (abs(wwidget->min_db_draw) + 
								  wwidget->base_db_offset));

			if (px < wwidget->g_start_x)
				px = wwidget->g_start_x;
			if (px > wwidget->g_end_x)
				px = wwidget->g_end_x;

			if (py < wwidget->g_start_y)
				py = wwidget->g_start_y;
			if (py > wwidget->g_end_y)
				py = wwidget->g_end_y;

			cairo_line_to(cr, px + 0.5, py + 0.5);
		}
		cairo_line_to(cr, wwidget->g_end_x - 0.5, wwidget->g_end_y - 0.5);
		cairo_close_path(cr);
		/* Plot it */
		cairo_set_line_width(cr, 2);
		cairo_set_source_rgb(cr, HC2CC(0xFF), HC2CC(0xFF), HC2CC(0x00));
		cairo_stroke(cr);
		cairo_restore(cr);
	}

	mkr_iter = planar->mkr_list;
	while (mkr_iter != NULL && planar->draw_markers) {
		int px, py;
		int sdb;
		mkr = (wispy_planar_marker *) mkr_iter->data;

		if (mkr->samp_num < 0 || 
			mkr->samp_num > wwidget->sweepcache->latest->num_samples) {
			mkr_iter = g_list_next(mkr_iter);
			continue;
		}
			
		chpix = mkr->samp_num * wwidget->wbar;

		sdb = WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm,
					wwidget->sweepcache->latest->sample_data[mkr->samp_num]);

		px = wwidget->g_start_x + chpix;
		py = (float) wwidget->g_len_y * 
			(float) ((float) (abs(sdb) + wwidget->base_db_offset) /
					 (float) (abs(wwidget->min_db_draw) + 
							  wwidget->base_db_offset));
		cairo_save(cr);

		cairo_arc(cr, px, py, 10, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, mkr->r, mkr->g, mkr->b, 0.5);
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, mkr->r, mkr->g, mkr->b);
		cairo_stroke(cr);
		cairo_arc(cr, px, py, 1, 0, 2 * M_PI);
		cairo_stroke(cr);

		cairo_restore(cr);

		mkr_iter = g_list_next(mkr_iter);
	}

	cairo_restore(cr);
}

static void wispy_planar_wdr_devbind(GtkWidget *widget, wispy_device_registry *wdr, int slot) {
	WispyPlanar *planar;
	WispyWidget *wwidget;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_PLANAR(widget));
	g_return_if_fail(IS_WISPY_WIDGET(widget));

	planar = WISPY_PLANAR(widget);
	wwidget = WISPY_WIDGET(widget);
}

static void wispy_planar_wdr_sweep(int slot, int mode,
								   wispy_sample_sweep *sweep, void *aux) {
	WispyPlanar *planar;
	WispyWidget *wwidget;
	wispy_phy *pd;
	int tout = 0;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_PLANAR(aux));
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	planar = WISPY_PLANAR(aux);
	wwidget = WISPY_WIDGET(aux);

	tout = wwidget->draw_timeout;

	/* Update the timer */
	if (sweep != NULL) {
		pd = (wispy_phy *) sweep->phydev;
#ifdef HAVE_HILDON
		tout = 300 * pd->draw_agg_suggestion;
#else
		tout = 100 * pd->draw_agg_suggestion;
#endif
	}

	if (tout != wwidget->draw_timeout) {
		wwidget->draw_timeout = tout;
		g_source_remove(wwidget->timeout_ref);
		wwidget->timeout_ref = 
			g_timeout_add(wwidget->draw_timeout, 
						  (GSourceFunc) wispy_widget_timeout, wwidget);
	}
}

static gint wispy_planar_button_press(GtkWidget *widget, 
									  GdkEventButton *event,
									  gpointer *aux) {
	WispyPlanar *planar;
	WispyWidget *wwidget;
	int ch;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_PLANAR(aux));
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	planar = WISPY_PLANAR(aux);
	wwidget = WISPY_WIDGET(aux);

	g_return_if_fail(wwidget->sweepcache != NULL);
	g_return_if_fail(wwidget->sweepcache->avg != NULL);

	if (event->button != 1)
		return TRUE;

	wispy_widget_graphics_update(wwidget);
	wispy_widget_update(GTK_WIDGET(wwidget));

	return TRUE;
}

static gboolean wispy_planar_mouse_move(GtkWidget *widget,
										GdkEventMotion *event,
										gpointer *aux) {
	int x, y;
	int ch;
	GdkModifierType state;
	WispyPlanar *planar;
	WispyWidget *wwidget;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_PLANAR(aux));
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	planar = WISPY_PLANAR(aux);
	wwidget = WISPY_WIDGET(aux);

	g_return_if_fail(wwidget->sweepcache != NULL);
	g_return_if_fail(wwidget->sweepcache->avg != NULL);

	if (event->is_hint) {
		gdk_window_get_pointer(event->window, &x, &y, &state);
	} else {
		x = (int) event->x;
		y = (int) event->y;
		state = event->state;
	}

	/* If the button is down and we have a current marker, move it around */
	if (planar->mouse_down && (state & GDK_BUTTON1_MASK) &&
		planar->cur_mkr != NULL) {
		if (x > wwidget->g_start_x && x < wwidget->g_end_x &&
			y > wwidget->g_start_y && y < wwidget->g_end_y) {
			/* We assume we know the current marker when the mouse is down, so we just
			 * adjust the marker khz as they drag it around.  Set the sample, we'll
			 * derive the khz later */
			planar->cur_mkr->samp_num = (x - wwidget->g_start_x) / wwidget->wbar;
		} else {
			planar->cur_mkr->samp_num = -1;
		}

		wispy_widget_graphics_update(wwidget);
		wispy_widget_update(GTK_WIDGET(wwidget));
	} else if ((state & GDK_BUTTON1_MASK)) {
		GtkTreeIter iter;
		GtkTreeSelection *selection;
		GtkTreeModel *model;

		selection = 
			gtk_tree_view_get_selection(GTK_TREE_VIEW(planar->mkr_treeview));

		if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
			gtk_tree_model_get(model, &iter, 5, &(planar->cur_mkr), -1);

			planar->mouse_down = 1;
		}
	}

	if ((state & GDK_BUTTON1_MASK) == 0 && planar->mouse_down) {
		planar->mouse_down = 0;
		planar->cur_mkr = NULL;
	}

	return TRUE;
}

void wispy_planar_update(GtkWidget *widget) {
	WispyWidget *wwidget;
	WispyPlanar *planar;
	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean valid;
	char freqt[6], curt[6], avgt[6], maxt[6];
	wispy_planar_marker *mkr;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(widget));
	g_return_if_fail(IS_WISPY_PLANAR(widget));

	wwidget = WISPY_WIDGET(widget);
	planar = WISPY_PLANAR(widget);

	g_return_if_fail(wwidget->draw != NULL);

	/* Bail out if we don't have any sweep data */
	if (wwidget->sweepcache == NULL)
		return;
	if (wwidget->sweepcache->pos < 0)
		return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(planar->mkr_treeview));

	valid = gtk_tree_model_get_iter_first(model, &iter);
	while (valid && planar->draw_markers) {
		gtk_tree_model_get(model, &iter, 5, &mkr, -1);

		if (mkr->samp_num < 0 || 
			mkr->samp_num > wwidget->sweepcache->latest->num_samples) {
			gtk_list_store_set(GTK_LIST_STORE(model), &iter,
							   0, mkr->pixbuf,
							   1, "----",
							   2, "--",
							   3, "--", 
							   4, "--",
							   5, mkr,
							   -1);
		} else {
			int freq = (mkr->samp_num * wwidget->sweepcache->latest->res_hz / 1000) +
				wwidget->sweepcache->latest->start_khz;

			/* khz to mhz */
			if (freq >= 1000)
				freq = freq / 1000;

			snprintf(freqt, 6, "%4d", freq);

			snprintf(avgt, 6, "%d", 
					 wwidget->sweepcache->avg->sample_data[mkr->samp_num]);
			snprintf(maxt, 6, "%d",
					 wwidget->sweepcache->peak->sample_data[mkr->samp_num]);
			snprintf(curt, 6, "%d",
					 wwidget->sweepcache->latest->sample_data[mkr->samp_num]);

			gtk_list_store_set(GTK_LIST_STORE(model), &iter,
							   0, mkr->pixbuf,
							   1, freqt,
							   2, curt,
							   3, avgt,
							   4, maxt,
							   5, mkr,
							   -1);
		}

		/* Make iter point to the next row in the list store */
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}

void wispy_planar_context_help(gpointer *aux) {
	Wispy_Help_Dialog("Planar View", planar_help_txt);
}

void wispy_planar_context_markers(gpointer *aux) {
	WispyWidget *wwidget;
	WispyPlanar *planar;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));
	g_return_if_fail(IS_WISPY_PLANAR(aux));

	wwidget = WISPY_WIDGET(aux);
	planar = WISPY_PLANAR(aux);

	if (planar->draw_markers) {
		planar->draw_markers = 0;
	} else {
		planar->draw_markers = 1;
	}

	gtk_widget_set_sensitive(planar->mkr_treeview, planar->draw_markers);
	wispy_widget_graphics_update(wwidget);
	wispy_widget_update(GTK_WIDGET(wwidget));
}

void wispy_planar_context_peak(gpointer *aux) {
	WispyWidget *wwidget;
	WispyPlanar *planar;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));
	g_return_if_fail(IS_WISPY_PLANAR(aux));

	wwidget = WISPY_WIDGET(aux);
	planar = WISPY_PLANAR(aux);

	if (planar->draw_peak) {
		planar->draw_peak = 0;
	} else {
		planar->draw_peak = 1;
	}

	wispy_widget_graphics_update(wwidget);
	wispy_widget_update(GTK_WIDGET(wwidget));
}

void wispy_planar_context_avg(gpointer *aux) {
	WispyWidget *wwidget;
	WispyPlanar *planar;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));
	g_return_if_fail(IS_WISPY_PLANAR(aux));

	wwidget = WISPY_WIDGET(aux);
	planar = WISPY_PLANAR(aux);

	if (planar->draw_avg) {
		planar->draw_avg = 0;
	} else {
		planar->draw_avg = 1;
	}

	wispy_widget_graphics_update(wwidget);
	wispy_widget_update(GTK_WIDGET(wwidget));
}

void wispy_planar_context_cur(gpointer *aux) {
	WispyWidget *wwidget;
	WispyPlanar *planar;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));
	g_return_if_fail(IS_WISPY_PLANAR(aux));

	wwidget = WISPY_WIDGET(aux);
	planar = WISPY_PLANAR(aux);

	if (planar->draw_cur) {
		planar->draw_cur = 0;
	} else {
		planar->draw_cur = 1;
	}

	wispy_widget_graphics_update(wwidget);
	wispy_widget_update(GTK_WIDGET(wwidget));
}

void wispy_planar_context_menu(GtkWidget *widget, GtkWidget *menu) {
	WispyWidget *wwidget;
	WispyPlanar *planar;
	GtkWidget *mi;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(widget));
	g_return_if_fail(IS_WISPY_PLANAR(widget));

	wwidget = WISPY_WIDGET(widget);
	planar = WISPY_PLANAR(widget);

	mi = gtk_check_menu_item_new_with_label("Show dBm scale");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), wwidget->show_dbm);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(wispy_widget_context_dbm),
							 widget);
	gtk_widget_show(mi);

	mi = gtk_check_menu_item_new_with_label("Show dBm lines");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), wwidget->show_dbm_lines);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(wispy_widget_context_dbmlines),
							 widget);
	gtk_widget_show(mi);

	mi = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_widget_show(mi);

	mi = gtk_check_menu_item_new_with_label("Show markers");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), planar->draw_markers);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(wispy_planar_context_markers),
							 widget);
	gtk_widget_show(mi);

	mi = gtk_check_menu_item_new_with_label("Show peak");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), planar->draw_peak);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(wispy_planar_context_peak),
							 widget);
	gtk_widget_show(mi);

	mi = gtk_check_menu_item_new_with_label("Show average");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), planar->draw_avg);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(wispy_planar_context_avg),
							 widget);
	gtk_widget_show(mi);

	mi = gtk_check_menu_item_new_with_label("Show current");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), planar->draw_cur);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(wispy_planar_context_cur),
							 widget);
	gtk_widget_show(mi);
}

static void wispy_planar_class_init(WispyPlanarClass *class) {
	GObjectClass *gobject_class;
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	gobject_class = G_OBJECT_CLASS(class);
	object_class = GTK_OBJECT_CLASS(class);
	widget_class = GTK_WIDGET_CLASS(class);

	object_class->destroy = wispy_planar_destroy;
}

static void wispy_planar_destroy(GtkObject *object) {
	WispyPlanar *planar = WISPY_PLANAR(object);
	WispyWidget *wwidget;

	wwidget = WISPY_WIDGET(planar);

	GTK_OBJECT_CLASS(wispy_planar_parent_class)->destroy(object);
}

GtkWidget *wispy_planar_new() {
	WispyPlanar *planar;
	WispyWidget *wwidget;

	planar = gtk_type_new(wispy_planar_get_type());

	wwidget = WISPY_WIDGET(planar);

	return GTK_WIDGET(planar);
}

wispy_planar_marker *wispy_planar_marker_new(GtkWidget *widget, 
											 double r, double g, double b,
											 int samp) {
	wispy_planar_marker *mkr = malloc(sizeof(wispy_planar_marker));
	cairo_t *cr;
	GdkPixmap *pixmap;
	GdkColormap *cmap;

	mkr->r = r;
	mkr->g = g;
	mkr->b = b;
	mkr->samp_num = samp;

	mkr->cur = mkr->avg = mkr->peak = 0;

	cmap = gdk_colormap_get_system();
	pixmap = gdk_pixmap_new(NULL, 9, 9, cmap->visual->depth);
	gdk_drawable_set_colormap(pixmap, gdk_colormap_get_system());
	cr = gdk_cairo_create(pixmap);
	cairo_rectangle(cr, 0, 0, 9, 9);
	cairo_clip(cr);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_rectangle(cr, 0, 0, 9, 9);
	cairo_fill_preserve(cr);
	cairo_stroke(cr);
	cairo_destroy(cr);

	/* mkr->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 9, 9); */
	mkr->pixbuf = gdk_pixbuf_get_from_drawable(NULL, pixmap, 
											   gdk_drawable_get_colormap(pixmap),
											   0, 0, 0, 0, 9, 9);

	return mkr;
}

static void wispy_planar_init(WispyPlanar *planar) {
	WispyWidget *wwidget;
	GtkWidget *scrollwindow;
	
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	GtkWidget *clabel;
	PangoAttrList	*attr_lst;
	PangoAttribute	*attr;

	GtkWidget *bhb;

	GdkColor c;
	GtkStyle *style;

	wispy_planar_marker *mkr;

	wwidget = WISPY_WIDGET(planar);

	wwidget->sweep_num_samples = WISPY_PLANAR_NUM_SAMPLES;
	wwidget->sweep_keep_avg = 1;
	wwidget->sweep_keep_peak = 1;

	wwidget->sweep_num_aggregate = 2;

	wwidget->hlines = 8;
	wwidget->base_db_offset = 0;

	wwidget->graph_title = "<b>Planar View</b>";
	wwidget->graph_title_bg = "#00CC00";
	wwidget->graph_control_bg = "#A0CCA0";

	wwidget->show_channels = 1;
	wwidget->show_dbm = 1;
	wwidget->show_dbm_lines = 1;

	wwidget->wdr_sweep_func = wispy_planar_wdr_sweep;
	wwidget->wdr_devbind_func = wispy_planar_wdr_devbind;
	wwidget->draw_mouse_move_func = wispy_planar_mouse_move;
	wwidget->draw_mouse_click_func = wispy_planar_button_press;

#ifdef HAVE_HILDON
	wwidget->draw_timeout = 1500;
#else
	wwidget->draw_timeout = 500;
#endif
	wwidget->draw_func = wispy_planar_draw;
	wwidget->update_func = wispy_planar_update;

	wwidget->menu_func = wispy_planar_context_menu;
	wwidget->help_func = wispy_planar_context_help;

	wwidget->timeout_ref = 
		g_timeout_add(wwidget->draw_timeout, 
					  (GSourceFunc) wispy_widget_timeout, wwidget);

	wispy_widget_buildgui(wwidget);

	planar->mkr_list = NULL;
	planar->cur_mkr = NULL;

	scrollwindow = gtk_scrolled_window_new(NULL, NULL);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwindow),
								   GTK_POLICY_NEVER,
								   GTK_POLICY_AUTOMATIC);

	planar->mkr_treeview = gtk_tree_view_new();

	/* Force the treeview bg colors */
	gdk_color_parse(wwidget->graph_control_bg, &c);
	style = gtk_style_new();
	gtk_widget_set_style(GTK_WIDGET(planar->mkr_treeview), style);
	style->bg[GTK_STATE_NORMAL] = c;
	style->base[GTK_STATE_NORMAL] = c;
	gtk_style_unref(style);

	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrollwindow),
										  planar->mkr_treeview);

	/* Relly now, this is the ONLY way to do all this? */
	attr_lst = pango_attr_list_new();
	attr = pango_attr_size_new(7.0 * PANGO_SCALE);
	attr->start_index = 0;
	attr->end_index = 100;
	pango_attr_list_insert(attr_lst, attr);

	cell = gtk_cell_renderer_pixbuf_new();

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, cell, TRUE);
	gtk_tree_view_column_add_attribute(column, cell, "pixbuf", 0);
	gtk_tree_view_append_column(GTK_TREE_VIEW(planar->mkr_treeview),
								GTK_TREE_VIEW_COLUMN(column));

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "size-points", 6.0, 
				 "xalign", 0.5f, 
				 NULL);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, cell, TRUE);
	gtk_tree_view_column_add_attribute(column, cell, "text", 1);
	clabel = gtk_label_new("Freq");
	gtk_label_set_use_markup(GTK_LABEL(clabel), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(clabel), FALSE);
	gtk_label_set_attributes(GTK_LABEL(clabel), attr_lst);
	gtk_tree_view_column_set_widget(column, clabel);
	gtk_tree_view_column_set_expand(column, 1);
	gtk_tree_view_column_set_alignment(column, 0.5);
	gtk_widget_show(clabel);
	gtk_tree_view_append_column(GTK_TREE_VIEW(planar->mkr_treeview),
								GTK_TREE_VIEW_COLUMN(column));
	
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, cell, TRUE);
	gtk_tree_view_column_add_attribute(column, cell, "text", 2);
	clabel = gtk_label_new("Cur");
	gtk_label_set_use_markup(GTK_LABEL(clabel), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(clabel), FALSE);
	gtk_label_set_attributes(GTK_LABEL(clabel), attr_lst);
	gtk_tree_view_column_set_widget(column, clabel);
	gtk_tree_view_column_set_expand(column, 1);
	gtk_tree_view_column_set_alignment(column, 0.5);
	gtk_widget_show(clabel);
	gtk_tree_view_append_column(GTK_TREE_VIEW(planar->mkr_treeview),
								GTK_TREE_VIEW_COLUMN(column));

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, cell, TRUE);
	gtk_tree_view_column_add_attribute(column, cell, "text", 3);
	clabel = gtk_label_new("Avg");
	gtk_label_set_use_markup(GTK_LABEL(clabel), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(clabel), FALSE);
	gtk_label_set_attributes(GTK_LABEL(clabel), attr_lst);
	gtk_tree_view_column_set_widget(column, clabel);
	gtk_tree_view_column_set_expand(column, 1);
	gtk_tree_view_column_set_alignment(column, 0.5);
	gtk_widget_show(clabel);
	gtk_tree_view_append_column(GTK_TREE_VIEW(planar->mkr_treeview),
								GTK_TREE_VIEW_COLUMN(column));

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, cell, TRUE);
	gtk_tree_view_column_add_attribute(column, cell, "text", 4);
	clabel = gtk_label_new("Max");
	gtk_label_set_use_markup(GTK_LABEL(clabel), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(clabel), FALSE);
	gtk_label_set_attributes(GTK_LABEL(clabel), attr_lst);
	gtk_tree_view_column_set_widget(column, clabel);
	gtk_tree_view_column_set_expand(column, 1);
	gtk_tree_view_column_set_alignment(column, 0.5);
	gtk_widget_show(clabel);
	gtk_tree_view_append_column(GTK_TREE_VIEW(planar->mkr_treeview),
								GTK_TREE_VIEW_COLUMN(column));

	gtk_box_pack_start(GTK_BOX(wwidget->vbox), scrollwindow, TRUE, TRUE, 2);

	bhb = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wwidget->vbox), bhb, FALSE, FALSE, 0);
	planar->mkr_newbutton = gtk_button_new_with_label("Add");
	gtk_box_pack_start(GTK_BOX(bhb), planar->mkr_newbutton, TRUE, TRUE, 0);
	planar->mkr_delbutton = gtk_button_new_with_label("Del");
	gtk_box_pack_start(GTK_BOX(bhb), planar->mkr_delbutton, TRUE, TRUE, 0);
	gtk_widget_show(planar->mkr_newbutton);
	gtk_widget_show(planar->mkr_delbutton);
	gtk_widget_set_sensitive(planar->mkr_newbutton, 0);
	gtk_widget_set_sensitive(planar->mkr_delbutton, 0);
	gtk_widget_show(bhb);

	planar->mkr_treelist = gtk_list_store_new(6, GDK_TYPE_PIXBUF, G_TYPE_STRING, 
											  G_TYPE_STRING, G_TYPE_STRING, 
											  G_TYPE_STRING, G_TYPE_POINTER);
	gtk_tree_view_set_model(GTK_TREE_VIEW(planar->mkr_treeview),
							GTK_TREE_MODEL(planar->mkr_treelist));

	/* Auto delete when we don't ref it anymore */
	g_object_unref(planar->mkr_treelist);
	pango_attr_list_unref(attr_lst);

	mkr = wispy_planar_marker_new(GTK_WIDGET(planar), 1, 0, 0, -1);
	planar->mkr_list = g_list_append(planar->mkr_list, mkr);
	gtk_list_store_append(GTK_LIST_STORE(planar->mkr_treelist), &iter);
	gtk_list_store_set(GTK_LIST_STORE(planar->mkr_treelist), &iter,
					   0, mkr->pixbuf,
					   1, "----",
					   2, "--",
					   3, "--",
					   4, "--",
					   5, mkr,
					   -1);

	mkr = wispy_planar_marker_new(GTK_WIDGET(planar), 0, 1, 0, -1);
	planar->mkr_list = g_list_append(planar->mkr_list, mkr);
	gtk_list_store_append(GTK_LIST_STORE(planar->mkr_treelist), &iter);
	gtk_list_store_set(GTK_LIST_STORE(planar->mkr_treelist), &iter,
					   0, mkr->pixbuf,
					   1, "----",
					   2, "--",
					   3, "--",
					   4, "--",
					   5, mkr,
					   -1);

	mkr = wispy_planar_marker_new(GTK_WIDGET(planar), 0, 0, 1, -1);
	planar->mkr_list = g_list_append(planar->mkr_list, mkr);
	gtk_list_store_append(GTK_LIST_STORE(planar->mkr_treelist), &iter);
	gtk_list_store_set(GTK_LIST_STORE(planar->mkr_treelist), &iter,
					   0, mkr->pixbuf,
					   1, "----",
					   2, "--",
					   3, "--",
					   4, "--",
					   5, mkr,
					   -1);

	mkr = wispy_planar_marker_new(GTK_WIDGET(planar), 1, 1, 0, -1);
	planar->mkr_list = g_list_append(planar->mkr_list, mkr);
	gtk_list_store_append(GTK_LIST_STORE(planar->mkr_treelist), &iter);
	gtk_list_store_set(GTK_LIST_STORE(planar->mkr_treelist), &iter,
					   0, mkr->pixbuf,
					   1, "----",
					   2, "--",
					   3, "--",
					   4, "--",
					   5, mkr,
					   -1);

	gtk_widget_show(planar->mkr_treeview);
	gtk_widget_show(scrollwindow);

	/* Draw it all by default */
	planar->draw_markers = 1;
	planar->draw_peak = 1;
	planar->draw_avg = 1;
	planar->draw_cur = 1;
}

