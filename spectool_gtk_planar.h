/* Spectool signal graph
 *
 * GTK widget implementation
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

#ifndef __SPECTOOL_PLANAR_WIDGET_H__
#define __SPECTOOL_PLANAR_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define SPECTOOL_TYPE_PLANAR \
	(spectool_planar_get_type())
#define SPECTOOL_PLANAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	SPECTOOL_TYPE_PLANAR, SpectoolPlanar))
#define SPECTOOL_PLANAR_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	SPECTOOL_TYPE_PLANAR, SpectoolPlanarClass))
#define IS_SPECTOOL_PLANAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	SPECTOOL_TYPE_PLANAR))
#define IS_SPECTOOL_PLANAR_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	SPECTOOL_TYPE_PLANAR))

typedef struct _SpectoolPlanar SpectoolPlanar;
typedef struct _SpectoolPlanarClass SpectoolPlanarClass;

#define SPECTOOL_PLANAR_NUM_SAMPLES		250

typedef struct _spectool_planar_marker {
	double r, g, b;
	GdkPixbuf *pixbuf;
	int samp_num;
	int cur, avg, peak;
} spectool_planar_marker;

struct _SpectoolPlanar {
	SpectoolWidget parent;

	/* What components do we draw */
	int draw_markers;
	int draw_peak;
	int draw_avg;
	int draw_cur;

	/* is the mouse down during an event? */
	int mouse_down;

	GtkWidget *sweepinfo;

	GtkWidget *mkr_treeview;
	GtkListStore *mkr_treelist;
	GList *mkr_list;
	spectool_planar_marker *cur_mkr;
	GtkWidget *mkr_newbutton, *mkr_delbutton;
};

struct _SpectoolPlanarClass {
	SpectoolWidgetClass parent_class;
};

GType spectool_planar_get_type(void);
GtkWidget *spectool_planar_new();
void spectool_planar_clear(void);

G_END_DECLS

#endif
#endif


