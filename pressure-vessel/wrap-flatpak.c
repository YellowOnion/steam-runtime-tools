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
#define FLATPAK_SESSION_HELPER_BUS_NAME "org.freedesktop.Flatpak"

static gboolean
check_launch_on_host (const char *launch_executable,
                      GError **error)
{
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
  int wait_status;
  const char *test_argv[] =
  {
    NULL,
    "--bus-name=" FLATPAK_SESSION_HELPER_BUS_NAME,
    "--",
    "true",
    NULL
  };

  test_argv[0] = launch_executable;

  if (!g_spawn_sync (NULL,  /* cwd */
                     (gchar **) test_argv,
                     NULL,  /* environ */
                     G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                     NULL, NULL,    /* child setup */
                     &child_stdout,
                     &child_stderr,
                     &wait_status,
                     error))
    {
      return FALSE;
    }

  if (wait_status != 0)
    {
      pv_log_failure ("Cannot run commands on host system: wait status %d",
                 wait_status);

      if (child_stdout != NULL && child_stdout[0] != '\0')
        pv_log_failure ("Output:\n%s", child_stdout);

      if (child_stderr != NULL && child_stderr[0] != '\0')
        pv_log_failure ("Diagnostic output:\n%s", child_stderr);

      return glnx_throw (error, "Unable to run a command on the host system");
    }

  return TRUE;
}

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
 * @subsandbox_out: (out) (not optional) (nullable): Used to return
 *  an adverb command that will launch its arguments in a sub-sandbox,
 *  or %NULL if not using sub-sandboxing
 * @run_on_host_out: (out) (not optional) (nullable): Used to return
 *  an adverb command that will launch its arguments on the host system,
 *  or %NULL if not using sandbox escape
 *
 * Check that we are running under Flatpak and can launch the game somehow.
 * On success, exactly one of the `(out)` parameters will be set to non-%NULL.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_wrap_check_flatpak (const char *tools_dir,
                       FlatpakBwrap **subsandbox_out,
                       FlatpakBwrap **run_on_host_out,
                       GError **error)
{
  g_autoptr(FlatpakBwrap) subsandbox = NULL;
  g_autoptr(FlatpakBwrap) run_on_host = NULL;
  g_autoptr(GKeyFile) info = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *flatpak_version = NULL;
  g_autofree gchar *launch_executable = NULL;

  g_return_val_if_fail (tools_dir != NULL, FALSE);
  g_return_val_if_fail (subsandbox_out != NULL, FALSE);
  g_return_val_if_fail (*subsandbox_out == NULL, FALSE);
  g_return_val_if_fail (run_on_host_out != NULL, FALSE);
  g_return_val_if_fail (*run_on_host_out == NULL, FALSE);
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
        }
    }
  /* Deliberately not documented: only people who are in a position
   * to run their own modified versions of Flatpak and pressure-vessel
   * should be using this, and those people can find this in the
   * source code */
  else if (g_getenv ("PRESSURE_VESSEL_FLATPAK_PR4018") != NULL)
    {
      g_warning ("Assuming your version of Flatpak contains unmerged "
                 "changes (#4018, #4125, #4126, #4093)");
      subsandbox = get_subsandbox_adverb (launch_executable);
    }
  /* Also deliberately not documented */
  else if (g_getenv ("PRESSURE_VESSEL_FLATPAK_SANDBOX_ESCAPE") != NULL)
    {
      g_autofree gchar *policy = NULL;

      policy = g_key_file_get_string (info,
                                      FLATPAK_METADATA_GROUP_SESSION_BUS_POLICY,
                                      FLATPAK_SESSION_HELPER_BUS_NAME,
                                      NULL);

      if (g_strcmp0 (policy, "talk") != 0)
        return glnx_throw (error,
                           "PRESSURE_VESSEL_FLATPAK_SANDBOX_ESCAPE can "
                           "only be used if the Flatpak app has been "
                           "configured to allow escape from the sandbox");

      g_warning ("Running bwrap command on host via %s (experimental)",
                 FLATPAK_SESSION_HELPER_BUS_NAME);

      /* If we have permission to escape from the sandbox, we'll do that,
       * and launch bwrap that way */
      run_on_host = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      flatpak_bwrap_add_arg (run_on_host,
                             launch_executable);
      flatpak_bwrap_add_arg (run_on_host,
                             "--bus-name=" FLATPAK_SESSION_HELPER_BUS_NAME);

      /* If we can't launch a command on the host, just fail. */
      if (!check_launch_on_host (launch_executable, error))
        return FALSE;
    }
  else
    {
      return glnx_throw (error,
                         "pressure-vessel (SteamLinuxRuntime) cannot be run "
                         "in a Flatpak environment. For Proton 5.13+, "
                         "unofficial community builds that do not use "
                         "pressure-vessel are available.");
    }

  /* Exactly one of them is non-NULL on success */
  g_assert ((subsandbox != NULL) + (run_on_host != NULL) == 1);

  *subsandbox_out = g_steal_pointer (&subsandbox);
  *run_on_host_out = g_steal_pointer (&run_on_host);
  return TRUE;
}
