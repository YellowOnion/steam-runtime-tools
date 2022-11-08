/*
 * Copyright © 2017 Red Hat, Inc
 * Copyright © 2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Florian Müllner <fmuellner@gnome.org>
 *       Matthias Clasen <mclasen@redhat.com>
 */

/*
 * Alternative executable to the canonical 'xdg-open' with a better handling
 * of Steam's URLs.
 * Loosely based on the xdg-open implementation of flatpak-xdg-utils.
 */

#include <libglnx.h>

#include "steam-runtime-tools/glib-backports-internal.h"

#include <glib.h>
#include <glib-object.h>
#include <gio/gunixfdlist.h>

#include <steam-runtime-tools/log-internal.h>
#include <steam-runtime-tools/utils-internal.h>

typedef GUnixFDList AutoUnixFDList;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoUnixFDList, g_object_unref)

static gchar **uris = NULL;
static gboolean opt_print_help = FALSE;
static gboolean opt_print_version = FALSE;

static const GOptionEntry option_entries[] =
{
  /* Imitate the allowed options of the real 'xdg-open' */
  { "manual", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &opt_print_help,
    NULL, NULL },
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY,
    &uris, NULL, NULL },
  { NULL }
};

static gboolean
open_with_portal (const char *uri_or_filename,
                  GError **error)
{
  g_autoptr(GDBusConnection) connection = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

  if (connection != NULL)
    {
      const gchar *portal_bus_name = "org.freedesktop.portal.Desktop";
      const gchar *portal_object_path = "/org/freedesktop/portal/desktop";
      const gchar *portal_iface_name = "org.freedesktop.portal.OpenURI";
      const gchar *method_name = NULL;
      GVariant *arguments = NULL;   /* floating */
      GVariantBuilder opt_builder;
      g_autoptr(AutoUnixFDList) fd_list = NULL;
      g_autoptr(GFile) file = NULL;
      g_autoptr(GVariant) result = NULL;

      g_debug ("Trying the D-Bus desktop portal");

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      file = g_file_new_for_commandline_arg (uri_or_filename);

      if (g_file_is_native (file))
        {
          /* The canonical 'xdg-open' also handles paths. We try to replicate
           * that too, but it might not always work because the container
           * inside and outside filesystem structure might be different. */
          g_autofree gchar *path = NULL;
          int fd;

          path = g_file_get_path (file);
          fd = open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
          if (fd < 0)
            {
              return glnx_throw_errno_prefix (error, "Failed to open '%s'", path);
            }

          fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */

          arguments = g_variant_new ("(sh@a{sv})",
                                     "", 0,
                                     g_variant_builder_end (&opt_builder));
          method_name = "OpenFile";
        }
      else
        {
          arguments = g_variant_new ("(ss@a{sv})",
                                     "", uri_or_filename,
                                     g_variant_builder_end (&opt_builder));
          method_name = "OpenURI";
        }

      result = g_dbus_connection_call_with_unix_fd_list_sync (connection,
                                                              portal_bus_name,
                                                              portal_object_path,
                                                              portal_iface_name,
                                                              method_name,
                                                              /* sinks floating reference */
                                                              g_steal_pointer (&arguments),
                                                              NULL,
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              fd_list,
                                                              NULL,
                                                              NULL,
                                                              error);

      if (result == NULL)
        return glnx_prefix_error (error,
                                  "Unable to open URL with xdg-desktop-portal");
      else
        return TRUE;
    }
  else
    {
      return glnx_prefix_error (error,
                                "Unable to connect to D-Bus session bus");
    }
}

int
main (int argc,
      char **argv)
{
  const gchar *uri;
  g_autofree gchar *scheme = NULL;
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(GError) pipe_error = NULL;
  g_autoptr(GError) portal_error = NULL;
  g_autoptr(GError) error = NULL;
  gboolean prefer_steam;

  _srt_util_set_glib_log_handler ("steam-runtime-urlopen",
                                  G_LOG_DOMAIN,
                                  SRT_LOG_FLAGS_OPTIONALLY_JOURNAL,
                                  NULL, NULL, NULL);

  option_context = g_option_context_new ("{ file | URL }");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      g_printerr ("%s: %s\n", g_get_prgname (), error->message);
      return 1;
    }

  if (opt_print_version)
    {
      /* Simply print the version number, similarly to the real xdg-open */
      g_print ("%s\n", VERSION);
      return EXIT_SUCCESS;
    }

  if (uris == NULL || g_strv_length (uris) > 1)
    {
      g_autofree gchar *help = g_option_context_get_help (option_context, TRUE, NULL);
      g_print ("%s\n", help);
      return 1;
    }

  /* In reality this could also be a path, but we call it "uri" for simplicity */
  uri = uris[0];

  scheme = g_uri_parse_scheme (uri);

  /* For steam: and steamlink: URLs, we never want to go via
   * xdg-desktop-portal and the desktop environment's URL-handling
   * machinery, because there's a chance that they will choose the wrong
   * copy of Steam, for example if we have both native and Flatpak versions
   * of Steam installed. We want to use whichever one is actually running,
   * via the ~/.steam/steam.pipe in the current execution environment. */
  if (scheme != NULL && (g_ascii_strcasecmp (scheme, "steamlink") == 0
                         || g_ascii_strcasecmp (scheme, "steam") == 0))
    {
      g_debug ("Passing the URL '%s' to the Steam pipe", uri);
      if (_srt_steam_command_via_pipe (&uri, 1, &pipe_error))
        return EXIT_SUCCESS;
      else
        goto fail;
    }

  prefer_steam = _srt_boolean_environment ("SRT_URLOPEN_PREFER_STEAM", FALSE);

  if (!prefer_steam && open_with_portal (uri, &portal_error))
    return EXIT_SUCCESS;

  if (scheme != NULL && (g_ascii_strcasecmp (scheme, "http") == 0
                         || g_ascii_strcasecmp (scheme, "https") == 0))
    {
      g_autofree gchar *steam_url = NULL;
      steam_url = g_strjoin ("/", "steam://openurl", uri, NULL);

      g_debug ("Passing the URL '%s' to the Steam pipe", steam_url);
      if (_srt_steam_command_via_pipe ((const gchar **) &steam_url, 1, &pipe_error))
        return EXIT_SUCCESS;
    }

  /* If we haven't tried xdg-desktop-portal yet because we were hoping
   * to go via Steam, try it now - going by the less-preferred route is
   * better than nothing, and in particular we can't go via Steam for
   * non-web URLs like mailto: */
  if (portal_error == NULL && open_with_portal (uri, &portal_error))
    return EXIT_SUCCESS;

fail:
  g_printerr ("%s: Unable to open URL\n", g_get_prgname ());

  if (pipe_error != NULL)
    g_printerr ("%s: tried using steam.pipe, received error: %s\n",
                g_get_prgname (), pipe_error->message);

  if (portal_error != NULL)
    g_printerr ("%s: tried using xdg-desktop-portal, received error: %s\n",
                g_get_prgname (), portal_error->message);

  return 4;
}
