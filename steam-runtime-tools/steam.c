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

#include "steam-runtime-tools/steam.h"
#include "steam-runtime-tools/steam-internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "steam-runtime-tools/glib-compat.h"

/**
 * SECTION:steam
 * @title: Steam installation
 * @short_description: Information about the Steam installation
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtSteamIssues represents problems encountered with the Steam
 * installation.
 */

/*
 * _srt_steam_check:
 * @environ: (nullable): The list of environment variables to use.
 * @path_out: (out) (optional) (nullable) (type filename):
 *  Used to return the absolute path to the Steam installation
 * @bin32_out: (out) (optional) (nullable) (type filename):
 *  Used to return the absolute path to `ubuntu12_32`
 */
SrtSteamIssues
_srt_steam_check (const GStrv environ,
                  gchar **path_out,
                  gchar **bin32_out)
{
  SrtSteamIssues issues = SRT_STEAM_ISSUES_NONE;
  gchar *dot_steam_bin32 = NULL;
  gchar *dot_steam_steam = NULL;
  gchar *dot_steam_root = NULL;
  gchar *default_steam_path = NULL;
  char *path = NULL;
  char *bin32 = NULL;
  const char *home = NULL;
  const char *user_data = NULL;
  GStrv env = NULL;

  g_return_val_if_fail (path_out == NULL || *path_out == NULL,
                        SRT_STEAM_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (bin32_out == NULL || *bin32_out == NULL,
                        SRT_STEAM_ISSUES_INTERNAL_ERROR);

  env = (environ == NULL) ? g_get_environ () : g_strdupv (environ);

  home = g_environ_getenv (env, "HOME");
  user_data = g_environ_getenv (env, "XDG_DATA_HOME");

  if (home == NULL)
    home = g_get_home_dir ();

  if (user_data == NULL)
    user_data = g_get_user_data_dir ();

  default_steam_path = g_build_filename (user_data,
                                         "Steam", NULL);

  dot_steam_bin32 = g_build_filename (home, ".steam", "bin32", NULL);
  dot_steam_steam = g_build_filename (home, ".steam", "steam", NULL);
  dot_steam_root = g_build_filename (home, ".steam", "root", NULL);

  /* Canonically, ~/.steam/steam is a symlink to the Steam installation.
   * (This is ignoring the Valve-internal "beta universe", which uses
   * ~/.steam/steambeta instead.) */
  if (g_file_test (dot_steam_steam, G_FILE_TEST_IS_SYMLINK))
    {
      path = realpath (dot_steam_steam, NULL);

      if (path == NULL)
        g_debug ("realpath(%s): %s", dot_steam_steam, g_strerror (errno));
    }
  else
    {
      /* e.g. https://bugs.debian.org/916303 */
      issues |= SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK;
    }

  /* If that doesn't work, try ~/.steam/root, which points to whichever
   * one of the public or beta universe was run most recently. */
  if (path == NULL &&
      g_file_test (dot_steam_root, G_FILE_TEST_IS_SYMLINK))
    {
      path = realpath (dot_steam_root, NULL);

      if (path == NULL)
        g_debug ("realpath(%s): %s", dot_steam_root, g_strerror (errno));
    }

  /* If *that* doesn't work, try going up one level from ubuntu12_32,
   * to which ~/.steam/bin32 is a symlink */
  if (path == NULL &&
      g_file_test (dot_steam_bin32, G_FILE_TEST_IS_SYMLINK))
    {
      bin32 = realpath (dot_steam_bin32, NULL);

      if (bin32 == NULL)
        {
          g_debug ("realpath(%s): %s", dot_steam_bin32, g_strerror (errno));
        }
      else if (!g_str_has_suffix (bin32, "/ubuntu12_32"))
        {
          g_debug ("Unexpected bin32 path: %s -> %s", dot_steam_bin32,
                   bin32);
        }
      else
        {
          path = strndup (bin32, strlen (bin32) - strlen ("/ubuntu12_32"));

          /* We don't try to survive out-of-memory */
          if (path == NULL)
            g_error ("strndup: %s", g_strerror (errno));
        }
    }

  /* If *that* doesn't work, try the default installation location. */
  if (path == NULL)
    {
      path = realpath (default_steam_path, NULL);

      if (path == NULL)
        g_debug ("realpath(%s): %s", default_steam_path, g_strerror (errno));
    }

  if (path == NULL)
    {
      g_debug ("Unable to find Steam installation");
      issues |= SRT_STEAM_ISSUES_CANNOT_FIND;
      goto out;
    }

  g_debug ("Found Steam installation at %s", path);

  /* If we haven't found ubuntu12_32 yet, it's a subdirectory of the
   * Steam installation */
  if (bin32 == NULL)
    {
      /* We don't try to survive out-of-memory */
      if (asprintf (&bin32, "%s/ubuntu12_32", path) < 0)
        g_error ("asprintf: %s", g_strerror (errno));
    }

  if (bin32 != NULL)
    {
      g_debug ("Found ubuntu12_32 directory at %s", bin32);
    }
  else
    {
      g_debug ("Unable to find ubuntu12_32 directory");
    }

  /* We can't just transfer ownership here, because in the older GLib
   * that we're targeting, g_free() and free() are not guaranteed to be
   * associated with the same memory pool. */
  if (path_out != NULL)
    *path_out = g_strdup (path);

  if (bin32_out != NULL)
    *bin32_out = g_strdup (bin32);

out:
  free (path);
  free (bin32);
  g_free (dot_steam_bin32);
  g_free (dot_steam_steam);
  g_free (dot_steam_root);
  g_free (default_steam_path);
  g_strfreev (env);
  return issues;
}
