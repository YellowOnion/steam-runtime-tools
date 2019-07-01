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

/*
 * Output basic information about the system on which the tool is run.
 * The output is a JSON object with the following keys:
 *
 * can-run:
 *   An object. The keys are strings representing multiarch tuples, as
 *   used in Debian and the freedesktop.org SDK runtime. The values are
 *   boolean: true if we can definitely run executables of this
 *   architecture, or false if we cannot prove that we can do so.
 */

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <glib.h>

int
main (int argc,
      char **argv)
{
  g_print ("{\n");

  g_print ("  \"can-run\": {\n");
  g_print ("    \"i386-linux-gnu\": %s,\n",
           srt_architecture_can_run_i386 () ? "true" : "false");
  g_print ("    \"x86_64-linux-gnu\": %s\n",
           srt_architecture_can_run_x86_64 () ? "true" : "false");
  g_print ("  }\n");

  g_print ("}\n");
  return 0;
}
