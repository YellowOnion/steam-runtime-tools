/*
 * Copyright © 2020 Collabora Ltd.
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

#include <libglnx.h>

#include <steam-runtime-tools/glib-backports-internal.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>

#include "test-utils.h"

int
main (int argc,
      char **argv)
{
  gchar **envp = g_get_environ ();

  g_return_val_if_fail (argc > 5, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[1], "--dest") == 0, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[3], "--provider") == 0, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[4], g_environ_getenv (envp, "SRT_TEST_SYSROOT")) == 0,
                        EXIT_FAILURE);

  const char *dest = argv[2];

#if defined(MOCK_ARCHITECTURE_x86_64)
  const gchar *multiarch = "x86_64-linux-gnu";
  const gchar *multiarch_mock = "x86_64-mock-container";
#else
  const gchar *multiarch = "i386-linux-gnu";
  const gchar *multiarch_mock = "i386-mock-container";
#endif

  gchar *path = g_build_filename (g_environ_getenv (envp, "SRT_TEST_SYSROOT"), "overrides", "lib", multiarch, NULL);

  for (int i = 5; i < argc; i++)
    {
      /* We are in a container. A soname-match it's likely to fail because `ld.so.cache`
       * doesn't have a reference to them. We mimic this behavior here. */
      if (g_strrstr (argv[i], "soname-match:"))
        {
          continue;
        }

      const gchar *soname_match = g_strrstr (argv[i], "soname:");
      if (soname_match)
        {
          gchar **soname_split = g_strsplit (soname_match, "soname:", 2);
          const gchar *soname = soname_split[1];
          gchar *file_path = g_build_filename (path, soname, NULL);
          /* We continue only if we have the soname that we are searching for */
          if (g_file_test (file_path, G_FILE_TEST_EXISTS))
            {
              gchar *dest_full_path = g_build_filename (dest, soname, NULL);
              gchar *target = g_build_filename ("/lib", multiarch, soname, NULL);
              /* `path-match:` might have already created the symlink */
              if (symlink (target, dest_full_path) != 0 && errno != EEXIST)
                g_error ("symlink: %s", g_strerror (errno));

              g_free (dest_full_path);
              g_free (target);
            }
          g_strfreev (soname_split);
          g_free (file_path);
        }

      const gchar *path_match = g_strrstr (argv[i], "path-match:");
      if (path_match)
        {
          const gchar *filename;
          gchar *expected_path = g_build_filename (g_environ_getenv (envp, "SRT_TEST_SYSROOT"),
                                                   "overrides", "lib", multiarch_mock, NULL);
          gchar *expected_match_path = g_strdup_printf ("path-match:%s/libGLX_*.so.*", expected_path);
          /* We expect to be asked to look in /overrides/lib/MULTIARCH as a "path match" */
          g_assert_cmpstr (path_match, ==, expected_match_path);
          GDir *dir = g_dir_open (path, 0, NULL);
          g_assert_nonnull (dir);
          while ((filename = g_dir_read_name (dir)))
            {
              /* The real regex should be "libGLX_*.so.*" but for our testing purpose
               * checking only for files with "libGLX_" suffix is enough */
              if (g_str_has_prefix (filename, "libGLX_"))
                {
                  gchar *dest_full_path = g_build_filename (dest, filename, NULL);
                  gchar *target = g_build_filename ("/lib", multiarch, filename, NULL);
                  /* `soname:` might have already created the symlink */
                  if (symlink (target, dest_full_path) != 0 && errno != EEXIST)
                    g_error ("symlink: %s", g_strerror (errno));

                  g_free (dest_full_path);
                  g_free (target);
                }
            }
          g_free (expected_path);
          g_free (expected_match_path);
          if (dir != NULL)
            g_dir_close (dir);
        }
    }

  g_free (path);
  g_strfreev (envp);
  return EXIT_SUCCESS;
}
