/*
 * Copyright © 2020-2021 Collabora Ltd.
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
 */

#include "wrap-flatpak.h"

#include "libglnx/libglnx.h"

#include "flatpak-run-private.h"
#include "utils.h"

#define FLATPAK_PORTAL_BUS_NAME "org.freedesktop.portal.Flatpak"

static FlatpakBwrap *
get_subsandbox_adverb (const char *launch_executable)
{
  FlatpakBwrap *ret = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  flatpak_bwrap_add_arg (ret, launch_executable);
  /* Tell pressure-vessel-launch to send its whole environment
   * to the subsandbox, except for the parts that we edit later.
   * This effectively matches bwrap's behaviour. */
  flatpak_bwrap_add_arg (ret, "--pass-env-matching=*");
  flatpak_bwrap_add_arg (ret, "--bus-name=" FLATPAK_PORTAL_BUS_NAME);

  return ret;
}

/*
 * pv_wrap_check_flatpak:
 * @tools_dir: Path to .../pressure-vessel/bin/
 * @subsandbox_out: (out) (not optional) (not nullable): Used to return
 *  an adverb command that will launch its arguments in a sub-sandbox
 *
 * Check that we are running under Flatpak and can launch the game somehow.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_wrap_check_flatpak (const char *tools_dir,
                       FlatpakBwrap **subsandbox_out,
                       GError **error)
{
  g_autoptr(FlatpakBwrap) subsandbox = NULL;
  g_autoptr(GKeyFile) info = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *flatpak_version = NULL;
  g_autofree gchar *launch_executable = NULL;

  g_return_val_if_fail (tools_dir != NULL, FALSE);
  g_return_val_if_fail (subsandbox_out != NULL, FALSE);
  g_return_val_if_fail (*subsandbox_out == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  info = g_key_file_new ();

  if (!g_key_file_load_from_file (info, "/.flatpak-info", G_KEY_FILE_NONE,
                                  &local_error))
    {
      g_warning ("Unable to load Flatpak instance info: %s",
                 local_error->message);
      g_clear_error (&local_error);
    }

  flatpak_version = g_key_file_get_string (info,
                                           FLATPAK_METADATA_GROUP_INSTANCE,
                                           FLATPAK_METADATA_KEY_FLATPAK_VERSION,
                                           NULL);

  if (flatpak_version == NULL)
    g_warning ("Running under Flatpak, unknown version");
  else
    g_info ("Running under Flatpak, version %s", flatpak_version);

  launch_executable = g_build_filename (tools_dir,
                                        "pressure-vessel-launch",
                                        NULL);

  if (flatpak_version != NULL && strverscmp (flatpak_version, "1.11.0") >= 0)
    {
      g_auto(GStrv) devices = NULL;
      g_auto(GStrv) features = NULL;
      g_auto(GStrv) filesystems = NULL;

      g_warning ("Using experimental Flatpak sub-sandboxing "
                 "(requires Flatpak 1.11.x commit 1.10.1-80-gcb47d83b "
                 "or later)");
      subsandbox = get_subsandbox_adverb (launch_executable);

      devices = g_key_file_get_string_list (info,
                                            FLATPAK_METADATA_GROUP_CONTEXT,
                                            FLATPAK_METADATA_KEY_DEVICES,
                                            NULL, NULL);
      features = g_key_file_get_string_list (info,
                                             FLATPAK_METADATA_GROUP_CONTEXT,
                                             FLATPAK_METADATA_KEY_FEATURES,
                                             NULL, NULL);
      filesystems = g_key_file_get_string_list (info,
                                                FLATPAK_METADATA_GROUP_CONTEXT,
                                                FLATPAK_METADATA_KEY_FILESYSTEMS,
                                                NULL, NULL);

      if (devices != NULL
          && g_strv_contains ((const char * const *) devices, "shm"))
        {
          g_debug ("OK: /dev/shm shared with host");
        }
      else if (features != NULL
               && g_strv_contains ((const char * const *) features,
                                   "per-app-dev-shm"))
        {
          g_debug ("OK: per-app-ID /dev/shm (flatpak#4214)");
        }
      else
        {
          g_warning ("/dev/shm not shared between app instances "
                     "(flatpak#4214). "
                     "The Steam Overlay will not work.");
          g_info ("Try this: "
                  "flatpak override --user --allow=per-app-dev-shm "
                  "com.valvesoftware.Steam");
        }
    }
  else
    {
      return glnx_throw (error,
                         "pressure-vessel (SteamLinuxRuntime) cannot be run "
                         "in a Flatpak environment. For Proton 5.13+, "
                         "unofficial community builds that do not use "
                         "pressure-vessel are available.");
    }

  g_assert (subsandbox != NULL);

  *subsandbox_out = g_steal_pointer (&subsandbox);
  return TRUE;
}
