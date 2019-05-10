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

#include "config.h"
#include "subprojects/libglnx/config.h"
#include "utils.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libglnx.h"

#include "glib-backports.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-private.h"

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

  if (ret != 0)
    return ret;

  if ((*s1)[min] == '\0')
    return -1;

  if ((*s2)[min] == '\0')
    return 1;

  if ((*s1)[min] == '=')
    return -1;

  if ((*s2)[min] == '=')
    return 1;

  return strcmp (*s1, *s2);
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
