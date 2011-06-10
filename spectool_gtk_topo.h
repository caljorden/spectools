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

#ifndef __SPECTOOL_TOPO_WIDGET_H__
#define __SPECTOOL_TOPO_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define SPECTOOL_TYPE_TOPO \
	(spectool_topo_get_type())
#define SPECTOOL_TOPO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	SPECTOOL_TYPE_TOPO, SpectoolTopo))
#define SPECTOOL_TOPO_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	SPECTOOL_TYPE_TOPO, SpectoolTopoClass))
#define IS_SPECTOOL_TOPO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	SPECTOOL_TYPE_TOPO))
#define IS_SPECTOOL_TOPO_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	SPECTOOL_TYPE_TOPO))

typedef struct _SpectoolTopo SpectoolTopo;
typedef struct _SpectoolTopoClass SpectoolTopoClass;

/* Access the color array */
#define SPECTOOL_TOPO_COLOR(a, b, c)	((a)[((b) * 3) + c])

struct _SpectoolTopo {
	SpectoolWidget parent;

	GtkWidget *sweepinfo;

	GtkWidget *legend_pix;
	GtkWidget *leg_min, *leg_max;

	float *colormap;
	int colormap_len;

	unsigned int *sample_counts;
	int sch, scw;
	int sweep_count_num, sweep_peak_max;
};

struct _SpectoolTopoClass {
	SpectoolWidgetClass parent_class;
};

GType spectool_topo_get_type(void);
GtkWidget *spectool_topo_new(void);
void spectool_topo_clear(void);

G_END_DECLS

#endif
#endif

