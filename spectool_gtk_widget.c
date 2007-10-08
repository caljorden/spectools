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

#include "spectool_gtk_widget.h"

/* control/picker pane width and initial height */
#define WISPY_WIDGET_PADDING	5

static void wispy_widget_class_init(WispyWidgetClass *class);
static void wispy_widget_init(WispyWidget *graph);
static void wispy_widget_destroy(GtkObject *object);
static void wispy_widget_realize(GtkWidget *widget);

static gint wispy_widget_configure(GtkWidget *widget, 
									 GdkEventConfigure *event);
static gboolean wispy_widget_expose(GtkWidget *widget, 
									  GdkEventExpose *event,
									  gpointer *aux);
static void wispy_widget_size_allocate(GtkWidget *widget,
										 GtkAllocation *allocation);
static void wispy_widget_wdr_sweep(int slot, int mode,
								   wispy_sample_sweep *sweep, void *aux);

static GType wispy_widget_child_type (GtkContainer *container);

G_DEFINE_TYPE(WispyWidget, wispy_widget, GTK_TYPE_BIN);

void wispychannelopts_init(WispyChannelOpts *in) {
	in->chan_h = -1;
	in->chanhit = NULL;
	in->chancolors = NULL;
	in->chanset = NULL;
	in->hi_chan = -1;
}
						
static void wispy_widget_destroy(GtkObject *object) {
	WispyWidget *wwidget = WISPY_WIDGET(object);

	wdr_del_sweepcb(wwidget->wdr, wwidget->wdr_slot,
					wispy_widget_wdr_sweep, wwidget);

	if (wwidget->wdr_slot >= 0) {
		wdr_del_ref(wwidget->wdr, wwidget->wdr_slot);
		wwidget->wdr_slot = -1;
	}

	if (wwidget->timeout_ref >= 0) {
		g_source_remove(wwidget->timeout_ref);
		wwidget->timeout_ref = -1;
	}

	GTK_OBJECT_CLASS(wispy_widget_parent_class)->destroy(object);
}

GtkWidget *wispy_widget_new() {
	WispyWidget *wwidget;

	wwidget = gtk_type_new(wispy_widget_get_type());

	return GTK_WIDGET(wwidget);
}

static void wispy_widget_wdr_sweep(int slot, int mode,
								   wispy_sample_sweep *sweep, void *aux) {
	WispyWidget *wwidget;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	wwidget = WISPY_WIDGET(aux);

	/* Generic sweep handler to add it to our cache, all things get this */
	if ((mode & WISPY_POLL_ERROR)) {
		wwidget->phydev = NULL;
		if (wwidget->sweepcache != NULL) {
			wispy_cache_free(wwidget->sweepcache);
			wwidget->sweepcache = NULL;
		}
		wdr_del_ref(wwidget->wdr, wwidget->wdr_slot);
		wwidget->wdr_slot = -1;
	} else if ((mode & WISPY_POLL_CONFIGURED)) {
		if (wwidget->sweepcache != NULL) {
			wispy_cache_free(wwidget->sweepcache);
			wwidget->sweepcache = NULL;
		}

		if (wwidget->sweep_num_samples > 0) {
			wwidget->sweepcache = 
				wispy_cache_alloc(wwidget->sweep_num_samples, 
								  wwidget->sweep_keep_avg, 
								  wwidget->sweep_keep_peak);
		}

		wwidget->amp_offset_mdbm = 
			wwidget->phydev->device_spec->supported_ranges[0].amp_offset_mdbm;
		wwidget->amp_res_mdbm = 
			wwidget->phydev->device_spec->supported_ranges[0].amp_res_mdbm;

		wwidget->base_db_offset =
			WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm,
							   wwidget->phydev->device_spec->supported_ranges[0].rssi_max);
		wwidget->min_db_draw = 
			WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm, 0);
	
	} else if (wwidget->sweepcache != NULL && sweep != NULL) {
		wispy_cache_append(wwidget->sweepcache, sweep);
		wwidget->min_db_draw = 
			WISPY_RSSI_CONVERT(wwidget->amp_offset_mdbm, wwidget->amp_res_mdbm, 
							   sweep->min_rssi_seen > 2 ? 
							   sweep->min_rssi_seen - 2: sweep->min_rssi_seen);
	}

	/* Call the secondary sweep handler */
	if (wwidget->wdr_sweep_func != NULL)
		(*(wwidget->wdr_sweep_func))(slot, mode, sweep, aux);
}

/* Common level function for opening a device, calls the secondary level
 * function if one exists after device is linked in */
void wispy_widget_bind_dev(GtkWidget *widget, wispy_device_registry *wdr,
						   int slot) {
	WispyWidget *wwidget;
	char ltxt[256];
	wispy_sample_sweep *ran;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(widget));

	wwidget = WISPY_WIDGET(widget);

	wwidget->wdr = wdr;

	/* Unref using the old slot, but only after we've reffed using the 
	 * new one, incase someone picked the same dev twice, we need this ordering
	 * to prevent it from getting reaped from a 0 count on the ref*/
	wdr_add_ref(wdr, slot);
	wdr_del_ref(wdr, wwidget->wdr_slot);

	/* Allocate the new device and drop our old sweep cache if one existed */
	wwidget->wdr_slot = slot;
	wwidget->phydev = wdr_get_phy(wwidget->wdr, slot);

	/* register a sweep callback */
	wdr_add_sweepcb(wwidget->wdr, wwidget->wdr_slot,
					wispy_widget_wdr_sweep, wwidget->sweep_num_aggregate,
					wwidget);

	/* Call our secondary open function */
	if (wwidget->wdr_devbind_func != NULL)
		(*(wwidget->wdr_devbind_func))(widget, wdr, slot);

	/* Force calibration */
	if (wispy_get_state(wwidget->phydev) > WISPY_STATE_CONFIGURING)
		wispy_widget_wdr_sweep(-1, WISPY_POLL_CONFIGURED, NULL, widget);

	/* Toggle off the "no device" panel and give us the graph */
	gtk_widget_show(wwidget->draw);
}

static gboolean wispy_widget_menu_button_press(gpointer *widget,
											   GdkEvent *event) {
	WispyWidgetController *con = (WispyWidgetController *) widget;
	char alt_title_text[32];

	g_return_if_fail(widget != NULL);
	g_return_if_fail(event != NULL);

	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *bevent = (GdkEventButton *) event;

		/* toggle showing/hiding the widgets */
		if (GTK_WIDGET_VISIBLE(con->wwidget)) {
			gtk_widget_hide(GTK_WIDGET(con->wwidget));
			if (con->wwidget->graph_title != NULL) {
				snprintf(alt_title_text, 32, "%s (hidden)",
						 con->wwidget->graph_title);
				gtk_label_set_markup(GTK_LABEL(con->label), alt_title_text);
			}

			gtk_widget_destroy(con->arrow);
			con->arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_OUT);
			gtk_container_add(GTK_CONTAINER(con->menubutton), con->arrow);
			gtk_widget_show(con->arrow);
		} else {
			gtk_widget_show(GTK_WIDGET(con->wwidget));
			if (con->wwidget->graph_title != NULL) {
				gtk_label_set_markup(GTK_LABEL(con->label), 
									 con->wwidget->graph_title);
			}

			gtk_widget_destroy(con->arrow);
			con->arrow = gtk_arrow_new(GTK_ARROW_UP, GTK_SHADOW_OUT);
			gtk_container_add(GTK_CONTAINER(con->menubutton), con->arrow);
			gtk_widget_show(con->arrow);
		}

		return TRUE;
	}

	return FALSE;
}

WispyWidgetController *wispy_widget_buildcontroller(GtkWidget *widget) {
	GtkWidget *hbox;
	WispyWidgetController *con = 
		(WispyWidgetController *) malloc(sizeof(WispyWidgetController));
	WispyWidget *wwidget;

	GdkColor c;
	GtkStyle *style;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(widget));

	wwidget = WISPY_WIDGET(widget);

	con->wwidget = wwidget;

	/* Colored titled box and label */
	con->evbox = gtk_event_box_new();

	if (wwidget->graph_title_bg != NULL) {
		gdk_color_parse(wwidget->graph_title_bg, &c);
		style = gtk_style_new();
		gtk_widget_set_style(GTK_WIDGET(con->evbox), style);
		style->bg[GTK_STATE_NORMAL] = c;
		gtk_style_unref(style);
	} else {
		fprintf(stderr, "BUG: %p Missing graph_title_bg in widget base\n", widget);
	}

	if (wwidget->graph_title != NULL) {
		hbox = gtk_hbox_new(FALSE, 2);
		gtk_container_add(GTK_CONTAINER(con->evbox), hbox);

		con->label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(con->label), wwidget->graph_title);
		gtk_box_pack_start(GTK_BOX(hbox), con->label, FALSE, FALSE, 0);

		con->menubutton = gtk_button_new();
		con->arrow = gtk_arrow_new(GTK_ARROW_UP, GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(con->menubutton), con->arrow);
		gtk_box_pack_end(GTK_BOX(hbox), con->menubutton, FALSE, FALSE, 0);
		g_signal_connect_swapped(G_OBJECT(con->menubutton), "event", 
								 G_CALLBACK(wispy_widget_menu_button_press),
								 G_OBJECT(con));

		gtk_widget_show(con->arrow);
		gtk_widget_show(con->menubutton);
		gtk_widget_show(hbox);
		gtk_widget_show(con->label);
	} else {
		fprintf(stderr, "BUG: %p Missing graph_title in widget base\n", widget);
	}

	return con;
}

void wispy_widget_link_channel(GtkWidget *widget, WispyChannelOpts *opts) {
	WispyWidget *wwidget;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(widget));

	wwidget = WISPY_WIDGET(widget);

	wwidget->chanopts = opts;
}

gint wispy_widget_mouse_click(GtkWidget *widget, GdkEventButton *button, gpointer *aux) {
	WispyWidget *wwidget;
	GtkWidget *menu;
	GdkEvent *event = (GdkEvent *) button;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	wwidget = WISPY_WIDGET(aux);

	/* Catch rightclick */
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 3 &&
		wwidget->menu_func != NULL) {

		menu = gtk_menu_new();
		gtk_widget_show(menu);

		(*wwidget->menu_func)(GTK_WIDGET(wwidget), menu);

		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 
					   button->button, button->time);

		return 1;
	}

	if (wwidget->draw_mouse_click_func != NULL) 
		return (*wwidget->draw_mouse_click_func)(widget, button, aux);

	return 0;
}

void wispy_widget_buildgui(WispyWidget *widget) {
	GtkWidget *vbox, *hbox, *hbox2;
	GtkWidget *temp;
	/* Sigh, is this really the only way to change the colors here? */
	GtkWidget *eb;

	GdkColor c;
	GtkStyle *style;

	g_return_if_fail(widget != NULL);

	/* Brute override of the style */
	gdk_color_parse("#505050", &c);
	style = gtk_style_new();
	gtk_widget_set_style(GTK_WIDGET(widget), style);
	style->bg[GTK_STATE_NORMAL] = c;
	style->base[GTK_STATE_NORMAL] = c;
	gtk_style_unref(style);

	/* Main packing of graph and sidebar */
	hbox = GTK_BIN(widget)->child;

	/* Make the cairo drawing area and link it into events */
	widget->draw = gtk_drawing_area_new();

	gtk_signal_connect(GTK_OBJECT(widget->draw), "expose_event",
					   (GtkSignalFunc) wispy_widget_expose, widget);

	/* Set mask */
	gtk_widget_set_events(widget->draw, GDK_EXPOSURE_MASK |
						  GDK_LEAVE_NOTIFY_MASK |
						  GDK_BUTTON_PRESS_MASK |
						  GDK_POINTER_MOTION_MASK |
						  GDK_POINTER_MOTION_HINT_MASK);

	/* Attach mouse */
	gtk_signal_connect(GTK_OBJECT(widget->draw), "button_press_event",
					   (GtkSignalFunc) wispy_widget_mouse_click, 
					   widget);
	if (widget->draw_mouse_move_func != NULL)
		gtk_signal_connect(GTK_OBJECT(widget->draw), "motion_notify_event",
						   (GtkSignalFunc) widget->draw_mouse_move_func, 
						   widget);

	gtk_box_pack_start(GTK_BOX(hbox), widget->draw, TRUE, TRUE, 0);

	/* Sidebar contents */
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), WISPY_WIDGET_PADDING);
	gtk_box_pack_end(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

	eb = gtk_event_box_new();
	gtk_box_pack_start(GTK_BOX(vbox), eb, TRUE, TRUE, 0);

	if (widget->graph_control_bg != NULL) {
		gdk_color_parse(widget->graph_control_bg, &c);
		style = gtk_style_new();
		gtk_widget_set_style(GTK_WIDGET(eb), style);
		style->bg[GTK_STATE_NORMAL] = c;
		gtk_style_unref(style);
	} else {
		fprintf(stderr, "BUG: %p Missing graph_control_bg in widget base\n", widget);
	}

	widget->infoeb = eb;
	/* widget->framevbox = vbox; */ /* Framing vbox holds title, etc */
	widget->vbox = gtk_vbox_new(FALSE, 0); /* vbox is where extra GUI bits go */
	gtk_container_add(GTK_CONTAINER(eb), widget->vbox);

	gtk_widget_show(widget->draw); 
	gtk_widget_show(widget->vbox);
	gtk_widget_show(vbox);
	gtk_widget_show(eb);
	gtk_widget_show(hbox);
}

static void wispy_widget_init(WispyWidget *widget) {
	widget->chanopts = NULL;
	widget->g_start_x = widget->g_end_x = widget->g_len_x = 0;
	widget->g_start_y = widget->g_end_y = widget->g_len_y = 0;

	widget->timeout_ref = -1;

	widget->phydev = NULL;
	widget->sweepcache = NULL;
	widget->wdr_slot = -1;

	widget->hbox = gtk_hbox_new(FALSE, WISPY_WIDGET_PADDING);
	gtk_widget_set_parent(widget->hbox, GTK_WIDGET(widget));
	GTK_BIN(widget)->child = widget->hbox;

	widget->offscreen = NULL;
	widget->old_width = widget->old_height = 0;
}

static void wispy_widget_size_allocate(GtkWidget *widget,
										 GtkAllocation *allocation) {
	WispyWidget *wwidget = WISPY_WIDGET(widget);

	widget->allocation = *allocation;

	gtk_widget_set_size_request(wwidget->vbox, allocation->width / 5 + 20, -1);

	if (GTK_BIN(wwidget)->child && GTK_WIDGET_VISIBLE(GTK_BIN(wwidget)->child)) {
		gtk_widget_size_allocate(GTK_BIN(wwidget)->child, allocation);
    }

	if (wwidget->old_width != allocation->width || 
		wwidget->old_height != allocation->height) {
		if (wwidget->offscreen) {
			cairo_surface_destroy(wwidget->offscreen);
			wwidget->offscreen = NULL;
		}

		wwidget->old_width = allocation->width;
		wwidget->old_height = allocation->height;
	}
}

static void wispy_widget_size_request (GtkWidget *widget, GtkRequisition *requisition) {
	WispyWidget *wwidget = WISPY_WIDGET(widget);

	requisition->width = 0;
	requisition->height = 0;

	if (GTK_BIN(wwidget)->child && GTK_WIDGET_VISIBLE(GTK_BIN(wwidget)->child)) {
		GtkRequisition child_requisition;

		gtk_widget_size_request(GTK_BIN(wwidget)->child, &child_requisition);

		requisition->width += child_requisition.width;
		requisition->height += child_requisition.height;
	}
}

void wispy_widget_draw(GtkWidget *widget, cairo_t *cr, WispyWidget *wwidget) {
	cairo_text_extents_t extents;
	int x, chpix, maxcw, start_db;
	const double dash_onoff[] = {2, 4};
	const double dash_ononoff[] = {4, 2};

	char mtext[128];

	int chanmod;

	g_return_if_fail(widget != NULL);
	
	if (GTK_WIDGET_VISIBLE(wwidget) == 0) {
		return;
	}

	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, widget->allocation.width, widget->allocation.height);
	cairo_set_source_rgb(cr, HC2CC(0x50), HC2CC(0x50), HC2CC(0x50));
	cairo_fill(cr);
	cairo_stroke(cr);

	/* Render the offscreen widget */
	if (wwidget->offscreen == NULL) {
		return;
	}

	cairo_save(cr);
	cairo_set_source_surface(cr, wwidget->offscreen, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);

	/* Render the selected channel text on top of any other data */
	if (wwidget->show_dbm || wwidget->show_dbm_lines) {
		/* Draw the dBm lines and power labels */
		maxcw = 0;
		cairo_save(cr);
		cairo_set_line_width(cr, 0.5);

		start_db = 0;
		for (x = wwidget->base_db_offset - 1; x > wwidget->min_db_draw;
			 x--) {
			if (x % 10 == 0) {
				start_db = x;
				break;
			}
		}
		
		if (start_db == 0)
			start_db = wwidget->base_db_offset;

		for (x = start_db; x > wwidget->min_db_draw; x -= 10) {
			int py;

			py = (float) wwidget->g_len_y * 
				(float) ((float) (abs(x) + wwidget->base_db_offset) /
						 (float) (abs(wwidget->min_db_draw) +
								  wwidget->base_db_offset));

			cairo_set_source_rgb(cr, 1, 1, 1);

			if (wwidget->show_dbm_lines) {
				cairo_set_dash(cr, dash_onoff, 2, 0);
				/* .5 hack for pixel alignment */
				cairo_move_to(cr, wwidget->g_start_x, 
							  wwidget->g_start_y + py + 0.5);
				cairo_line_to(cr, wwidget->g_end_x, wwidget->g_start_y + py + 0.5);
				cairo_stroke(cr);
			}

			if (wwidget->show_dbm) {
				snprintf(mtext, 128, "%d dBm", x);

				cairo_select_font_face(cr, "Helvetica", 
									   CAIRO_FONT_SLANT_NORMAL, 
									   CAIRO_FONT_WEIGHT_BOLD);
				cairo_set_font_size(cr, 10);
				cairo_text_extents(cr, mtext, &extents);
				cairo_move_to(cr, wwidget->g_start_x - wwidget->dbm_w, 
							  wwidget->g_start_y + py + (extents.height / 2));

				cairo_show_text(cr, mtext);
			}

		}
		cairo_restore(cr);
	}

	if (wwidget->chanopts != NULL && wwidget->chanopts->chanset != NULL) {
		/* Plot the highlighted channels */
		for (x = 0; x < wwidget->chanopts->chanset->chan_num; x++) {
			int center, spread;
			int start, end;

			if (wwidget->chanopts->chanhit[x] == 0 &&
				wwidget->chanopts->hi_chan != x)
				continue;

			if (wwidget->sweepcache->latest == NULL) {
				continue;
			}

			/*
			 * We'll draw the sidebar lines on this at the end so it's on
			 * top of the spectral data
			 */
			center = ((float) wwidget->g_len_x /
					  (wwidget->sweepcache->latest->end_khz -
					   wwidget->sweepcache->latest->start_khz)) *
				(wwidget->chanopts->chanset->chan_freqs[x] - 
				 wwidget->sweepcache->latest->start_khz) + wwidget->g_start_x;

			spread = ((float) wwidget->g_len_x /
					  (wwidget->sweepcache->latest->end_khz -
					   wwidget->sweepcache->latest->start_khz)) *
				(wwidget->chanopts->chanset->chan_width); 

			start = center - (spread / 2);
			end = center + (spread / 2);

			if (start < wwidget->g_start_x) {
				start = wwidget->g_start_x;
			}

			if (end > wwidget->g_end_x) {
				end = wwidget->g_end_x;
			}

			cairo_save(cr);
			/* White for highlighted channel, color for active channel */
			if (wwidget->chanopts->hi_chan == x) {
				cairo_set_source_rgba(cr, 1, 1, 1, 0.20);
			} else {
				cairo_set_source_rgba(cr,
									  wwidget->chanopts->chancolors[(3 * x) + 0],
									  wwidget->chanopts->chancolors[(3 * x) + 1],
									  wwidget->chanopts->chancolors[(3 * x) + 2],
									  0.25);
			}
			cairo_rectangle(cr, start + 0.5, wwidget->g_start_y + 0.5, 
							end - start, wwidget->g_len_y);
			cairo_fill_preserve(cr);
			cairo_stroke(cr);
			cairo_move_to(cr, center, wwidget->g_start_y);
			cairo_line_to(cr, center, wwidget->g_end_y);
			cairo_stroke(cr);
			cairo_restore(cr);

			/*
			if (wwidget->chanopts->hi_chan > -1) {
				snprintf(mtext, 128, "Channel %s, %d%s",
				wwidget->chanopts->chanset->chan_text[wwidget->chanopts->hi_chan],
				wwidget->chanopts->chanset->startkhz >= 1000 ? 
				wwidget->chanopts->chanset->chan_freqs[wwidget->chanopts->hi_chan]/ 1000 : 
				wwidget->chanopts->chanset->chan_freqs[x],
				wwidget->chanopts->chanset->startkhz >= 1000 ? "MHz" : "KHz");

				cairo_save(cr);
				cairo_set_source_rgb(cr, 1, 1, 1);
				cairo_select_font_face(cr, "Helvetica", 
									   CAIRO_FONT_SLANT_NORMAL, 
									   CAIRO_FONT_WEIGHT_BOLD);
				cairo_set_font_size(cr, 14);
				cairo_text_extents(cr, mtext, &extents);
				cairo_move_to(cr, wwidget->g_end_x - extents.width - 5,
							  wwidget->g_start_y + extents.height + 5);
				cairo_show_text(cr, mtext);
				cairo_restore(cr);
			}
			*/
		}
	}

	/* Redraw the bounding box */
	cairo_save(cr);
	cairo_rectangle(cr, wwidget->g_start_x, wwidget->g_start_y, 
					wwidget->g_len_x + 0.5, wwidget->g_len_y + 0.5);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_stroke(cr);
	cairo_restore(cr);

	cairo_restore(cr);
}

void wispy_widget_graphics_update(WispyWidget *wwidget) {
	cairo_text_extents_t extents;
	int x, chpix, maxcw, start_db;
	const double dash_onoff[] = {2, 4};
	const double dash_ononoff[] = {4, 2};
	cairo_t *offcr;
	GtkWidget *widget;

	char mtext[128];

	int chanmod;

	g_return_if_fail(wwidget != NULL);
	
	if (GTK_WIDGET_VISIBLE(wwidget) == 0) {
		return;
	}

	widget = wwidget->draw;

	/* Make an offscreen surface for this wispywidget if one doesn't exist (ie, it's a 
	 * new widget, or it's been resized) */
	if (wwidget->offscreen == NULL) {
		wwidget->offscreen = 
			cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
									   widget->allocation.width,
									   widget->allocation.height);
	}
	offcr = cairo_create(wwidget->offscreen);

	/* Draw all our base stuff into the offscreen cr */
	cairo_save(offcr);
	cairo_rectangle(offcr, 0, 0, widget->allocation.width, widget->allocation.height);
	cairo_set_source_rgb(offcr, HC2CC(0x50), HC2CC(0x50), HC2CC(0x50));
	cairo_fill(offcr);
	cairo_stroke(offcr);

	wwidget->g_len_x = widget->allocation.width - (WISPY_WIDGET_PADDING * 2);
	wwidget->g_len_y = widget->allocation.height - (WISPY_WIDGET_PADDING * 2);
	wwidget->g_start_x = WISPY_WIDGET_PADDING;
	wwidget->g_start_y = WISPY_WIDGET_PADDING;
	wwidget->g_end_x = wwidget->g_start_x + wwidget->g_len_x;
	wwidget->g_end_y = wwidget->g_start_y + wwidget->g_len_y;

	/* We haven't been initialized so we don't know... anything */
	if (wwidget->wdr_slot < 0) {
		cairo_destroy(offcr);
		return;
	}

	/* We haven't calibrated, so we don't know our channels, etc */
	if (wwidget->sweepcache == NULL ||
		(wwidget->sweepcache != NULL && wwidget->sweepcache->pos < 0)) {
		cairo_set_source_rgb(offcr, HC2CC(0xFF), HC2CC(0xFF), HC2CC(0xFF));
		cairo_select_font_face(offcr, "Helvetica", 
							   CAIRO_FONT_SLANT_NORMAL, 
							   CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(offcr, 16);
		cairo_text_extents(offcr, "Device calibrating...", &extents);
		cairo_move_to(offcr, wwidget->g_start_x + (wwidget->g_len_x / 2) - 
					  (extents.width / 2),
					  wwidget->g_start_y + (wwidget->g_len_y / 2) - 
					  (extents.height / 2));
		cairo_show_text(offcr, "Device calibrating...");
		cairo_destroy(offcr);
		return;
	}

	/* Assume we have at most a 3 digit DBM rating, and figure out the scaling.
	 * We use 000 since they're nice fat digits even with a variable-width font,
	 * and then we slap another 10% on the result. 
	 * We re-use the same string for height calcs for the channel list, just
	 * new size */
	cairo_save(offcr);
	snprintf(mtext, 128, "-000 dBm");
	cairo_select_font_face(offcr, "Helvetica", 
						   CAIRO_FONT_SLANT_NORMAL, 
						   CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(offcr, 10);
	cairo_text_extents(offcr, mtext, &extents);
	wwidget->dbm_w = extents.width + ((double) extents.width * 0.1);
	cairo_set_font_size(offcr, 14);
	cairo_text_extents(offcr, mtext, &extents);
	cairo_restore(offcr);

	/* Figure out a square scaled to the number of samples we have and
	 * build the size of the animated graph.  wbar ends up being the 
	 * width of a sample, so we can use that for all sorts of math later */
	wwidget->wbar = 
		(double) (widget->allocation.width - wwidget->dbm_w -
		 (WISPY_WIDGET_PADDING * 2) - 5) / 
		(double) (wwidget->sweepcache->latest->num_samples - 1);
	wwidget->g_len_x = wwidget->wbar * 
		(wwidget->sweepcache->latest->num_samples - 1);
	wwidget->g_len_y = widget->allocation.height - (WISPY_WIDGET_PADDING * 2);
	wwidget->g_start_x = WISPY_WIDGET_PADDING + wwidget->dbm_w;
	wwidget->g_start_y = WISPY_WIDGET_PADDING;
	wwidget->g_end_x = wwidget->g_start_x + wwidget->g_len_x;
	wwidget->g_end_y = wwidget->g_start_y + wwidget->g_len_y;

	cairo_rectangle(offcr, wwidget->g_start_x, wwidget->g_start_y, 
					wwidget->g_len_x, wwidget->g_len_y);
	cairo_set_source_rgb(offcr, HC2CC(0x00), HC2CC(0x00), HC2CC(0x00));
	cairo_fill_preserve(offcr);
	cairo_stroke(offcr);

	/* Call the second-level draw */
	if (wwidget->draw_func != NULL)
		(*(wwidget->draw_func))(widget, offcr, wwidget);

	cairo_destroy(offcr);
}

/* Expose event on the drawable widget */
static gint wispy_widget_expose(GtkWidget *widget, GdkEventExpose *event,
								gpointer *aux) {
	int x, y, w, h;
	WispyWidget *wwidget;
	cairo_t *cr;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));
	wwidget = WISPY_WIDGET(aux);

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

	cairo_rectangle(cr, x, y, w, h);

	cairo_clip(cr);

	wispy_widget_draw(widget, cr, wwidget);

	cairo_destroy(cr);

	return FALSE;
}

void wispy_widget_update(GtkWidget *widget) {
	WispyWidget *wwidget;
	GdkRectangle update_rect;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(widget));

	wwidget = WISPY_WIDGET(widget);

	g_return_if_fail(wwidget->draw != NULL);

	update_rect.x = wwidget->draw->allocation.x;
	update_rect.y = wwidget->draw->allocation.y;
	update_rect.width = wwidget->draw->allocation.width;
	update_rect.height = wwidget->draw->allocation.height;

	gtk_widget_draw(widget, &update_rect);

	if (wwidget->update_func != NULL)
		(*(wwidget->update_func))(widget);
}

gint wispy_widget_timeout(gpointer *data) {
	/* Kick the graphics update out here during a timered update */
	wispy_widget_graphics_update(WISPY_WIDGET(data));

	/* do a GTK level update */
	wispy_widget_update(GTK_WIDGET(data));
	return TRUE;
}

static GType wispy_widget_child_type(GtkContainer *container) {
	if (!GTK_BIN(container)->child)
		return GTK_TYPE_WIDGET;
	else
		return G_TYPE_NONE;
}

static void wispy_widget_class_init(WispyWidgetClass *class) {
	GObjectClass *gobject_class;
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;

	gobject_class = G_OBJECT_CLASS(class);
	object_class = GTK_OBJECT_CLASS(class);
	widget_class = GTK_WIDGET_CLASS(class);
	container_class = (GtkContainerClass*) class;

	object_class->destroy = wispy_widget_destroy;
	widget_class->size_allocate = wispy_widget_size_allocate;
	widget_class->size_request = wispy_widget_size_request;

	container_class->child_type = wispy_widget_child_type;
}

/* Annoying that nothing else seems to include this, but we need it for
 * calculating the gradient of colors for the channel highlights */
inline void rgb_to_hsv(double r, double g, double b, 
					   double *h, double *s, double *v) {
	double min, delta;

	if ((b > g) && (b > r)) {
		*v = b;
		if (v != 0) {
			if (r > g)
				min = g;
			else
				min = r;
			delta = *v - min;
			if (delta != 0) {
				*s = (delta / *v);
				*h = 4 + (r - g) / delta;
			} else {
				*s = 0;
				*h = 4 + (r - g);
			}
			*h *= 60;
			if (*h < 0)
				*h += 360;
			*v = *v / 255;
		} else {
			*s = 0;
			*h = 0;
		}
	} else if (g > r) {
		*v = g;
		if (*v != 0) {
			if (r > b)
				min = b;
			else
				min = r;

			delta = *v - min;
			if (delta != 0) {
				*s = (delta / *v);
				*h = 2 + (b - r) / delta;
			} else {
				*s = 0;
				*h = 2 + (b - r);
			}
			*h *= 60;
			if (*h < 0)
				*h += 360;
			*v = *v / 255;
		} else {
			*s = 0;
			*h = 0;
		}
	} else {
		*v = r;
		if (v != 0) {
			if (g > b)
				min = b;
			else
				min = g;

			delta = *v - min;

			if (delta != 0) {
				*s = (delta / *v);
				*h = (g - b) / delta;
			} else {
				*s = 0;
				*h = (g - b);
			}

			*h *= 60;
			if (*h < 0)
				*h += 360;
			*v = *v / 255;
		} else {
			*s = 0;
			*h = 0;
		}
	}
}

inline void hsv_to_rgb(double *r, double *g, double *b, 
					   double h, double s, double v) {
	double hf = h / 60;
	int i = floor(hf);
	double f = hf - i;
	double pv = v * (1 - s);
	double qv = v * (1 - s * f);
	double tv = v * (1 - s * (1 - f));

	if (v == 0) {
		*r = 0;
		*g = 0;
		*b = 0;
		return;
	}

	if (i == -1) {
		*r = v;
		*g = pv;
		*b = qv;
	} else if (i == 0) {
		*r = v;
		*g = tv;
		*b = pv;
	} else if (i == 1) {
		*r = qv;
		*g = v;
		*b = pv;
	} else if (i == 2) {
		*r = pv;
		*g = v;
		*b = tv;
	} else if (i == 3) {
		*r = pv;
		*g = qv;
		*b = v;
	} else if (i == 4) {
		*r = tv;
		*g = pv;
		*b = v;
	} else if (i == 5) {
		*r = v;
		*g = pv;
		*b = qv;
	} else if (i == 6) {
		*r = v;
		*g = tv;
		*b = pv;
	} else {
		*r = 0;
		*b = 0;
		*g = 0;
	}

	*r *= 255;
	*b *= 255;
	*g *= 255;
}

void wispy_widget_context_channels(gpointer *aux) {
	WispyWidget *wwidget;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	wwidget = WISPY_WIDGET(aux);

	if (wwidget->show_channels) {
		wwidget->show_channels = 0;
	} else {
		wwidget->show_channels = 1;
	}

	wispy_widget_update(GTK_WIDGET(wwidget));
}

void wispy_widget_context_dbm(gpointer *aux) {
	WispyWidget *wwidget;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	wwidget = WISPY_WIDGET(aux);

	if (wwidget->show_dbm) {
		wwidget->show_dbm = 0;
	} else {
		wwidget->show_dbm = 1;
	}

	wispy_widget_update(GTK_WIDGET(wwidget));
}

void wispy_widget_context_dbmlines(gpointer *aux) {
	WispyWidget *wwidget;

	g_return_if_fail(aux != NULL);
	g_return_if_fail(IS_WISPY_WIDGET(aux));

	wwidget = WISPY_WIDGET(aux);

	if (wwidget->show_dbm_lines) {
		wwidget->show_dbm_lines = 0;
	} else {
		wwidget->show_dbm_lines = 1;
	}

	wispy_widget_update(GTK_WIDGET(wwidget));
}

