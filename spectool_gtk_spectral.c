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

#include "spectool_gtk_spectral.h"

char spectral_help_txt[] = 
"<b>Spectral View</b>\n\n"
"\"Waterfall\" style view over time showing the peak spectrum usage for each "
"time slice.";

/* Default set of colors inherited from the old spectool_gtk code */
float spect_default_colormap[] = {
	HC2CC(0x00), HC2CC(0x45), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0x5C), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0x73), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0x8A), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0xA1), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0xB8), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0xCF), HC2CC(0xFE),
	HC2CC(0x00), HC2CC(0xE6), HC2CC(0xFE),
	HC2CC(0x1A), HC2CC(0xEB), HC2CC(0xCB),
	HC2CC(0x33), HC2CC(0xF0), HC2CC(0x98),
	HC2CC(0x4D), HC2CC(0xF5), HC2CC(0x66),
	HC2CC(0x66), HC2CC(0xFA), HC2CC(0x33),
	HC2CC(0x80), HC2CC(0xFF), HC2CC(0x00),
	HC2CC(0x99), HC2CC(0xF5), HC2CC(0x00),
	HC2CC(0xB3), HC2CC(0xEA), HC2CC(0x00),
	HC2CC(0xCC), HC2CC(0xE0), HC2CC(0x00),
	HC2CC(0xE6), HC2CC(0xD5), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0xCB), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0xBC), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0xAD), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0x9E), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0x8F), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0x80), HC2CC(0x00),
	HC2CC(0xFF), HC2CC(0x74), HC2CC(0x0E),
	HC2CC(0xFF), HC2CC(0x68), HC2CC(0x1C),
	HC2CC(0xFF), HC2CC(0x5D), HC2CC(0x29),
	HC2CC(0xFF), HC2CC(0x51), HC2CC(0x37),
	HC2CC(0xFF), HC2CC(0x45), HC2CC(0x45),
	HC2CC(0xFF), HC2CC(0x39), HC2CC(0x53),
	HC2CC(0xFF), HC2CC(0x2D), HC2CC(0x61),
	HC2CC(0xFF), HC2CC(0x25), HC2CC(0x65)
};
int spect_default_colormap_len = 31;

float spect_21_colormap[] = {
	HC2CC(0x00), HC2CC(0x00), HC2CC(0x00),
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
int spect_21_colormap_len = 50;

static void spectool_spectral_class_init(SpectoolSpectralClass *class);
static void spectool_spectral_init(SpectoolSpectral *graph);
static void spectool_spectral_destroy(GtkObject *object);

static gint spectool_spectral_configure(GtkWidget *widget, 
									 GdkEventConfigure *event);

G_DEFINE_TYPE(SpectoolSpectral, spectool_spectral, SPECTOOL_TYPE_WIDGET);

void spectool_spectral_draw(GtkWidget *widget, cairo_t *cr, SpectoolWidget *wwidget) {
	SpectoolSpectral *spectral;
	int nsamp, pos, sp;
	int sh, b_s_y;
	cairo_pattern_t *pattern;
	cairo_matrix_t matrix;
	GdkPixmap *linemap;
	GdkColormap *cmap = gdk_colormap_get_system();
	cairo_t *lcr;

	g_return_if_fail(widget != NULL);

	spectral = SPECTOOL_SPECTRAL(wwidget);

	cairo_save(cr);

	/* Figure out the height mod our number of samples...  wwidget->wbar is
	 * the width of each rectangle */
	sh = (double) wwidget->g_len_y / wwidget->sweepcache->num_alloc;

	if (sh < 3)
		sh = 3;
	else
		sh = sh + 1;

	/* Plot across for each sample, with funny looping to handle the
	 * ring allocation.  The oldest sample is the first non-null sample
	 * after the current position.  We want to go back /drawable/ samples,
	 * so hack in a smarter decrement based on how we know the ring works.
	 * Probably an ugly way of doing it. */
	if (wwidget->sweepcache->pos >= (wwidget->g_len_y / sh)) {
		nsamp = wwidget->sweepcache->pos - (wwidget->g_len_y / sh);
	} else if (wwidget->sweepcache->sweeplist[wwidget->sweepcache->num_alloc - 1] !=
			   NULL) {
		nsamp = wwidget->sweepcache->num_alloc - ((wwidget->g_len_y / sh) -
												  wwidget->sweepcache->pos);
	} else {
		nsamp = 0;
	}

	/* Catch rounding errors and other silliness */
	if (nsamp < 0)
		nsamp = 0;

	pos = 0;
	while (nsamp != wwidget->sweepcache->pos && 
		   pos <= wwidget->sweepcache->num_alloc) {
		spectool_sample_sweep *samp;

		/* Loop around the ring */
		if (nsamp >= wwidget->sweepcache->num_alloc)
			nsamp = 0;

		/* Skip empty allocations in the ring */
		if (wwidget->sweepcache->sweeplist[nsamp] == NULL) {
			nsamp++;
			continue;
		}

		/* Skip ones too old to be seen */
		if (sh * pos > wwidget->g_len_y) {
			break;
		}

		/* Thrash out if we're outside our cache bounds, but it shouldn't
		 * happen */
		if (nsamp < 0 || nsamp >= spectral->line_cache_len) {
			printf("debug - outside of cache range!  %d, cache %d\n",
				   nsamp, spectral->line_cache_len);
			return;
		}

		if (spectral->line_cache[nsamp] == NULL) {
			/* Current sample */
			samp = wwidget->sweepcache->sweeplist[nsamp];

			linemap = gdk_pixmap_new(NULL, wwidget->g_len_x, sh, 
									 cmap->visual->depth);
			gdk_drawable_set_colormap(linemap, cmap);

			/* Make a pattern for cairo to draw */
			pattern = 
				cairo_pattern_create_linear(0, 0, wwidget->g_len_x, 0);
			lcr = gdk_cairo_create(linemap);
			cairo_rectangle(lcr, 0, 0, wwidget->g_len_x, sh);
			cairo_clip(lcr);

			for (sp = 0; sp < samp->num_samples - 1; sp++) {
				int cpos;
				int sdb = 
					SPECTOOL_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm,
									   samp->sample_data[sp]);

				cpos = 
					(float) (spectral->colormap_len) * 
					((float) (abs(sdb) + wwidget->base_db_offset) /
					 (float) (abs(wwidget->min_db_draw) -
							  abs(wwidget->base_db_offset)));

				if (cpos < 0)
					cpos = 0;
				else if (cpos >= spectral->colormap_len)
					cpos = spectral->colormap_len - 1;

				cpos = spectral->colormap_len - cpos;

				/* Pattern add uses 0.0 - 1.0 for stop lengths so we have to calculate
				 * them that way */
				cairo_pattern_add_color_stop_rgb(pattern,
								 (float) (sp) / (samp->num_samples - 1),
								 SPECTOOL_SPECTRAL_COLOR(spectral->colormap, cpos, 0),
								 SPECTOOL_SPECTRAL_COLOR(spectral->colormap, cpos, 1),
								 SPECTOOL_SPECTRAL_COLOR(spectral->colormap, cpos, 2));
			}

			/* Draw a line over our pixmap */
			cairo_set_source(lcr, pattern);
			cairo_rectangle(lcr, -1, -1, wwidget->g_len_x + 1, sh + 1);
			cairo_fill(lcr);
			cairo_destroy(lcr);
			cairo_pattern_destroy(pattern);
			spectral->line_cache[nsamp] = linemap;
		}

		/* Top position of the pixmap line, drawing from the bottom up */
		b_s_y = wwidget->g_end_y - ((pos + 1) * sh);

		gdk_cairo_set_source_pixmap(cr, spectral->line_cache[nsamp], 
									wwidget->g_start_x, b_s_y);
		cairo_rectangle(cr, wwidget->g_start_x + 0.5, b_s_y + 0.5, 
						wwidget->g_len_x, sh + 1);
		cairo_fill(cr);

		nsamp++;
		pos++;

	}

	cairo_restore(cr);
}

static void spectool_spectral_size_allocate(GtkWidget *widget,
										 GtkAllocation *allocation) {
	SpectoolSpectral *spectral = SPECTOOL_SPECTRAL(widget);
	int x;

	GTK_WIDGET_CLASS(spectool_spectral_parent_class)->size_allocate(widget, allocation);

	/* Wipe the line cache on resize */
	if (allocation->width != spectral->oldx ||
		allocation->height != spectral->oldy) {
		spectral->oldx = allocation->width;
		spectral->oldy = allocation->height;

		if (spectral->line_cache != NULL) {
			for (x = 0; x < spectral->line_cache_len; x++) {
				if (spectral->line_cache[x] != NULL) {
					gdk_pixmap_unref(spectral->line_cache[x]);
					spectral->line_cache[x] = NULL;
				}

			}
		}
	}
}

static void spectool_spectral_class_init(SpectoolSpectralClass *class) {
	GObjectClass *gobject_class;
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	gobject_class = G_OBJECT_CLASS(class);
	object_class = GTK_OBJECT_CLASS(class);
	widget_class = GTK_WIDGET_CLASS(class);

	object_class->destroy = spectool_spectral_destroy;
	widget_class->size_allocate = spectool_spectral_size_allocate;
}

static void spectool_spectral_destroy(GtkObject *object) {
	SpectoolSpectral *spectral = SPECTOOL_SPECTRAL(object);
	SpectoolWidget *wwidget;

	wwidget = SPECTOOL_WIDGET(spectral);

	GTK_OBJECT_CLASS(spectool_spectral_parent_class)->destroy(object);
}

static void spectool_spectral_wdr_devbind(GtkWidget *widget, spectool_device_registry *wdr,
									   int slot) {
	SpectoolSpectral *spectral;
	SpectoolWidget *wwidget;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_SPECTOOL_SPECTRAL(widget));
	g_return_if_fail(IS_SPECTOOL_WIDGET(widget));

	spectral = SPECTOOL_SPECTRAL(widget);
	wwidget = SPECTOOL_WIDGET(widget);
}

static void spectool_spectral_wdr_sweep(int slot, int mode, 
									 spectool_sample_sweep *sweep, 
									 void *aux) {
	SpectoolSpectral *spectral;
	SpectoolWidget *wwidget;
	int x, tout;
	spectool_phy *pd;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_SPECTOOL_SPECTRAL(aux));
	g_return_if_fail(IS_SPECTOOL_WIDGET(aux));

	spectral = SPECTOOL_SPECTRAL(aux);
	wwidget = SPECTOOL_WIDGET(aux);

	tout = wwidget->draw_timeout;

	/* Update the timer */
	if (sweep != NULL && sweep->phydev != NULL) {
		pd = (spectool_phy *) sweep->phydev;
#ifdef HAVE_HILDON
		tout = 500 * pd->draw_agg_suggestion;
#else
		tout = 150 * pd->draw_agg_suggestion;
#endif
	}

	if (tout != wwidget->draw_timeout) {
		wwidget->draw_timeout = tout;
		g_source_remove(wwidget->timeout_ref);
		wwidget->timeout_ref = 
			g_timeout_add(wwidget->draw_timeout, 
						  (GSourceFunc) spectool_widget_timeout, wwidget);
	}

	if ((mode & SPECTOOL_POLL_CONFIGURED)) {
		/* Allocate the cache of lines to draw */
		if (spectral->line_cache != NULL) {
			for (x = 0; x < spectral->line_cache_len; x++) {
				if (spectral->line_cache[x] != NULL) {
					gdk_pixmap_unref(spectral->line_cache[x]);
				}

			}
		}
		free(spectral->line_cache);

		spectral->line_cache = malloc(sizeof(GdkPixmap *) *
									  wwidget->sweepcache->num_alloc);
		spectral->line_cache_len = wwidget->sweepcache->num_alloc;
		spectral->n_sweeps_delta = 0;

		for (x = 0; x < spectral->line_cache_len; x++) {
			spectral->line_cache[x] = NULL;
		}
	} else if ((mode & SPECTOOL_POLL_SWEEPCOMPLETE)) {
		/* Null out this sweep in the cache so we have to recalculate it */
		if (wwidget->sweepcache->pos >= 0 && 
			wwidget->sweepcache->pos < spectral->line_cache_len) {
			if (spectral->line_cache[wwidget->sweepcache->pos] != NULL)
				gdk_pixmap_unref(spectral->line_cache[wwidget->sweepcache->pos]);
			spectral->line_cache[wwidget->sweepcache->pos] = NULL;
		}

		spectral->n_sweeps_delta++;
	}
}

GtkWidget *spectool_spectral_new(void) {
	SpectoolSpectral *spectral;
	SpectoolWidget *wwidget;

	spectral = gtk_type_new(spectool_spectral_get_type());

	wwidget = SPECTOOL_WIDGET(spectral);

	return GTK_WIDGET(spectral);
}

static gboolean spectool_spectral_legend_expose(GtkWidget *widget,
											 GdkEventExpose *event,
											 gpointer *aux) {
	cairo_t *cr;
	int x, y, w, h, dw, dh;
	SpectoolSpectral *spectral;
	cairo_pattern_t *pattern;
	int cp;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_SPECTOOL_SPECTRAL(aux));

	spectral = SPECTOOL_SPECTRAL(aux);

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

	for (cp = 0; cp < spectral->colormap_len; cp++) {
		cairo_pattern_add_color_stop_rgb(pattern,
										 (float) (cp) / (spectral->colormap_len),
								 SPECTOOL_SPECTRAL_COLOR(spectral->colormap, cp, 0),
								 SPECTOOL_SPECTRAL_COLOR(spectral->colormap, cp, 1),
								 SPECTOOL_SPECTRAL_COLOR(spectral->colormap, cp, 2));
	}

	cairo_set_source(cr, pattern);
	cairo_rectangle(cr, -1, -1, dw + 1, dh + 1);
	cairo_fill(cr);
	cairo_pattern_destroy(pattern);

	cairo_destroy(cr);

	return FALSE;
}

void spectool_spectral_context_help(gpointer *aux) {
	Spectool_Help_Dialog("Spectral View", spectral_help_txt);
}

static void spectool_spectral_init(SpectoolSpectral *spectral) {
	SpectoolWidget *wwidget;

	GtkWidget *temp;
	GtkWidget *legendv, *legendh;
	PangoAttrList *attr_list;
	PangoAttribute *attr;

	wwidget = SPECTOOL_WIDGET(spectral);

	wwidget->sweep_num_samples = SPECTOOL_SPECTRAL_NUM_SAMPLES;
	wwidget->sweep_keep_avg = 0;
	wwidget->sweep_keep_peak = 0;

	wwidget->sweep_num_aggregate = 3;

	wwidget->hlines = 8;
	wwidget->base_db_offset = -30;

	wwidget->graph_title = strdup("<b>Spectral View</b>");
	wwidget->graph_title_bg = strdup("#CC0000");
	wwidget->graph_control_bg = strdup("#CCA0A0");

	wwidget->show_channels = 0;
	wwidget->show_dbm = 0;
	wwidget->show_dbm_lines = 0;

	wwidget->wdr_sweep_func = spectool_spectral_wdr_sweep;
	wwidget->wdr_devbind_func = spectool_spectral_wdr_devbind;
	wwidget->draw_mouse_move_func = NULL;
	wwidget->draw_mouse_click_func = NULL;
#ifdef HAVE_HILDON
	wwidget->draw_timeout = 1500;
#else
	wwidget->draw_timeout = 500;
#endif
	wwidget->draw_func = spectool_spectral_draw;

	wwidget->menu_func = NULL;
	wwidget->help_func = spectool_spectral_context_help;

	spectral->colormap = spect_21_colormap;
	spectral->colormap_len = spect_21_colormap_len;

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

	spectral->legend_pix = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(legendv), spectral->legend_pix, FALSE, FALSE, 2);
	gtk_drawing_area_size(GTK_DRAWING_AREA(spectral->legend_pix), -1, 10);
	gtk_signal_connect(GTK_OBJECT(spectral->legend_pix), "expose_event",
					   (GtkSignalFunc) spectool_spectral_legend_expose, spectral);

	attr_list = pango_attr_list_new();
	attr = pango_attr_size_new(7.0 * PANGO_SCALE);
	attr->start_index = 0;
	attr->end_index = 100;
	pango_attr_list_insert(attr_list, attr);

	temp = gtk_label_new("Min");
	gtk_label_set_use_markup(GTK_LABEL(temp), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(temp), FALSE);
	gtk_label_set_attributes(GTK_LABEL(temp), attr_list);
	gtk_misc_set_alignment(GTK_MISC(temp), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(legendh), temp, TRUE, TRUE, 0);
	gtk_widget_show(temp);

	temp = gtk_label_new("Max");
	gtk_label_set_use_markup(GTK_LABEL(temp), FALSE);
	gtk_label_set_use_underline(GTK_LABEL(temp), FALSE);
	gtk_label_set_attributes(GTK_LABEL(temp), attr_list);
	gtk_misc_set_alignment(GTK_MISC(temp), 1, 0.5);
	gtk_box_pack_start(GTK_BOX(legendh), temp, TRUE, TRUE, 0);
	gtk_widget_show(temp);

	pango_attr_list_unref(attr_list);

	gtk_widget_show(legendv);
	gtk_widget_show(legendh);
	gtk_widget_show(spectral->legend_pix);

	spectral->oldx = spectral->oldy = 0;
}

