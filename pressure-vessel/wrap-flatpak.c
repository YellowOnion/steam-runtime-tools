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
    "--bus-name=org.freedesktop.Flatpak",
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

  /* Deliberately not documented: only people who are in a position
   * to run their own modified versions of Flatpak and pressure-vessel
   * should be using this, and those people can find this in the
   * source code */
  if (g_getenv ("PRESSURE_VESSEL_FLATPAK_PR4018") != NULL)
    {
      g_warning ("Assuming your version of Flatpak contains unmerged "
                 "changes (#4018, #4125, #4126, #4093)");
      /* Use a sub-sandbox */
      subsandbox = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      flatpak_bwrap_add_arg (subsandbox, launch_executable);
      /* Tell pressure-vessel-launch to send its whole environment
       * to the subsandbox, except for the parts that we edit later.
       * This effectively matches bwrap's behaviour. */
      flatpak_bwrap_add_arg (subsandbox, "--pass-env-matching=*");
      flatpak_bwrap_add_arg (subsandbox,
                             "--bus-name=org.freedesktop.portal.Flatpak");
    }
  /* Also deliberately not documented */
  else if (g_getenv ("PRESSURE_VESSEL_FLATPAK_SANDBOX_ESCAPE") != NULL)
    {
      g_warning ("Assuming permissions have been set to allow Steam "
                 "to escape from the Flatpak sandbox");

      /* If we have permission to escape from the sandbox, we'll do that,
       * and launch bwrap that way */
      run_on_host = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      flatpak_bwrap_add_arg (run_on_host,
                             launch_executable);
      flatpak_bwrap_add_arg (run_on_host,
                             "--bus-name=org.freedesktop.Flatpak");

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
