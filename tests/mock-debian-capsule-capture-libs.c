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

#include <steam-runtime-tools/glib-compat.h>

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>

int
main (int argc,
      char **argv)
{
  GError *error = NULL;
  gchar *file_path = NULL;
  gchar *target = NULL;
  GFile *file = NULL;

#if defined(MOCK_ARCHITECTURE_x86_64)
  const gchar *multiarch = "x86_64-linux-gnu";
  static const char *const sonames[] = { "libGLX_mesa.so.0", NULL };
#else
  const gchar *multiarch = "i386-linux-gnu";
  static const char *const sonames[] = { "libGLX_mesa.so.0", "libGLX_nvidia.so.0", NULL };
#endif

  g_return_val_if_fail (argc > 3, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[1], "--dest") == 0, EXIT_FAILURE);

  for (gsize i = 0; sonames[i] != NULL; i++)
    {
      file_path = g_build_filename (argv[2], sonames[i], NULL);
      file = g_file_new_for_path (file_path);
      target = g_build_filename ("/lib", multiarch, sonames[i], NULL);
      if (!g_file_make_symbolic_link (file, target, NULL, &error))
        {
          g_critical ("An error occurred creating a symlink: %s", error->message);
          g_clear_error (&error);
          g_clear_pointer (&file, g_object_unref);
          g_free (file_path);
          g_free (target);
          return EXIT_FAILURE;
        }
      g_clear_pointer (&file, g_object_unref);
      g_free (file_path);
      g_free (target);
    }

  return EXIT_SUCCESS;
}
