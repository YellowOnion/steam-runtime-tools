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

#include <glib.h>
#include <stdio.h>

#include "../steam-runtime-tools/graphics-test-defines.h"

int
main (int argc,
      char **argv)
{
  // Give good output
  printf ("{\n\t\"waffle\": {\n\t\t\"platform\": \"glx\",\n\t\t\"api\": \"gl\"\n\t},\n\t\"OpenGL\": {\n\t\t\"vendor string\": \"Intel Open Source Technology Center\",\n"
          "\t\t\"renderer string\": \""
          SRT_TEST_GOOD_GRAPHICS_RENDERER
          "\",\n\t\t\"version string\": \""
          SRT_TEST_GOOD_GRAPHICS_VERSION
          "\",\n\t\t\"shading language version string\": \"1.30\",\n"
          "\t\t\"extensions\": [\n"
          "\t\t]\n\t}\n}\n");

  if (g_strcmp0 (g_getenv ("LIBGL_DEBUG"), "verbose") == 0)
    {
      fprintf (stderr, "info: you used LIBGL_DEBUG=verbose\n");
    }

  return 0;
}

