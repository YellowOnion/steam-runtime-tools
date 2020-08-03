/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2019 Collabora Ltd.
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

#include "utils.h"

#include <ftw.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libglnx/libglnx.h"

#include "glib-backports.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

static void
child_setup_cb (gpointer user_data)
{
  flatpak_close_fds_workaround (3);
}

/**
 * pv_avoid_gvfs:
 *
 * Disable gvfs. This function must be called from main() before
 * starting any threads.
 */
void
pv_avoid_gvfs (void)
{
  g_autofree gchar *old_env = NULL;

  /* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
  old_env = g_strdup (g_getenv ("GIO_USE_VFS"));
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  g_vfs_get_default ();
  if (old_env)
    g_setenv ("GIO_USE_VFS", old_env, TRUE);
  else
    g_unsetenv ("GIO_USE_VFS");
}

/**
 * pv_envp_cmp:
 * @p1: a `const char * const *`
 * @p2: a `const char * const *`
 *
 * Compare two environment variables, given as pointers to pointers
 * to the actual `KEY=value` string.
 *
 * In particular this is suitable for sorting a #GStrv using `qsort`.
 *
 * Returns: negative, 0 or positive if `*p1` compares before, equal to
 *  or after `*p2`
 */
int
pv_envp_cmp (const void *p1,
             const void *p2)
{
  const char * const * s1 = p1;
  const char * const * s2 = p2;
  size_t l1 = strlen (*s1);
  size_t l2 = strlen (*s2);
  size_t min;
  const char *tmp;
  int ret;

  tmp = strchr (*s1, '=');

  if (tmp != NULL)
    l1 = tmp - *s1;

  tmp = strchr (*s2, '=');

  if (tmp != NULL)
    l2 = tmp - *s2;

  min = MIN (l1, l2);
  ret = strncmp (*s1, *s2, min);

  /* If they differ before the first '=' (if any) in either s1 or s2,
   * then they are certainly different */
  if (ret != 0)
    return ret;

  ret = strcmp (*s1, *s2);

  /* If they do not differ at all, then they are equal */
  if (ret == 0)
    return ret;

  /* FOO < FOO=..., and FOO < FOOBAR */
  if ((*s1)[min] == '\0')
    return -1;

  /* FOO=... > FOO, and FOOBAR > FOO */
  if ((*s2)[min] == '\0')
    return 1;

  /* FOO= < FOOBAR */
  if ((*s1)[min] == '=' && (*s2)[min] != '=')
    return -1;

  /* FOOBAR > FOO= */
  if ((*s2)[min] == '=' && (*s1)[min] != '=')
    return 1;

  /* Fall back to plain string comparison */
  return ret;
}

/**
 * pv_get_current_dirs:
 * @cwd_p: (out) (transfer full) (optional): Used to return the
 *  current physical working directory, equivalent to `$(pwd -P)`
 *  in a shell
 * @cwd_l: (out) (transfer full) (optional): Used to return the
 *  current logical working directory, equivalent to `$(pwd -L)`
 *  in a shell
 *
 * Return the physical and/or logical working directory.
 *
 * Equivalent to `$(pwd -L)` in a shell.
 */
void
pv_get_current_dirs (gchar **cwd_p,
                     gchar **cwd_l)
{
  g_autofree gchar *cwd = NULL;
  const gchar *pwd;

  g_return_if_fail (cwd_p == NULL || *cwd_p == NULL);
  g_return_if_fail (cwd_l == NULL || *cwd_l == NULL);

  cwd = g_get_current_dir ();

  if (cwd_p != NULL)
    *cwd_p = flatpak_canonicalize_filename (cwd);

  if (cwd_l != NULL)
    {
      pwd = g_getenv ("PWD");

      if (pwd != NULL && pv_is_same_file (pwd, cwd))
        *cwd_l = g_strdup (pwd);
      else
        *cwd_l = g_strdup (cwd);
    }
}

/**
 * pv_is_same_file:
 * @a: a path
 * @b: a path
 *
 * Returns: %TRUE if a and b are names for the same inode.
 */
gboolean
pv_is_same_file (const gchar *a,
                 const gchar *b)
{
  GStatBuf a_buffer, b_buffer;

  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (b != NULL, FALSE);

  if (strcmp (a, b) == 0)
    return TRUE;

  return (stat (a, &a_buffer) == 0
          && stat (b, &b_buffer) == 0
          && a_buffer.st_dev == b_buffer.st_dev
          && a_buffer.st_ino == b_buffer.st_ino);
}

void
pv_search_path_append (GString *search_path,
                       const gchar *item)
{
  g_return_if_fail (search_path != NULL);

  if (item == NULL || item[0] == '\0')
    return;

  if (search_path->len != 0)
    g_string_append (search_path, ":");

  g_string_append (search_path, item);
}

gchar *
pv_capture_output (const char * const * argv,
                   GError **error)
{
  gsize len;
  gint wait_status;
  g_autofree gchar *output = NULL;
  g_autofree gchar *errors = NULL;
  gsize i;
  g_autoptr(GString) command = g_string_new ("");

  g_return_val_if_fail (argv != NULL, NULL);
  g_return_val_if_fail (argv[0] != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  for (i = 0; argv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = g_shell_quote (argv[i]);

      g_string_append_printf (command, " %s", quoted);
    }

  g_debug ("run:%s", command->str);

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * see flatpak_close_fds_workaround */
  if (!g_spawn_sync (NULL,  /* cwd */
                     (char **) argv,
                     NULL,  /* env */
                     (G_SPAWN_SEARCH_PATH |
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN),
                     child_setup_cb, NULL,
                     &output,
                     &errors,
                     &wait_status,
                     error))
    return NULL;

  g_printerr ("%s", errors);

  if (!g_spawn_check_exit_status (wait_status, error))
    return NULL;

  len = strlen (output);

  /* Emulate shell $() */
  if (len > 0 && output[len - 1] == '\n')
    output[len - 1] = '\0';

  g_debug ("-> %s", output);

  return g_steal_pointer (&output);
}

/*
 * Returns: (transfer none): The first key in @table in iteration order,
 *  or %NULL if @table is empty.
 */
gpointer
pv_hash_table_get_arbitrary_key (GHashTable *table)
{
  GHashTableIter iter;
  gpointer key = NULL;

  g_hash_table_iter_init (&iter, table);
  if (g_hash_table_iter_next (&iter, &key, NULL))
    return key;
  else
    return NULL;
}

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable :-( */
static struct
{
  gchar *source_root;
  gchar *dest_root;
  GError *error;
} nftw_data;

static int
copy_tree_helper (const char *fpath,
                  const struct stat *sb,
                  int typeflag,
                  struct FTW *ftwbuf)
{
  size_t len;
  const char *suffix;
  g_autofree gchar *dest = NULL;
  g_autofree gchar *target = NULL;
  GError **error = &nftw_data.error;

  g_return_val_if_fail (g_str_has_prefix (fpath, nftw_data.source_root), 1);

  if (strcmp (fpath, nftw_data.source_root) == 0)
    {
      if (typeflag != FTW_D)
        {
          glnx_throw (error, "\"%s\" is not a directory", fpath);
          return 1;
        }

      if (!glnx_shutil_mkdir_p_at (-1, nftw_data.dest_root,
                                   sb->st_mode & 07777, NULL, error))
        return 1;

      return 0;
    }

  len = strlen (nftw_data.source_root);
  g_return_val_if_fail (fpath[len] == '/', 1);
  suffix = &fpath[len + 1];
  dest = g_build_filename (nftw_data.dest_root, suffix, NULL);

  switch (typeflag)
    {
      case FTW_D:
        if (!glnx_shutil_mkdir_p_at (-1, dest, sb->st_mode & 07777,
                                     NULL, error))
          return 1;
        break;

      case FTW_SL:
        target = glnx_readlinkat_malloc (-1, fpath, NULL, error);

        if (target == NULL)
          return 1;

        if (symlink (target, dest) != 0)
          {
            glnx_throw_errno_prefix (error,
                                     "Unable to create symlink at \"%s\"",
                                     dest);
            return 1;
          }
        break;

      case FTW_F:
        /* Fast path: try to make a hard link. */
        if (link (fpath, dest) == 0)
          break;

        /* Slow path: fall back to copying.
         *
         * This does a FICLONE or copy_file_range to get btrfs reflinks
         * if possible, making the copy as cheap as cp --reflink=auto.
         *
         * Rather than second-guessing which errno values would result
         * in link() failing but a copy succeeding, we just try it
         * unconditionally - the worst that can happen is that this
         * fails too. */
        if (!glnx_file_copy_at (AT_FDCWD, fpath, sb,
                                AT_FDCWD, dest,
                                GLNX_FILE_COPY_OVERWRITE,
                                NULL, error))
          {
            glnx_prefix_error (error, "Unable to copy \"%s\" to \"%s\"",
                               fpath, dest);
            return 1;
          }
        break;

      default:
        glnx_throw (&nftw_data.error,
                    "Don't know how to handle ftw type flag %d at %s",
                    typeflag, fpath);
        return 1;
    }

  return 0;
}

gboolean
pv_cheap_tree_copy (const char *source_root,
                    const char *dest_root,
                    GError **error)
{
  int res;

  g_return_val_if_fail (source_root != NULL, FALSE);
  g_return_val_if_fail (dest_root != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* Can't run concurrently */
  g_return_val_if_fail (nftw_data.source_root == NULL, FALSE);

  nftw_data.source_root = flatpak_canonicalize_filename (source_root);
  nftw_data.dest_root = flatpak_canonicalize_filename (dest_root);
  nftw_data.error = NULL;

  res = nftw (nftw_data.source_root, copy_tree_helper, 100, FTW_PHYS);

  if (res == -1)
    {
      g_assert (nftw_data.error == NULL);
      glnx_throw_errno_prefix (error, "Unable to copy \"%s\" to \"%s\"",
                               source_root, dest_root);
    }
  else if (res != 0)
    {
      g_propagate_error (error, g_steal_pointer (&nftw_data.error));
    }

  g_clear_pointer (&nftw_data.source_root, g_free);
  g_clear_pointer (&nftw_data.dest_root, g_free);
  g_assert (nftw_data.error == NULL);
  return (res == 0);
}

static gint
ftw_remove (const gchar *path,
            const struct stat *sb,
            gint typeflags,
            struct FTW *ftwbuf)
{
  if (remove (path) < 0)
    return -1;

  return 0;
}

/*
 * @directory: (type filename): The directory to remove.
 *
 * Recursively delete @directory within the same file system and
 * without following symbolic links.
 *
 * Returns: %TRUE if the removal was successful
 */
gboolean
pv_rm_rf (const char *directory)
{
  g_return_val_if_fail (directory != NULL, FALSE);

  if (nftw (directory, ftw_remove, 10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0)
    return FALSE;

  return TRUE;
}

gboolean
pv_boolean_environment (const gchar *name,
                        gboolean def)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRUE;

  if (g_strcmp0 (value, "") == 0 || g_strcmp0 (value, "0") == 0)
    return FALSE;

  if (value != NULL)
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return def;
}

/**
 * pv_divert_stdout_to_stderr:
 * @error: Used to raise an error on failure
 *
 * Duplicate file descriptors so that functions that would write to
 * `stdout` instead write to a copy of the original `stderr`. Return
 * a file handle that can be used to print structured output to the
 * original `stdout`.
 *
 * Returns: (transfer full): A libc file handle for the original `stdout`,
 *  or %NULL on error. Free with `fclose()`.
 */
FILE *
pv_divert_stdout_to_stderr (GError **error)
{
  g_autoptr(FILE) original_stdout = NULL;
  glnx_autofd int original_stdout_fd = -1;
  int flags;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Duplicate the original stdout so that we still have a way to write
   * machine-readable output. */
  original_stdout_fd = dup (STDOUT_FILENO);

  if (original_stdout_fd < 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to duplicate fd %d",
                                         STDOUT_FILENO);

  flags = fcntl (original_stdout_fd, F_GETFD, 0);

  if (flags < 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to get flags of new fd");

  fcntl (original_stdout_fd, F_SETFD, flags|FD_CLOEXEC);

  /* If something like g_debug writes to stdout, make it come out of
   * our original stderr. */
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to make fd %d a copy of fd %d",
                                         STDOUT_FILENO, STDERR_FILENO);

  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to create a stdio wrapper for fd %d",
                                         original_stdout_fd);
  else
    original_stdout_fd = -1;    /* ownership taken, do not close */

  return g_steal_pointer (&original_stdout);
}

/**
 * pv_async_signal_safe_error:
 * @message: A human-readable message
 * @exit_status: Call `_exit` with this status
 *
 * Exit with a fatal error, like g_error(), but async-signal-safe
 * (see signal-safety(7)).
 */
void
pv_async_signal_safe_error (const char *message,
                            int exit_status)
{
  if (write (2, message, strlen (message)) < 0)
    {
      /* Ignore - there's nothing we can do about it anyway - but
       * suppress -Wunused-result. */
    }

  _exit (exit_status);
}

#define PROC_SYS_KERNEL_RANDOM_UUID "/proc/sys/kernel/random/uuid"

/**
 * pv_get_random_uuid:
 * @error: Used to raise an error on failure
 *
 * Return a random UUID (RFC 4122 version 4) as a string.
 * It is a 128-bit quantity, with 122 bits of entropy, and 6 fixed bits
 * indicating the "variant" (type, 0b10) and "version" (subtype, 0b0100).
 *
 * Returns: (transfer full): A random UUID, or %NULL on error
 */
gchar *
pv_get_random_uuid (GError **error)
{
  g_autofree gchar *contents = NULL;

  if (!g_file_get_contents (PROC_SYS_KERNEL_RANDOM_UUID,
                            &contents, NULL, error))
    return NULL;

  g_strchomp (contents);    /* delete trailing newline */

  /* Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
  if (strlen (contents) != 36)
    return glnx_null_throw (error, "%s not in expected format",
                            PROC_SYS_KERNEL_RANDOM_UUID);

  return g_steal_pointer (&contents);
}
