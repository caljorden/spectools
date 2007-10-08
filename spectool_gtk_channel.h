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

#ifndef __WISPY_CHANNEL_WIDGET_H__
#define __WISPY_CHANNEL_WIDGET_H__

#ifdef HAVE_GTK

#include <glib.h>
#include <glib-object.h>

#include "spectool_container.h"
#include "spectool_gtk_hw_registry.h"
#include "spectool_gtk_widget.h"

G_BEGIN_DECLS

#define WISPY_TYPE_CHANNEL \
	(wispy_channel_get_type())
#define WISPY_CHANNEL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), \
	WISPY_TYPE_CHANNEL, WispyChannel))
#define WISPY_CHANNEL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	WISPY_TYPE_CHANNEL, WispyChannelClass))
#define IS_WISPY_CHANNEL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), \
	WISPY_TYPE_CHANNEL))
#define IS_WISPY_CHANNEL_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((class), \
	WISPY_TYPE_CHANNEL))

typedef struct _WispyChannel WispyChannel;
typedef struct _WispyChannelClass WispyChannelClass;

#define WISPY_CHANNEL_NUM_SAMPLES		0

struct _WispyChannel {
	WispyWidget parent;

	/* is the mouse down during an event? */
	int mouse_down;

	/* channel clickable rectangle */
	int chan_start_x, chan_start_y, chan_end_x, chan_end_y;
	GdkPoint *chan_points;
	int chan_h;

	GList *update_list;
};

struct _WispyChannelClass {
	WispyWidgetClass parent_class;
};

GType wispy_channel_get_type(void);
GtkWidget *wispy_channel_new();
void wispy_channel_append_update(GtkWidget *widget, GtkWidget *update);

G_END_DECLS

#endif
#endif


