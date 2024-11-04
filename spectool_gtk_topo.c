/*
 * * This code is free software; you can redistribute it and/or modify
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

#include "spectool_gtk_topo.h"
#include "spectool_gtk.h"

#define KLUGE_NOISE_FLOOR -90

char topo_help_txt[] = 
"<b>Topographic View</b>\n\n"
"2d historical view of peak values of sweep data.  Each point "
"represents the peak values of a sample, colored by percentage of "
"peak times.  The topographic view is well suited for viewing trends "
"of spectrum usage over time and finding overlapping devices which "
"may cause interference.\n";

float topo_21_colormap[] = {
	HC2CC(0), HC2CC(0), HC2CC(0),
	HC2CC(0), HC2CC(0), HC2CC(46),
	HC2CC(0), HC2CC(0), HC2CC(51),
	HC2CC(0), HC2CC(5), HC2CC(55),
	HC2CC(0), HC2CC(10), HC2CC(60),
	HC2CC(0), HC2CC(16), HC2CC(64),
	HC2CC(0), HC2CC(23), HC2CC(68),
	HC2CC(0), HC2CC(30), HC2CC(72),
	HC2CC(0), HC2CC(38), HC2CC(77),
	HC2CC(0), HC2CC(47), HC2CC(81),
	HC2CC(0), HC2CC(57), HC2CC(85),
	HC2CC(0), HC2CC(67), HC2CC(89),
	HC2CC(0), HC2CC(78), HC2CC(94),
	HC2CC(0), HC2CC(90), HC2CC(98),
	HC2CC(0), HC2CC(102), HC2CC(102),
	HC2CC(0), HC2CC(106), HC2CC(97),
	HC2CC(0), HC2CC(111), HC2CC(92),
	HC2CC(0), HC2CC(115), HC2CC(86),
	HC2CC(0), HC2CC(119), HC2CC(79),
	HC2CC(0), HC2CC(123), HC2CC(72),
	HC2CC(0), HC2CC(128), HC2CC(64),
	HC2CC(0), HC2CC(132), HC2CC(55),
	HC2CC(0), HC2CC(136), HC2CC(45),
	HC2CC(0), HC2CC(140), HC2CC(35),
	HC2CC(0), HC2CC(145), HC2CC(24),
	HC2CC(0), HC2CC(149), HC2CC(12),
	HC2CC(0), HC2CC(153), HC2CC(0),
	HC2CC(13), HC2CC(157), HC2CC(0),
	HC2CC(27), HC2CC(162), HC2CC(0),
	HC2CC(41), HC2CC(166), HC2CC(0),
	HC2CC(57), HC2CC(170), HC2CC(0),
	HC2CC(73), HC2CC(174), HC2CC(0),
	HC2CC(89), HC2CC(179), HC2CC(0),
	HC2CC(107), HC2CC(183), HC2CC(0),
	HC2CC(125), HC2CC(187), HC2CC(0),
	HC2CC(143), HC2CC(191), HC2CC(0),
	HC2CC(163), HC2CC(195), HC2CC(0),
	HC2CC(183), HC2CC(200), HC2CC(0),
	HC2CC(204), HC2CC(204), HC2CC(0),
	HC2CC(208), HC2CC(191), HC2CC(0),
	HC2CC(212), HC2CC(177), HC2CC(0),
	HC2CC(217), HC2CC(163), HC2CC(0),
	HC2CC(221), HC2CC(147), HC2CC(0),
	HC2CC(225), HC2CC(131), HC2CC(0),
	HC2CC(229), HC2CC(115), HC2CC(0),
	HC2CC(234), HC2CC(97), HC2CC(0),
	HC2CC(238), HC2CC(79), HC2CC(0),
	HC2CC(242), HC2CC(61), HC2CC(0),
	HC2CC(246), HC2CC(41), HC2CC(0),
	HC2CC(251), HC2CC(21), HC2CC(0),
	HC2CC(255), HC2CC(0), HC2CC(0)
};
int topo_21_colormap_len = 50;

static void spectool_topo_class_init(SpectoolTopoClass *class);
static void spectool_topo_init(SpectoolTopo *graph);
static void spectool_topo_destroy(GtkObject *object);

static gint spectool_topo_configure(GtkWidget *widget, 
									 GdkEventConfigure *event);

G_DEFINE_TYPE(SpectoolTopo, spectool_topo, SPECTOOL_TYPE_WIDGET);

void spectool_topo_draw(GtkWidget *widget, cairo_t *cr, SpectoolWidget *wwidget) {
	SpectoolTopo *topo;

	float sh; 
	double chpix;
	cairo_pattern_t *pattern;

	int samp, db;

	g_return_if_fail(IS_SPECTOOL_TOPO(wwidget));

	topo = SPECTOOL_TOPO(wwidget);

	if (topo->sch == 0 || topo->scw == 0) {
		printf("debug - sch or scw 0?  %d %d\n", topo->sch, topo->scw);
		return;
	}

	cairo_save(cr);

	// Find a representative line to get our maximum
	// 2/3rds down, up a bit to dodge noise floor
	// int avg_db = (((abs(wwidget->min_db_draw) - abs(wwidget->base_db_offset)) / 3) * 2) - 3 - abs(wwidget->base_db_offset);;
	// int avg_db = ((abs(wwidget->min_db_draw) / 3) * 2) - abs(wwidget->base_db_offset);
	int mindb = SPECTOOL_RSSI_CONVERT(wwidget->amp_offset_mdbm, 
									wwidget->amp_res_mdbm, 
									wwidget->phydev->min_rssi_seen);
	int avg_db = ((abs(mindb) / 3) * 2) - abs(wwidget->base_db_offset);
	int avg_peak = 1;

	// printf("row %d, db %d\n", avg_db, avg_db + abs(wwidget->base_db_offset));
	for (samp = 0; samp < topo->scw; samp++) {
		int z;
		if ((z = (topo->sample_counts[(avg_db * topo->scw) + samp])) > avg_peak) {
			// printf("new peak %d\n", z);
			avg_peak = z;
		}
	}
	
	// printf("peak %d\n", avg_peak);

	/* Figure out the height based on the dbm range */
	sh = (double) wwidget->g_len_y / (abs(wwidget->min_db_draw) - abs(wwidget->base_db_offset));

	/* Plot along all the normalized DB ranges  */
	for (db = 0; db < (abs(wwidget->min_db_draw) - abs(wwidget->base_db_offset)); db++) {
		/* Make a pattern to draw our levels to, scaled to the graph and db
		 * position so we don't matrix it */
		pattern =
			cairo_pattern_create_linear(wwidget->g_start_x,
										wwidget->g_start_y + (sh * db),
										wwidget->g_end_x,
										wwidget->g_start_y + (sh * db) + 1);

		/* Plot each sample count across that db */
		for (samp = 0; samp < topo->scw; samp++) {
			int cpos;

			/*
				printf("[%d,%d] %f ", samp, db,
				((float) (topo->sample_counts[(db * topo->scw) + samp])));
				*/

			cpos = (float) (topo->colormap_len - 1) *
				((float) (topo->sample_counts[(db * topo->scw) + samp]) /
				 (float) avg_peak);

			if (cpos < 0) cpos = 0;
			if (cpos > topo->colormap_len) cpos = topo->colormap_len - 1;

			cairo_pattern_add_color_stop_rgb(pattern,
					(float) (samp) / (topo->scw),
					SPECTOOL_TOPO_COLOR(topo->colormap, cpos, 0),
					SPECTOOL_TOPO_COLOR(topo->colormap, cpos, 1),
					SPECTOOL_TOPO_COLOR(topo->colormap, cpos, 2));
		}

		cairo_set_source(cr, pattern);
		cairo_rectangle(cr, 
						wwidget->g_start_x + 0.5, 
						wwidget->g_start_y + (sh * db) + 0.5,
						(float) wwidget->g_len_x, (float) sh + 1);

		/*
		printf("debug - rectangle %f, %f .. %f, %f\n",
						wwidget->g_start_x + 0.5, 
						wwidget->g_start_y + (sh * db) + 0.5,
						(float) wwidget->g_len_x, (float) sh + 1);
						*/

		cairo_fill(cr);

		cairo_pattern_destroy(pattern);
	}

	cairo_restore(cr);
}

void spectool_topo_update(GtkWidget *widget) {
	SpectoolWidget *wwidget;
	SpectoolTopo *topo;
	char perct[6];
	
	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_SPECTOOL_WIDGET(widget));
	g_return_if_fail(IS_SPECTOOL_TOPO(widget));

	wwidget = SPECTOOL_WIDGET(widget);
	topo = SPECTOOL_TOPO(widget);

	if (topo->sweep_count_num > 0 && topo->sweep_peak_max > 0) {
		snprintf(perct, 6, "%2.1f%%", 
				 ((float) topo->sweep_peak_max / 
				  topo->sweep_count_num) * 100);
		gtk_label_set_text(GTK_LABEL(topo->leg_max), perct);
	}

}

static void spectool_topo_class_init(SpectoolTopoClass *class) {
	GObjectClass *gobject_class;
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	gobject_class = G_OBJECT_CLASS(class);
	object_class = GTK_OBJECT_CLASS(class);
	widget_class = GTK_WIDGET_CLASS(class);

	object_class->destroy = spectool_topo_destroy;
}

static void spectool_topo_destroy(GtkObject *object) {
	SpectoolTopo *topo = SPECTOOL_TOPO(object);
	SpectoolWidget *wwidget;

	wwidget = SPECTOOL_WIDGET(topo);

	if (topo->sample_counts != NULL) {
		free(topo->sample_counts);
		topo->sample_counts = NULL;
	}

	GTK_OBJECT_CLASS(spectool_topo_parent_class)->destroy(object);
}

static void spectool_topo_wdr_devbind(GtkWidget *widget, spectool_device_registry *wdr, 
								   int slot) {
	/* Nothing, magic moved to sweep-configured */
}

static void spectool_topo_wdr_sweep(int slot, int mode, 
									 spectool_sample_sweep *sweep, 
									 void *aux) {
	SpectoolTopo *topo;
	SpectoolWidget *wwidget;
	int s, x, sc, tout;
	spectool_phy *pd;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_SPECTOOL_TOPO(aux));
	g_return_if_fail(IS_SPECTOOL_WIDGET(aux));

	topo = SPECTOOL_TOPO(aux);
	wwidget = SPECTOOL_WIDGET(aux);

	tout = wwidget->draw_timeout;

	/* Update the timer */
	if (sweep != NULL && sweep->phydev != NULL) {
		pd = (spectool_phy *) sweep->phydev;
#ifdef HAVE_HILDON
		tout = 500 * pd->draw_agg_suggestion;
#else
		tout = 200 * pd->draw_agg_suggestion;
#endif
	}

	if (tout != wwidget->draw_timeout) {
		wwidget->draw_timeout = tout;
		g_source_remove(wwidget->timeout_ref);
		wwidget->timeout_ref = 
			g_timeout_add(wwidget->draw_timeout, 
						  (GSourceFunc) spectool_widget_timeout, wwidget);
	}

	if ((mode & SPECTOOL_POLL_ERROR)) {
		if (topo->sample_counts) {
			free(topo->sample_counts);
			topo->sample_counts = NULL;
		}
	} else if ((mode & SPECTOOL_POLL_CONFIGURED)) {
		if (topo->sample_counts != NULL) {
			free(topo->sample_counts);
		}

		// 2d plot; #samples wide, normalized dbrange high
		topo->sch = abs(wwidget->min_db_draw) - abs(wwidget->base_db_offset);

		topo->scw =
			spectool_phy_getcurprofile(wwidget->phydev)->num_samples;

		topo->sample_counts = (unsigned int *)
			malloc(sizeof(unsigned int) * topo->sch * topo->scw);

		memset(topo->sample_counts, 0,
			   sizeof(unsigned int) * topo->sch * topo->scw);

		topo->sweep_count_num = 0;
		/* always 1 for math */
		topo->sweep_peak_max = 1;

	} else if ((mode & SPECTOOL_POLL_SWEEPCOMPLETE)) {
		topo->sweep_count_num = 0;
		topo->sweep_peak_max = 1;

		memset(topo->sample_counts, 0,
			   sizeof(unsigned int) * topo->sch * topo->scw);

		int max = wwidget->sweepcache->pos;
		if (wwidget->sweepcache->looped)
			max = wwidget->sweepcache->num_alloc;

		for (s = 0; s < max; s++) {
			/* Copy the aggregate sweep (ie our peak data) over... */
			for (x = 0; x < topo->scw && x < 
				 wwidget->sweepcache->sweeplist[s]->num_samples; x++) {
				int sdb = SPECTOOL_RSSI_CONVERT(wwidget->amp_offset_mdbm, 
											 wwidget->amp_res_mdbm, 
								wwidget->sweepcache->sweeplist[s]->sample_data[x]);

#if 0
				if (sdb < KLUGE_NOISE_FLOOR)
					continue;
#endif

				// Normalize it into our base offset as an int
				int ndb = abs(sdb) - abs(wwidget->base_db_offset); 

				if (ndb < 0) 
					ndb = 0;
				if (ndb >= topo->sch) 
					ndb = topo->sch - 1;

				/* Increment that position */
				sc = ++(topo->sample_counts[(ndb * topo->scw) + x]);
				// sc = ++(topo->sample_counts[(x * topo->sch) + ndb]);

				/* Record the max peak count for easy math later */
				if (sc > topo->sweep_peak_max) {
					// printf("debug - sweep peak %d\n", sc);
					topo->sweep_peak_max = sc;
				}

			}

			topo->sweep_count_num++;
		}
	}
}

void spectool_topo_context_help(gpointer *aux) {
	Spectool_Help_Dialog("Topographic View", topo_help_txt);
}

void spectool_topo_context_menu(GtkWidget *widget, GtkWidget *menu) {
	SpectoolWidget *wwidget;
	SpectoolTopo *topo;
	GtkWidget *mi;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_SPECTOOL_WIDGET(widget));
	g_return_if_fail(IS_SPECTOOL_TOPO(widget));

	wwidget = SPECTOOL_WIDGET(widget);
	topo = SPECTOOL_TOPO(widget);

	mi = gtk_check_menu_item_new_with_label("Show dBm scale");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), wwidget->show_dbm);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(spectool_widget_context_dbm),
							 widget);
	gtk_widget_show(mi);

	mi = gtk_check_menu_item_new_with_label("Show dBm lines");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	gtk_widget_set_sensitive(mi, (wwidget->wdr_slot >= 0));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), wwidget->show_dbm_lines);
	g_signal_connect_swapped(G_OBJECT(mi), "activate",
							 G_CALLBACK(spectool_widget_context_dbmlines),
							 widget);
	gtk_widget_show(mi);
}

GtkWidget *spectool_topo_new(void) {
	SpectoolTopo *topo;
	SpectoolWidget *wwidget;

	topo = gtk_type_new(spectool_topo_get_type());

	wwidget = SPECTOOL_WIDGET(topo);

	return GTK_WIDGET(topo);
}

static gboolean spectool_topo_legend_expose(GtkWidget *widget,
											 GdkEventExpose *event,
											 gpointer *aux) {
	cairo_t *cr;
	int x, y, w, h, dw, dh;
	SpectoolTopo *topo;
	cairo_pattern_t *pattern;
	int cp;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(aux != NULL, FALSE);
	g_return_val_if_fail(IS_SPECTOOL_TOPO(aux), FALSE);

	topo = SPECTOOL_TOPO(aux);

	cr = gdk_cairo_create(widget->window);

	if (event != NULL) {
		x = event->area.x;
		y = event->area.y;
		w = event->area.width;
		h = event->area.height;
	} else {
		x = 0;
		y = 0;
		w = widget->allocation.width;
		h = widget->allocation.height;
	}
	dw = widget->allocation.width;
	dh = widget->allocation.height;

	cairo_rectangle(cr, x, y, w, h);

	cairo_clip(cr);

	pattern = cairo_pattern_create_linear(0, 0, dw, 0);

	for (cp = 0; cp < topo->colormap_len; cp++) {
		cairo_pattern_add_color_stop_rgb(pattern,
										 (float) (cp) / (topo->colormap_len),
								 SPECTOOL_TOPO_COLOR(topo->colormap, cp, 0),
								 SPECTOOL_TOPO_COLOR(topo->colormap, cp, 1),
								 SPECTOOL_TOPO_COLOR(topo->colormap, cp, 2));
	}

	cairo_set_source(cr, pattern);
	cairo_rectangle(cr, -1, -1, dw + 1, dh + 1);
	cairo_fill(cr);
	cairo_pattern_destroy(pattern);

	cairo_destroy(cr);

	return FALSE;
}

static void spectool_topo_init(SpectoolTopo *topo) {
	SpectoolWidget *wwidget;

	GtkWidget *temp;
	GtkWidget *legendv, *legendh;
	PangoAttrList *attr_list;
	PangoAttribute *attr;

	wwidget = SPECTOOL_WIDGET(topo);

	wwidget->sweep_num_samples = 60;

	wwidget->sweep_keep_avg = 0;
	wwidget->sweep_keep_peak = 0;

	/* Aggregate a big chunk of sweeps for each peak so we do less processing */
	wwidget->sweep_num_aggregate = 1;

	wwidget->hlines = 8;
	wwidget->base_db_offset = -50;

	wwidget->graph_title = strdup("<b>Topo View</b>");
	wwidget->graph_title_bg = strdup("#CC00CC");
	wwidget->graph_control_bg = strdup("#CCA0CC");

	wwidget->show_channels = 0;
	wwidget->show_dbm = 1;
	wwidget->show_dbm_lines = 1;

	wwidget->wdr_sweep_func = spectool_topo_wdr_sweep;
	wwidget->wdr_devbind_func = spectool_topo_wdr_devbind;
	wwidget->draw_mouse_move_func = NULL;
	wwidget->draw_mouse_click_func = NULL;

#ifndef HAVE_HILDON
	wwidget->draw_timeout = 2000;
#else
	wwidget->draw_timeout = 4000;
#endif
	wwidget->draw_func = spectool_topo_draw;

	wwidget->menu_func = spectool_topo_context_menu;

	wwidget->update_func = spectool_topo_update;

	topo->colormap = topo_21_colormap;
	topo->colormap_len = topo_21_colormap_len;

	wwidget->timeout_ref = 
		g_timeout_add(wwidget->draw_timeout, 
					  (GSourceFunc) spectool_widget_timeout, wwidget);

	spectool_widget_buildgui(wwidget);

	temp = gtk_frame_new("Legend");
	gtk_box_pack_start(GTK_BOX(wwidget->vbox), temp, FALSE, FALSE, 2);
	legendv = gtk_vbox_new(FALSE, 2);
	gtk_container_add(GTK_CONTAINER(temp), legendv);
	gtk_widget_show(temp);

	legendh = gtk_hbox_new(TRUE, 2);
	gtk_box_pack_start(GTK_BOX(legendv), legendh, TRUE, TRUE, 0);

	topo->legend_pix = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(legendv), topo->legend_pix, FALSE, FALSE, 2);
	gtk_drawing_area_size(GTK_DRAWING_AREA(topo->legend_pix), -1, 10);
	gtk_signal_connect(GTK_OBJECT(topo->legend_pix), "expose_event",
					   (GtkSignalFunc) spectool_topo_legend_expose, topo);

	attr_list = pango_attr_list_new();
	attr = pango_attr_size_new(7.0 * PANGO_SCALE);
	attr->start_index = 0;
	attr->end_index = 100;
	pango_attr_list_insert(attr_list, attr);

	topo->leg_min = gtk_label_new("0.0%");
	gtk_label_set_use_markup(GTK_LABEL(topo->leg_min), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(topo->leg_min), FALSE);
	gtk_label_set_attributes(GTK_LABEL(topo->leg_min), attr_list);
	gtk_misc_set_alignment(GTK_MISC(topo->leg_min), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(legendh), topo->leg_min, TRUE, TRUE, 0);
	gtk_widget_show(topo->leg_min);

	topo->leg_max = gtk_label_new("100%");
	gtk_label_set_use_markup(GTK_LABEL(topo->leg_max), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(topo->leg_max), FALSE);
	gtk_label_set_attributes(GTK_LABEL(topo->leg_max), attr_list);
	gtk_misc_set_alignment(GTK_MISC(topo->leg_max), 1, 0.5);
	gtk_box_pack_start(GTK_BOX(legendh), topo->leg_max, TRUE, TRUE, 0);
	gtk_widget_show(topo->leg_max);

	pango_attr_list_unref(attr_list);

	gtk_widget_show(legendv);
	gtk_widget_show(legendh);
	gtk_widget_show(topo->legend_pix);
}

