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

#ifndef __WISPY_SPECTRAL_WIDGET_H__
#define __WISPY_SPECTRAL_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define WISPY_TYPE_SPECTRAL \
	(wispy_spectral_get_type())
#define WISPY_SPECTRAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	WISPY_TYPE_SPECTRAL, WispySpectral))
#define WISPY_SPECTRAL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	WISPY_TYPE_SPECTRAL, WispySpectralClass))
#define IS_WISPY_SPECTRAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	WISPY_TYPE_SPECTRAL))
#define IS_WISPY_SPECTRAL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	WISPY_TYPE_SPECTRAL))

typedef struct _WispySpectral WispySpectral;
typedef struct _WispySpectralClass WispySpectralClass;

#define WISPY_SPECTRAL_NUM_SAMPLES		100

/* Access the color array */
#define WISPY_SPECTRAL_COLOR(a, b, c)	((a)[((b) * 3) + c])

struct _WispySpectral {
	WispyWidget parent;

	GtkWidget *sweepinfo;

	GtkWidget *legend_pix;

	float *colormap;
	int colormap_len;

	GdkPixmap **line_cache;
	int line_cache_len;
	int n_sweeps_delta;

	int oldx, oldy;
};

struct _WispySpectralClass {
	WispyWidgetClass parent_class;
};

GType wispy_spectral_get_type(void);
GtkWidget *wispy_spectral_new(void);
void wispy_spectral_clear(void);

G_END_DECLS

#endif
#endif

