/* Wispy signal graph
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

#ifndef __WISPY_PLANAR_WIDGET_H__
#define __WISPY_PLANAR_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define WISPY_TYPE_PLANAR \
	(wispy_planar_get_type())
#define WISPY_PLANAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	WISPY_TYPE_PLANAR, WispyPlanar))
#define WISPY_PLANAR_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	WISPY_TYPE_PLANAR, WispyPlanarClass))
#define IS_WISPY_PLANAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	WISPY_TYPE_PLANAR))
#define IS_WISPY_PLANAR_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	WISPY_TYPE_PLANAR))

typedef struct _WispyPlanar WispyPlanar;
typedef struct _WispyPlanarClass WispyPlanarClass;

#define WISPY_PLANAR_NUM_SAMPLES		250

typedef struct _wispy_planar_marker {
	double r, g, b;
	GdkPixbuf *pixbuf;
	int samp_num;
	int cur, avg, peak;
} wispy_planar_marker;

struct _WispyPlanar {
	WispyWidget parent;

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
	wispy_planar_marker *cur_mkr;
	GtkWidget *mkr_newbutton, *mkr_delbutton;
};

struct _WispyPlanarClass {
	WispyWidgetClass parent_class;
};

GType wispy_planar_get_type(void);
GtkWidget *wispy_planar_new();
void wispy_planar_clear(void);

G_END_DECLS

#endif
#endif


