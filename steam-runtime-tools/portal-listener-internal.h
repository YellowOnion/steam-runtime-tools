/*<private_header>*/
/*
 * Common code for portal-like services
 *
 * Copyright © 2018 Red Hat, Inc.
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on xdg-desktop-portal, flatpak-portal and flatpak-spawn.
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#pragma once

#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx.h"

typedef struct _SrtPortalListener SrtPortalListener;
typedef struct _SrtPortalListenerClass SrtPortalListenerClass;

#define _SRT_TYPE_PORTAL_LISTENER (_srt_portal_listener_get_type ())
#define _SRT_PORTAL_LISTENER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), _SRT_TYPE_PORTAL_LISTENER, SrtPortalListener))
#define _SRT_PORTAL_LISTENER_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), _SRT_TYPE_PORTAL_LISTENER, SrtPortalListenerClass))
#define _SRT_IS_PORTAL_LISTENER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), _SRT_TYPE_PORTAL_LISTENER))
#define _SRT_IS_PORTAL_LISTENER_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), _SRT_TYPE_PORTAL_LISTENER))
#define _SRT_PORTAL_LISTENER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), _SRT_TYPE_PORTAL_LISTENER, SrtPortalListenerClass)
GType _srt_portal_listener_get_type (void);

typedef enum
{
  SRT_PORTAL_LISTENER_FLAGS_PREFER_UNIQUE_NAME = (1 << 0),
  SRT_PORTAL_LISTENER_FLAGS_READY = (1 << 1),
  SRT_PORTAL_LISTENER_FLAGS_NONE = 0,
} SrtPortalListenerFlags;

typedef enum
{
  SRT_PORTAL_LISTENER_BUS_NAME_STATUS_WAITING = 0,
  SRT_PORTAL_LISTENER_BUS_NAME_STATUS_OWNED,
  SRT_PORTAL_LISTENER_BUS_NAME_STATUS_UNOWNED
} SrtPortalListenerBusNameStatus;

typedef struct
{
  gchar *name;
  guint name_owner_id;
  SrtPortalListenerBusNameStatus status;
} SrtPortalListenerBusName;

struct _SrtPortalListener
{
  GObject parent;
  GStrv original_environ;
  FILE *original_stdout;
  FILE *info_fh;
  GDBusConnection *session_bus;
  GDBusServer *server;
  gchar *original_cwd_l;
  gchar *server_socket;
  GArray *bus_names;
  SrtPortalListenerFlags flags;
};

SrtPortalListener *_srt_portal_listener_new (void);

gboolean _srt_portal_listener_set_up_info_fd (SrtPortalListener *self,
                                              int fd,
                                              GError **error);

gboolean _srt_portal_listener_check_socket_arguments (SrtPortalListener *self,
                                                      const char * const *opt_bus_names,
                                                      const char *opt_socket,
                                                      const char *opt_socket_directory,
                                                      GError **error);

gboolean _srt_portal_listener_listen (SrtPortalListener *self,
                                      const char * const *opt_bus_names,
                                      GBusNameOwnerFlags flags,
                                      const char *opt_socket,
                                      const char *opt_socket_directory,
                                      GError **error);

void _srt_portal_listener_close_info_fh (SrtPortalListener *self,
                                         gboolean success);
const char *_srt_portal_listener_get_suggested_bus_name (SrtPortalListener *self);

void _srt_portal_listener_stop_listening (SrtPortalListener *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtPortalListener, g_object_unref)
