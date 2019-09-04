/*
 * Copyright © 2017-2019 Collabora Ltd.
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
 */

#include "config.h"
#include "subprojects/libglnx/config.h"

#include <gio/gio.h>

#include "bwrap.h"

/**
 * pv_bwrap_run_sync:
 * @bwrap: A #FlatpakBwrap on which flatpak_bwrap_finish() has been called
 * @error: Used to raise an error on failure
 *
 * Try to run a command. Its standard output and standard error go to
 * pressure-vessel's own stdout and stderr.
 *
 * Returns: %TRUE if the subprocess runs to completion
 */
gboolean
pv_bwrap_run_sync (FlatpakBwrap *bwrap,
                   GError **error)
{
  gint exit_status;
  g_autofree gchar *output = NULL;
  g_autofree gchar *errors = NULL;
  guint i;
  g_autoptr(GString) command = g_string_new ("");
  void (*child_setup) (gpointer) = NULL;
  gpointer child_setup_data = NULL;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (bwrap->argv->len >= 2, FALSE);
  g_return_val_if_fail (pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  for (i = 0; i < bwrap->argv->len; i++)
    {
      g_autofree gchar *quoted = NULL;
      char *unquoted = g_ptr_array_index (bwrap->argv, i);

      if (unquoted == NULL)
        break;

      quoted = g_shell_quote (unquoted);
      g_string_append_printf (command, " %s", quoted);
    }

  if (bwrap->fds != NULL && bwrap->fds->len > 0)
    {
      child_setup = flatpak_bwrap_child_setup_cb;
      child_setup_data = bwrap->fds;
    }

  g_debug ("run:%s", command->str);

  if (!g_spawn_sync (NULL,  /* cwd */
                     (char **) bwrap->argv->pdata,
                     bwrap->envp,
                     G_SPAWN_SEARCH_PATH,
                     child_setup, child_setup_data,
                     &output,
                     &errors,
                     &exit_status,
                     error))
    return FALSE;

  g_print ("%s", output);
  g_printerr ("%s", errors);

  if (!g_spawn_check_exit_status (exit_status, error))
    return FALSE;

  return TRUE;
}

/**
 * pv_bwrap_execve:
 * @bwrap: A #FlatpakBwrap on which flatpak_bwrap_finish() has been called
 * @error: Used to raise an error on failure
 *
 * Attempt to replace the current process with the given bwrap command.
 * If unable to do so, raise an error.
 *
 * Returns: %FALSE
 */
gboolean
pv_bwrap_execve (FlatpakBwrap *bwrap,
                 GError **error)
{
  int saved_errno;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (bwrap->argv->len >= 2, FALSE);
  g_return_val_if_fail (pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_debug ("Replacing self with bwrap...");

  if (bwrap->fds != NULL && bwrap->fds->len > 0)
    flatpak_bwrap_child_setup_cb (bwrap->fds);

  execve (bwrap->argv->pdata[0],
          (char * const *) bwrap->argv->pdata,
          bwrap->envp);
  saved_errno = errno;

  /* If we are still here then execve failed */
  g_set_error (error,
               G_IO_ERROR,
               g_io_error_from_errno (saved_errno),
               "Error replacing self with bwrap: %s",
               g_strerror (saved_errno));
  return FALSE;
}

/**
 * pv_bwrap_bind_usr:
 * @bwrap: The #FlatpakBwrap
 * @host_path: Absolute path to a directory in the host filesystem
 * @mount_point: Absolute path of the location at which to mount @host_path
 *  in the container
 * @error: Used to raise an error on failure
 *
 * Append arguments to @bwrap that will bind-mount `/usr` and associated
 * directories from @host_path into @mount_point.
 *
 * If @host_path contains a `usr` directory, it is assumed to be a
 * system root. Its `usr` directory is mounted on `${mount_point}/usr`
 * in the container. Its `lib*`, `bin` and `sbin` directories are
 * created as symbolic links in @mount_point, or mounted on subdirectories
 * of @mount_point, as appropriate.
 *
 * If @host_path does not contain a `usr` directory, it is assumed to be
 * a merged `/usr`. It is mounted on `${mount_point}/usr`, and `lib*`,
 * `bin` and `sbin` symbolic links are created in @mount_point.
 *
 * In either case, if @host_path` contains `etc/alternatives` and/or
 * `etc/ld.so.cache`, they are mounted on corresponding paths under
 * @mount_point.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_bwrap_bind_usr (FlatpakBwrap *bwrap,
                   const char *host_path,
                   const char *mount_point,
                   GError **error)
{
  g_autofree gchar *usr = NULL;
  g_autofree gchar *dest = NULL;
  gboolean host_path_is_usr = FALSE;
  g_autoptr(GDir) dir = NULL;
  const gchar *member = NULL;
  static const char * const bind_etc[] =
  {
    "alternatives",
    "ld.so.cache"
  };
  gsize i;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (host_path != NULL, FALSE);
  g_return_val_if_fail (host_path[0] == '/', FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);
  g_return_val_if_fail (mount_point[0] == '/', FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  usr = g_build_filename (host_path, "usr", NULL);
  dest = g_build_filename (mount_point, "usr", NULL);

  if (g_file_test (usr, G_FILE_TEST_IS_DIR))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", usr, dest,
                              NULL);
    }
  else
    {
      /* host_path is assumed to be a merged /usr */
      host_path_is_usr = TRUE;
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", host_path, dest,
                              NULL);
    }

  g_clear_pointer (&dest, g_free);

  dir = g_dir_open (host_path, 0, error);

  if (dir == NULL)
    return FALSE;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
      if (g_str_has_prefix (member, "lib")
          || g_str_equal (member, "bin")
          || g_str_equal (member, "sbin"))
        {
          dest = g_build_filename (mount_point, member, NULL);

          if (host_path_is_usr)
            {
              g_autofree gchar *target = g_build_filename ("usr",
                                                           member, NULL);

              flatpak_bwrap_add_args (bwrap,
                                      "--symlink", target, dest,
                                      NULL);
            }
          else
            {
              g_autofree gchar *path = g_build_filename (host_path,
                                                         member, NULL);
              g_autofree gchar *target = glnx_readlinkat_malloc (-1, path, NULL, NULL);

              if (target != NULL)
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--symlink", target, dest,
                                          NULL);
                }
              else
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", path, dest,
                                          NULL);
                }
            }

          g_clear_pointer (&dest, g_free);
        }
    }

  for (i = 0; i < G_N_ELEMENTS (bind_etc); i++)
    {
      g_autofree gchar *path = g_build_filename (host_path, "etc",
                                                 bind_etc[i], NULL);

      if (g_file_test (path, G_FILE_TEST_EXISTS))
        {
          dest = g_build_filename (mount_point, "etc", bind_etc[i], NULL);

          flatpak_bwrap_add_args (bwrap,
                                  "--ro-bind", path, dest,
                                  NULL);

          g_clear_pointer (&dest, g_free);
        }
    }

  return TRUE;
}
