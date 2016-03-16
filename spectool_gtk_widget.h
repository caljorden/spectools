/* spectool generic gtk widget 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#ifndef __SPECTOOL_GTK_WIDGET_H__
#define __SPECTOOL_GTK_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>
#include <cairo.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"

/* 
 * Common GTK widget layer, provides functions for handling picking a device, 
 * determining and printing channels, etc
 *
 * Should save several hundred lines of shared code off descendant widgets
 * (Spectral, Planar, Topographic, Foo)
 *
 */

G_BEGIN_DECLS

/* Hex color to cairo color */
#define HC2CC(x)				((double) ((double) (x) / (double) 0xFF))

#define SPECTOOL_TYPE_WIDGET \
	(spectool_widget_get_type())
#define SPECTOOL_WIDGET(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	SPECTOOL_TYPE_WIDGET, SpectoolWidget))
#define SPECTOOL_WIDGET_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	SPECTOOL_TYPE_WIDGET, SpectoolWidgetClass))
#define IS_SPECTOOL_WIDGET(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	SPECTOOL_TYPE_WIDGET))
#define IS_SPECTOOL_WIDGET_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	SPECTOOL_TYPE_WIDGET))

typedef struct _SpectoolWidget SpectoolWidget;
typedef struct _SpectoolWidgetClass SpectoolWidgetClass;

typedef struct _SpectoolChannelOpts {
	int chan_h;
	int *chanhit;
	double *chancolors;
	struct spectool_channels *chanset;

	/* Hilighted channel, if we're showing channels */
	int hi_chan;
} SpectoolChannelOpts;

void spectoolchannelopts_init(SpectoolChannelOpts *in);

struct _SpectoolWidget {
	GtkBinClass parent;

	int hlines;
	/* Top of DB graph, max sample reported */
	int base_db_offset;
	/* Bottom of db graph, min sample reported */
	int min_db_draw;

	/* Conversion data */
	int amp_offset_mdbm;
	int amp_res_mdbm;

	/* Graph elements we've calculated */
	int g_start_x, g_start_y, g_end_x, g_end_y,
		g_len_x, g_len_y;
	int dbm_w;
	double wbar;

	gint timeout_ref;

	GtkWidget *vbox, *hbox, *infoeb;
	GtkWidget *sweepinfo;
	GtkWidget *draw, *menubutton;

	spectool_sweep_cache *sweepcache;

	/* To be set by children to control behavior */
	int sweep_num_samples;
	int sweep_keep_avg;
	int sweep_keep_peak;
	int sweep_roll_peak;
	int sweep_num_aggregate;

	/* Callbacks used by open, sweep */
	void (* wdr_sweep_func)(int, int, spectool_sample_sweep *, void *);
	void (* wdr_devbind_func)(GtkWidget *, spectool_device_registry *, int);

	spectool_device_registry *wdr;
	int wdr_slot;
	spectool_phy *phydev;

	/* Graph title (INCLUDING formatting) */
	char *graph_title;

	/* Graph background colors */
	char *graph_title_bg, *graph_control_bg;

	/* Menu callback, allows for adding custom items to the widget dropdown
	 * arrow menu 
	 * arg1 - widget 
	 * arg2 - menu */
	void (* menu_func)(GtkWidget *, GtkWidget *);

	void (* help_func)(gpointer *);

	/* Offscreen surface for regular drawing to be copied back during an expose */
	cairo_surface_t *offscreen;
	int old_width, old_height;

	/* Callbacks for drawing */
	int draw_timeout;
	void (* draw_func)(GtkWidget *, cairo_t *, SpectoolWidget *);

	/* Callbacks on size change */
	void (* sizechange_func)(GtkWidget *, GtkAllocation *);

	/* Plot channels on the bottom */
	int show_channels;
	/* Show DBM */
	int show_dbm;
	/* Show DBM lines */
	int show_dbm_lines;

	/* Callbacks for mouse events */
	gint (* draw_mouse_click_func)(GtkWidget *, GdkEventButton *, gpointer *);
	gboolean (* draw_mouse_move_func)(GtkWidget *, GdkEventMotion *, gpointer *);

	/* Update function */
	void (* update_func)(GtkWidget *);

	SpectoolChannelOpts *chanopts;

	/* Have we gotten a sweep? */
	int dirty;
};

struct _SpectoolWidgetClass {
	GtkBinClass parent_class;
};

/* Controller item - didn't feel like making this a full widget, so you ask
 * the widget to make you a controller then embed the box prior to the widget */
typedef struct _SpectoolWidgetController {
	/* Main widget */
	GtkWidget *evbox;

	GtkWidget *label, *arrow, *menubutton;
	SpectoolWidget *wwidget;
} SpectoolWidgetController;

GType spectool_widget_get_type(void);
GtkWidget *spectool_widget_new(void);
void spectool_widget_bind_dev(GtkWidget *widget, spectool_device_registry *wdr,
						   int slot);

/* Do the heavy lifting of actually constructing the GUI, so that child
 * classes can trigger this after setting up titles, etc.  I'm sure this
 * isn't the most elegant method, someone can send me a patch */
void spectool_widget_buildgui(SpectoolWidget *widget);

/* Update the backing graphics */
void spectool_widget_graphics_update(SpectoolWidget *wwidget);

SpectoolWidgetController *spectool_widget_buildcontroller(GtkWidget *widget);

void spectool_widget_link_channel(GtkWidget *widget, SpectoolChannelOpts *opts);

/* Timeout function */
gint spectool_widget_timeout(gpointer *data);

/* Calculate the channel clicked in */
extern inline int spectool_widget_find_chan_pt(SpectoolWidget *wwidget, int x, int y);

void spectool_widget_context_channels(gpointer *aux);
void spectool_widget_context_dbm(gpointer *aux);
void spectool_widget_context_dbmlines(gpointer *aux);

/* Color space conversion tools */
extern inline void rgb_to_hsv(double r, double g, double b, 
					   double *h, double *s, double *v);
extern inline void hsv_to_rgb(double *r, double *g, double *b, 
					   double h, double s, double v);

G_END_DECLS

#endif
#endif

