/*
 * Copyright © 2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "steam-runtime-tools/utils-internal.h"

#include <dlfcn.h>
#include <link.h>
#include <string.h>

#include <glib-object.h>

static gchar *helpers_path = NULL;

G_GNUC_INTERNAL const char *
_srt_get_helpers_path (void)
{
  const char *path;
  Dl_info ignored;
  struct link_map *map = NULL;
  gchar *dir;

  path = helpers_path;

  if (path != NULL)
    goto out;

  path = g_getenv ("SRT_HELPERS_PATH");

  if (path != NULL)
    goto out;

  if (dladdr1 (_srt_get_helpers_path, &ignored, (void **) &map,
               RTLD_DL_LINKMAP) == 0 ||
      map == NULL)
    {
      g_warning ("Unable to locate shared library containing "
                 "_srt_get_helpers_path()");
      goto out;
    }

  g_debug ("Found _srt_get_helpers_path() in %s", map->l_name);
  dir = g_path_get_dirname (map->l_name);

  if (g_str_has_suffix (dir, "/lib/" _SRT_MULTIARCH))
    dir[strlen (dir) - strlen ("/lib/" _SRT_MULTIARCH)] = '\0';
  else if (g_str_has_suffix (dir, "/lib64"))
    dir[strlen (dir) - strlen ("/lib64")] = '\0';
  else if (g_str_has_suffix (dir, "/lib"))
    dir[strlen (dir) - strlen ("/lib")] = '\0';

  /* If the library was found in /lib/MULTIARCH, /lib64 or /lib on a
   * merged-/usr system, assume --prefix=/usr (/libexec doesn't
   * normally exist) */
  if (dir[0] == '\0')
    {
      g_free (dir);
      dir = g_strdup ("/usr");
    }

  /* deliberate one-per-process leak */
  helpers_path = g_build_filename (
      dir, "libexec", "steam-runtime-tools-" _SRT_API_MAJOR,
      NULL);
  path = helpers_path;

  g_free (dir);

out:
  /* We have to return *something* non-NULL */
  if (path == NULL)
    {
      g_warning ("Unable to determine path to helpers");
      path = "/";
    }

  return path;
}

#if !GLIB_CHECK_VERSION(2, 36, 0)
static void _srt_constructor (void) __attribute__((__constructor__));
static void
_srt_constructor (void)
{
  g_type_init ();
}
#endif
