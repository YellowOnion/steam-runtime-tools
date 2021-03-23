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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

int
main (int argc,
      char **argv)
{
  g_return_val_if_fail (argc == 3, EXIT_FAILURE);
  g_return_val_if_fail (strcmp (argv[1], "--line-based") == 0, EXIT_FAILURE);

  /* If the argument is an absolute path we assume it is a library loader.
   * Because the loaders are mock objects we just check if they are located in
   * the expected locations. */
  if (g_str_has_prefix (argv[2], "/"))
    {
      if (g_strstr_len (argv[2], -1, "/lib/i386-linux-gnu/") != NULL ||
          g_strstr_len (argv[2], -1, "/lib32/dri/") != NULL ||
          g_strstr_len (argv[2], -1, "/lib/dri/") != NULL ||
          g_strstr_len (argv[2], -1, "/lib/vdpau/") != NULL ||
          g_strstr_len (argv[2], -1, "/another_custom_path/") != NULL ||
          g_strstr_len (argv[2], -1, "/custom_path32/") != NULL ||
          g_strstr_len (argv[2], -1, "/custom_path32_2/") != NULL)
        {
          printf ("requested=%s\n", argv[2]);
          printf ("path=%s\n", argv[2]);
          return EXIT_SUCCESS;
        }
      else
        {
          return EXIT_FAILURE;
        }
    }

  /* If the argument is a 64bit directory, we return an exit failure */
  if (g_strstr_len (argv[2], -1, "/custom_path64/") != NULL)
    return EXIT_FAILURE;

  gchar **envp = g_get_environ ();
  gchar *path = g_build_filename (g_environ_getenv (envp, "SRT_TEST_SYSROOT"), "usr", "lib", argv[2], NULL);

  /* Return as though we found the given soname in a canonical Fedora style,
   * 32-bit lib directory */
  printf ("requested=%s\n", argv[2]);
  printf ("path=%s\n", path);
  g_free (path);
  g_strfreev (envp);
  return EXIT_SUCCESS;
}
