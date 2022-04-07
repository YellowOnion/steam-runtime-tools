/*
 * Helper for misc smaller tests that need to try things in a separate
 * process.
 *
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

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "utils.h"

static void
try_divert_stdout (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FILE) original_stdout = NULL;

  original_stdout = _srt_divert_stdout_to_stderr (&error);
  g_assert_no_error (error);

  g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

  g_print ("printed-with-g-print\n");
  g_log ("tests-helper", G_LOG_LEVEL_DEBUG, "logged-as-debug");
  g_log (NULL, G_LOG_LEVEL_INFO, "logged-as-info");
  fprintf (original_stdout, "printed-to-original-stdout");

  fflush (stdout);
  fflush (original_stdout);
}

int
main (int argc,
      char **argv)
{
  if (argc < 2)
    g_error ("Usage: %s MODE [ARGUMENTS...]", argv[0]);

  if (g_strcmp0 (argv[1], "divert-stdout") == 0)
    {
      try_divert_stdout ();
    }
  else
    {
      g_error ("Unknown argv[1]: %s", argv[1]);
    }

  return 0;
}

