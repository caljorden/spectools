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

#ifndef __SPECTOOL_CHANNEL_WIDGET_H__
#define __SPECTOOL_CHANNEL_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define SPECTOOL_TYPE_CHANNEL \
	(spectool_channel_get_type())
#define SPECTOOL_CHANNEL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	SPECTOOL_TYPE_CHANNEL, SpectoolChannel))
#define SPECTOOL_CHANNEL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	SPECTOOL_TYPE_CHANNEL, SpectoolChannelClass))
#define IS_SPECTOOL_CHANNEL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	SPECTOOL_TYPE_CHANNEL))
#define IS_SPECTOOL_CHANNEL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	SPECTOOL_TYPE_CHANNEL))

typedef struct _SpectoolChannel SpectoolChannel;
typedef struct _SpectoolChannelClass SpectoolChannelClass;

#define SPECTOOL_CHANNEL_NUM_SAMPLES		0

struct _SpectoolChannel {
	SpectoolWidget parent;

	/* is the mouse down during an event? */
	int mouse_down;

	/* channel clickable rectangle */
	int chan_start_x, chan_start_y, chan_end_x, chan_end_y;
	GdkPoint *chan_points;
	int chan_h;

	GList *update_list;
};

struct _SpectoolChannelClass {
	SpectoolWidgetClass parent_class;
};

GType spectool_channel_get_type(void);
GtkWidget *spectool_channel_new();
void spectool_channel_append_update(GtkWidget *widget, GtkWidget *update);

G_END_DECLS

#endif
#endif


