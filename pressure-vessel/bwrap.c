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

#include <ftw.h>

#include <gio/gio.h>

#include "bwrap.h"
#include "utils.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

/**
 * pv_bwrap_run_sync:
 * @bwrap: A #FlatpakBwrap on which flatpak_bwrap_finish() has been called
 * @exit_status_out: (out) (optional): Used to return the exit status,
 *  or -1 if it could not be launched or was killed by a signal
 * @error: Used to raise an error on failure
 *
 * Try to run a command. It inherits pressure-vessel's own file
 * descriptors.
 *
 * Returns: %TRUE if the subprocess runs to completion
 */
gboolean
pv_bwrap_run_sync (FlatpakBwrap *bwrap,
                   int *exit_status_out,
                   GError **error)
{
  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (bwrap->argv->len >= 2, FALSE);
  g_return_val_if_fail (pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return pv_run_sync ((const char * const *) bwrap->argv->pdata,
                      (const char * const *) bwrap->envp,
                      exit_status_out, NULL, error);
}

/**
 * pv_bwrap_execve:
 * @bwrap: A #FlatpakBwrap on which flatpak_bwrap_finish() has been called
 * @original_stdout: If > 0, `dup2()` this file descriptor onto stdout
 * @error: Used to raise an error on failure
 *
 * Attempt to replace the current process with the given bwrap command.
 * If unable to do so, raise an error.
 *
 * Returns: %FALSE
 */
gboolean
pv_bwrap_execve (FlatpakBwrap *bwrap,
                 int original_stdout,
                 GError **error)
{
  int saved_errno;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (bwrap->argv->len >= 2, FALSE);
  g_return_val_if_fail (pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_debug ("Replacing self with %s...",
           glnx_basename (g_ptr_array_index (bwrap->argv, 0)));

  if (bwrap->fds != NULL && bwrap->fds->len > 0)
    flatpak_bwrap_child_setup_cb (bwrap->fds);

  fflush (stdout);
  fflush (stderr);

  if (original_stdout > 0 &&
      dup2 (original_stdout, STDOUT_FILENO) != STDOUT_FILENO)
    return glnx_throw_errno_prefix (error,
                                    "Unable to make fd %d a copy of fd %d",
                                    STDOUT_FILENO, original_stdout);

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
 * @provider_in_host_namespace: The directory we will
 *  use to provide the container's `/usr`, `/lib*` etc.,
 *  in the form of an absolute path that can be resolved
 *  on the host system
 * @provider_in_current_namespace: The same directory, this time
 *  in the form of an absolute path that can be resolved
 *  in the namespace where pressure-vessel-wrap is running.
 *  For example, if we run in a Flatpak container and we intend
 *  to mount the host system's `/usr`, then
 *  @provider_in_host_namespace would be `/`
 *  but @provider_in_current_namespace would be `/run/host`.
 * @provider_in_container_namespace: Absolute path of the location
 *  at which we will mount @provider_in_host_namespace in the
 *  final container
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
 * To make this useful, the caller will probably also have to bind-mount
 * `etc`, or at least `etc/alternatives` and `etc/ld.so.cache`. However,
 * these are not handled here.
 *
 * Returns: %TRUE on success
 */
gboolean
pv_bwrap_bind_usr (FlatpakBwrap *bwrap,
                   const char *provider_in_host_namespace,
                   const char *provider_in_current_namespace,
                   const char *provider_in_container_namespace,
                   GError **error)
{
  g_autofree gchar *usr = NULL;
  g_autofree gchar *usr_in_current_namespace = NULL;
  g_autofree gchar *dest = NULL;
  gboolean host_path_is_usr = FALSE;
  g_autoptr(GDir) dir = NULL;
  const gchar *member = NULL;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (provider_in_host_namespace != NULL, FALSE);
  g_return_val_if_fail (provider_in_host_namespace[0] == '/', FALSE);
  g_return_val_if_fail (provider_in_current_namespace != NULL, FALSE);
  g_return_val_if_fail (provider_in_current_namespace[0] == '/', FALSE);
  g_return_val_if_fail (provider_in_container_namespace != NULL, FALSE);
  g_return_val_if_fail (provider_in_container_namespace[0] == '/', FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  usr = g_build_filename (provider_in_host_namespace, "usr", NULL);
  usr_in_current_namespace = g_build_filename (provider_in_current_namespace,
                                               "usr", NULL);
  dest = g_build_filename (provider_in_container_namespace, "usr", NULL);

  if (g_file_test (usr_in_current_namespace, G_FILE_TEST_IS_DIR))
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
                              "--ro-bind", provider_in_host_namespace, dest,
                              NULL);
    }

  g_clear_pointer (&dest, g_free);

  dir = g_dir_open (provider_in_current_namespace, 0, error);

  if (dir == NULL)
    return FALSE;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
      if ((g_str_has_prefix (member, "lib")
           && !g_str_equal (member, "libexec"))
          || g_str_equal (member, "bin")
          || g_str_equal (member, "sbin")
          || g_str_equal (member, ".ref"))
        {
          dest = g_build_filename (provider_in_container_namespace, member, NULL);

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
              g_autofree gchar *path_in_host = g_build_filename (provider_in_host_namespace,
                                                                 member, NULL);
              g_autofree gchar *path_in_current_namespace =
                g_build_filename (provider_in_current_namespace, member, NULL);

              g_autofree gchar *target = glnx_readlinkat_malloc (-1,
                                                                 path_in_current_namespace,
                                                                 NULL, NULL);

              if (target != NULL)
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--symlink", target, dest,
                                          NULL);
                }
              else
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", path_in_host, dest,
                                          NULL);
                }
            }

          g_clear_pointer (&dest, g_free);
        }
    }

  return TRUE;
}

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable :-( */
static struct
{
  FlatpakBwrap *bwrap;
  const char *source;
  const char *dest;
} nftw_data;

static int
copy_tree_helper (const char *fpath,
                  const struct stat *sb,
                  int typeflag,
                  struct FTW *ftwbuf)
{
  const char *path_in_container;
  g_autofree gchar *target = NULL;
  gsize prefix_len;
  int fd = -1;
  GError *error = NULL;

  g_return_val_if_fail (g_str_has_prefix (fpath, nftw_data.source), 1);
  g_return_val_if_fail (g_str_has_suffix (nftw_data.source, nftw_data.dest), 1);

  prefix_len = strlen (nftw_data.source) - strlen (nftw_data.dest);
  path_in_container = fpath + prefix_len;

  switch (typeflag)
    {
      case FTW_D:
        flatpak_bwrap_add_args (nftw_data.bwrap,
                                "--dir", path_in_container,
                                NULL);
        break;

      case FTW_SL:
        target = glnx_readlinkat_malloc (-1, fpath, NULL, NULL);
        flatpak_bwrap_add_args (nftw_data.bwrap,
                                "--symlink", target, path_in_container,
                                NULL);
        break;

      case FTW_F:
        if (!glnx_openat_rdonly (AT_FDCWD, fpath, FALSE, &fd, &error))
          {
            g_warning ("Unable to copy file into container: %s",
                       error->message);
            g_clear_error (&error);
          }

        flatpak_bwrap_add_args_data_fd (nftw_data.bwrap,
                                        "--ro-bind-data", glnx_steal_fd (&fd),
                                        path_in_container);
        break;

      default:
        g_warning ("Don't know how to handle ftw type flag %d at %s",
                   typeflag, fpath);
    }

  return 0;
}

/**
 * pv_bwrap_copy_tree:
 * @bwrap: The #FlatpakBwrap
 * @source: A copy of the desired @dest in a temporary directory,
 *  for example `/tmp/tmp12345678/overrides/lib`. The path must end
 *  with @dest.
 * @dest: The destination path in the container, which must be absolute.
 *
 * For every file, directory or symbolic link in @source, add a
 * corresponding read-only file, directory or symbolic link via the bwrap
 * command-line, so that the files, directories and symbolic links in the
 * container will persist even after @source has been deleted.
 */
void
pv_bwrap_copy_tree (FlatpakBwrap *bwrap,
                    const char *source,
                    const char *dest)
{
  g_return_if_fail (nftw_data.bwrap == NULL);
  g_return_if_fail (dest[0] == '/');
  g_return_if_fail (g_str_has_suffix (source, dest));

  nftw_data.bwrap = bwrap;
  nftw_data.source = source;
  nftw_data.dest = dest;
  nftw (source, copy_tree_helper, 100, FTW_PHYS);
  nftw_data.bwrap = NULL;
  nftw_data.source = NULL;
  nftw_data.dest = NULL;
}

/**
 * pv_bwrap_add_api_filesystems:
 * @bwrap: The #FlatpakBwrap
 *
 * Make basic API filesystems available.
 */
void
pv_bwrap_add_api_filesystems (FlatpakBwrap *bwrap)
{
  flatpak_bwrap_add_args (bwrap,
                          "--dev-bind", "/dev", "/dev",
                          "--proc", "/proc",
                          "--ro-bind", "/sys", "/sys",
                          NULL);
}

FlatpakBwrap *
pv_bwrap_copy (FlatpakBwrap *bwrap)
{
  FlatpakBwrap *ret;

  g_return_val_if_fail (bwrap != NULL, NULL);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), NULL);
  /* bwrap can't own any fds, because if it did,
   * flatpak_bwrap_append_bwrap() would steal them. */
  g_return_val_if_fail (bwrap->fds == NULL || bwrap->fds->len == 0, NULL);

  ret = flatpak_bwrap_new (flatpak_bwrap_empty_env);
  flatpak_bwrap_append_bwrap (ret, bwrap);
  return ret;
}

/*
 * Return @bwrap's @envp, while resetting @bwrap's @envp to an empty
 * environment block.
 */
GStrv
pv_bwrap_steal_envp (FlatpakBwrap *bwrap)
{
  GStrv envp = g_steal_pointer (&bwrap->envp);

  bwrap->envp = g_strdupv (flatpak_bwrap_empty_env);
  return envp;
}
