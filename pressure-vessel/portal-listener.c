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

#include "config.h"
#include "subprojects/libglnx/config.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include "portal-listener.h"
#include "utils.h"

struct _PvPortalListenerClass
{
  GObjectClass parent;
};

G_DEFINE_TYPE (PvPortalListener, pv_portal_listener, G_TYPE_OBJECT)

void
pv_portal_listener_init (PvPortalListener *self)
{
  self->original_environ = g_get_environ ();
  pv_get_current_dirs (NULL, &self->original_cwd_l);
}

/*
 * Divert stdout to stderr, and set up the --info-fd to be the
 * original stdout or a specified fd.
 */
gboolean
pv_portal_listener_set_up_info_fd (PvPortalListener *self,
                                   int fd,
                                   GError **error)
{
  self->original_stdout = _srt_divert_stdout_to_stderr (error);

  if (self->original_stdout == NULL)
    return FALSE;

  if (fd > 0)   /* < 0 means unset, and 0 is stdout itself */
    {
      self->info_fh = fdopen (fd, "w");

      if (self->info_fh == NULL)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create a stdio wrapper for fd %d",
                                        fd);
    }
  else
    {
      self->info_fh = self->original_stdout;
    }

  return TRUE;
}

/*
 * If @bus_name is non-NULL, print it to the info fd. Then
 * close the --info-fd, and also close standard output (if different).
 */
void
pv_portal_listener_close_info_fh (PvPortalListener *self,
                                  const char *bus_name)
{
  if (self->info_fh != NULL)
    {
      if (bus_name != NULL)
        fprintf (self->info_fh, "bus_name=%s\n", bus_name);

      fflush (self->info_fh);
    }

  if (self->info_fh == self->original_stdout)
    self->original_stdout = NULL;
  else
    g_clear_pointer (&self->original_stdout, fclose);

  g_clear_pointer (&self->info_fh, fclose);
}

static void
pv_portal_listener_dispose (GObject *object)
{
  PvPortalListener *self = PV_PORTAL_LISTENER (object);

  pv_portal_listener_close_info_fh (self, NULL);

  G_OBJECT_CLASS (pv_portal_listener_parent_class)->dispose (object);
}

static void
pv_portal_listener_finalize (GObject *object)
{
  PvPortalListener *self = PV_PORTAL_LISTENER (object);

  g_clear_pointer (&self->original_environ, g_strfreev);
  g_clear_pointer (&self->original_cwd_l, g_free);

  G_OBJECT_CLASS (pv_portal_listener_parent_class)->finalize (object);
}

static void
pv_portal_listener_class_init (PvPortalListenerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->dispose = pv_portal_listener_dispose;
  object_class->finalize = pv_portal_listener_finalize;
}

PvPortalListener *
pv_portal_listener_new (void)
{
  return g_object_new (PV_TYPE_PORTAL_LISTENER,
                       NULL);
}
