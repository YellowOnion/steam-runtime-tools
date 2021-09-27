/*
 * Copyright © 2020-2021 Collabora Ltd.
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
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;
  const gchar *soname_match = NULL;
  const gchar *dest = NULL;
  const gchar *provider = NULL;
  g_autoptr(GPtrArray) found = NULL;
  int first_pattern_index = 5;
  gsize i;

#if defined(MOCK_ARCHITECTURE_x86_64)

#if defined(MOCK_DISTRO_DEBIAN)
  const gchar *lib_dir = "/usr/lib/x86_64-linux-gnu";
#elif defined(MOCK_DISTRO_ABI)
  const gchar *lib_dir = "/usr/lib/x86_64-mock-abi";
#elif defined(MOCK_DISTRO_UBUNTU)
  const gchar *lib_dir = "/usr/lib/x86_64-mock-ubuntu";
#else
  const gchar *lib_dir = "/usr/lib64";
#endif

#else

#if defined(MOCK_DISTRO_DEBIAN)
  const gchar *lib_dir = "/usr/lib/i386-linux-gnu";
#elif defined(MOCK_DISTRO_ABI)
  const gchar *lib_dir = "/usr/lib/i386-mock-abi";
#else
  const gchar *lib_dir = "/usr/lib";
#endif

#endif  // MOCK_ARCHITECTURE_x86_64

  g_return_val_if_fail (argc > first_pattern_index, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[1], "--dest") == 0, EXIT_FAILURE);
  g_return_val_if_fail (g_strcmp0 (argv[3], "--provider") == 0, EXIT_FAILURE);

  dest = argv[2];
  provider = argv[4];

  static const char *const sonames[] = { "libvdpau_r9000.so", "libGLX_mesa.so.0",
                                         "libGLX_nvidia.so.0", "libEGL_mesa.so.0",
                                         "libGL.so.1", "libva.so.2", "libva.so.1",
                                         "libvdpau.so.1", NULL };

  if (g_strcmp0 (argv[first_pattern_index], "--link-target=/") == 0)
    {
      first_pattern_index++;
      g_return_val_if_fail (argc > first_pattern_index, EXIT_FAILURE);
    }

  found = g_ptr_array_new_with_free_func (g_free);

  /* Mimic a system where a wildcard-matching search will return no
   * results. */
  for (i = 0; sonames[i] != NULL; i++)
    {
      for (gsize j = first_pattern_index; argv[j] != NULL; j++)
        {
          g_autofree gchar *lib_full_path = NULL;
          soname_match = g_strrstr (argv[j], sonames[i]);
          if (soname_match == NULL)
            continue;

          lib_full_path = g_build_filename (provider, lib_dir, sonames[i], NULL);

#if defined(MOCK_DISTRO_UBUNTU)
          /* This is a special case where libGL.so.1 is located under the "mesa"
           * directory */
          if (g_strcmp0 (sonames[i], "libGL.so.1") == 0)
            {
              g_clear_pointer (&lib_full_path, g_free);
              lib_full_path = g_build_filename (provider, lib_dir, "mesa",
                                                sonames[i], NULL);
            }
#endif
          g_print ("%s\n", lib_full_path);

          if (g_file_test (lib_full_path, G_FILE_TEST_EXISTS))
            g_ptr_array_add (found, g_strdup (sonames[i]));
        }
    }

  for (i = 0; i < found->len; i++)
    {
      g_autofree gchar *file_path = NULL;
      g_autofree gchar *target = NULL;
      file_path = g_build_filename (dest, found->pdata[i], NULL);
      file = g_file_new_for_path (file_path);
      target = g_build_filename (lib_dir, found->pdata[i], NULL);

#if defined(MOCK_DISTRO_UBUNTU)
          if (g_strcmp0 (found->pdata[i], "libGL.so.1") == 0)
            {
              g_clear_pointer (&target, g_free);
              target = g_build_filename (lib_dir, "mesa", found->pdata[i], NULL);
            }
#endif

      if (!g_file_make_symbolic_link (file, target, NULL, &error))
        {
          g_critical ("An error occurred creating a symlink: %s", error->message);
          return EXIT_FAILURE;
        }
    }

  return EXIT_SUCCESS;
}
