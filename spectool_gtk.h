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

#include "config.h"

#ifndef __WISPY_GTK_H__
#define __WISPY_GTK_H__

#ifdef HAVE_GTK

#include <spectool_container.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <libintl.h>

void Wispy_Alert_Dialog(char *text);

void Wispy_Help_Dialog(char *title, char *text);

#endif
#endif

