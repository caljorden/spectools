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

#ifndef __SPECTOOL_SPECTRAL_WIDGET_H__
#define __SPECTOOL_SPECTRAL_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define SPECTOOL_TYPE_SPECTRAL \
	(spectool_spectral_get_type())
#define SPECTOOL_SPECTRAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	SPECTOOL_TYPE_SPECTRAL, SpectoolSpectral))
#define SPECTOOL_SPECTRAL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	SPECTOOL_TYPE_SPECTRAL, SpectoolSpectralClass))
#define IS_SPECTOOL_SPECTRAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	SPECTOOL_TYPE_SPECTRAL))
#define IS_SPECTOOL_SPECTRAL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	SPECTOOL_TYPE_SPECTRAL))

typedef struct _SpectoolSpectral SpectoolSpectral;
typedef struct _SpectoolSpectralClass SpectoolSpectralClass;

#define SPECTOOL_SPECTRAL_NUM_SAMPLES		100

/* Access the color array */
#define SPECTOOL_SPECTRAL_COLOR(a, b, c)	((a)[((b) * 3) + c])

struct _SpectoolSpectral {
	SpectoolWidget parent;

	GtkWidget *sweepinfo;

	GtkWidget *legend_pix;

	float *colormap;
	int colormap_len;

	GdkPixmap **line_cache;
	int line_cache_len;
	int n_sweeps_delta;

	int oldx, oldy;
};

struct _SpectoolSpectralClass {
	SpectoolWidgetClass parent_class;
};

GType spectool_spectral_get_type(void);
GtkWidget *spectool_spectral_new(void);
void spectool_spectral_clear(void);

G_END_DECLS

#endif
#endif

