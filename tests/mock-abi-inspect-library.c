/*
 * Copyright © 2019-2021 Collabora Ltd.
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
#include <glib.h>

int
main (int argc,
      char **argv)
{
  g_return_val_if_fail (argc == 2, EXIT_FAILURE);

#if defined(MOCK_ARCHITECTURE_x86_64)
  const gchar *multiarch = "x86_64-mock-abi";
  const gchar *lib_dir = "lib64";
  const gchar *wrong_lib_dir = "32/";
  const gchar *wrong_abi = "i386";
#else
  const gchar *multiarch = "i386-mock-abi";
  const gchar *lib_dir = "lib32";
  const gchar *wrong_lib_dir = "64/";
  const gchar *wrong_abi = "x86_64";
#endif

  int ret = EXIT_FAILURE;
  gchar **split = NULL;
  gchar *path = NULL;
  gchar **envp = g_get_environ ();

  if (argv[1][0] == '/')
    {
      /* This is a very naive check to simulate the exit error that occurrs
       * when we request a library that is of the wrong ELF class. */
      if (g_strstr_len (argv[1], -1, wrong_abi) != NULL)
        goto out;
      if (g_strstr_len (argv[1], -1, wrong_lib_dir) != NULL)
        goto out;

      /* If the path is already absolute, just prepend the sysroot */
      path = g_build_filename (g_environ_getenv (envp, "SRT_TEST_SYSROOT"), argv[1], NULL);

    }
  else
    {
      path = g_build_filename (g_environ_getenv (envp, "SRT_TEST_SYSROOT"), "usr",
                               "lib", multiarch, argv[1], NULL);
    }

  /* When loading a library by its absolute or relative path, glib expands
   * dynamic string tokens: LIB, PLATFORM, ORIGIN.
   * We can just check for $LIB because it's the only one in use right now
   * in the MangoHUD test. */
  split = g_strsplit (path, "$LIB", -1);
  g_free (path);
  path = g_strjoinv (lib_dir, split);

  /* Return a JSON like if we found the given soname in a mock-abi lib folder */
  printf ("{\n\t\"%s\": {\n"
          "\t\t\"path\": \"%s\"\n"
          "\t}\n"
          "}\n", argv[1], path);

  ret = EXIT_SUCCESS;

out:
  g_free (path);
  g_strfreev (envp);
  g_strfreev (split);
  return ret;
}
