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
#include <glib.h>

int
main (int argc,
      char **argv)
{
  g_return_val_if_fail (argc == 2, EXIT_FAILURE);

  gchar **envp = g_get_environ ();
  gchar *path = g_build_filename (g_environ_getenv (envp, "SRT_TEST_SYSROOT"), "usr", "lib", "i386-linux-gnu", argv[1], NULL);

  /* Return a JSON like if we found the given soname in a canonical Debian style, i386 lib folder */
  printf ("{\n\t\"%s\": {\n"
          "\t\t\"path\": \"%s\"\n"
          "\t}\n"
          "}\n", argv[1], path);
  g_free (path);
  g_strfreev (envp);
  return 0;
}
