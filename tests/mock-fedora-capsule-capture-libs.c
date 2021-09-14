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

#include <steam-runtime-tools/glib-backports-internal.h>

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
  const gchar *soname_match = NULL;
  GPtrArray *found = NULL;

#if defined(MOCK_ARCHITECTURE_x86_64)
  static const char *const sonames[] = { "libvdpau_r9000.so", NULL };
  /* We don't use the x86_64 version yet */
  return EXIT_FAILURE;
#else
  static const char *const sonames[] = { "libvdpau_r9000.so", NULL };
#endif

  g_return_val_if_fail (argc > 5, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[1], "--dest") == 0, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[3], "--provider") == 0, EXIT_FAILURE);

  found = g_ptr_array_new_with_free_func (g_free);

  /* Currently we are using this just for VDPAU. Discard every other request.
   * Also we mimic a system where a wildcard-matching search will return no
   * results. */
  for (gsize i = 0; sonames[i] != NULL; i++)
    {
      for (gsize j = 5; argv[j] != NULL; j++)
        {
          soname_match = g_strrstr (argv[j], sonames[i]);
          if (soname_match != NULL)
            g_ptr_array_add (found, g_strdup (sonames[i]));
        }
    }

  for (gsize i = 0; i < found->len; i++)
    {
      file_path = g_build_filename (argv[2], found->pdata[i], NULL);
      file = g_file_new_for_path (file_path);
      target = g_build_filename ("/usr/lib", found->pdata[i], NULL);
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

  g_ptr_array_free (found, TRUE);

  return EXIT_SUCCESS;
}
