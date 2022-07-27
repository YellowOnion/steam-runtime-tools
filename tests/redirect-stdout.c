/*
 * Copyright © 2022 Collabora Ltd.
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

#include "tests/test-utils.h"

#include <stdio.h>

#include <glib.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/utils-internal.h"

#if !GLIB_CHECK_VERSION(2, 68, 0)
FILE *original_stdout = NULL;

static void
print_to_original_stdout (const char *message)
{
  if (original_stdout != NULL)
    fputs (message, original_stdout);
  else
    fputs (message, stdout);
}
#endif

/*
 * Ensure that g_debug() and g_info() log to stderr.
 *
 * This is a simplified version of _srt_divert_stdout_to_stderr(),
 * which isn't available when linking dynamically to libsteam-runtime-tools.
 * It only works for "well-behaved" GLib logging.
 */
void
_srt_tests_global_debug_log_to_stderr (void)
{
#if GLIB_CHECK_VERSION(2, 68, 0)
  /* Send g_debug() and g_info() to stderr instead of the default stdout. */
  g_log_writer_default_set_use_stderr (TRUE);
#else
  /* This is less thorough than the tricks with dup2() that are used
   * in _srt_divert_stdout_to_stderr(), but it's enough to divert GLib
   * logging without having to write a new GLogFunc or GLogWriterFunc,
   * which would be GLib-version-dependent. Not diverting them at the
   * file descriptor level also means our tests implicitly assert that
   * we don't leak unstructured output onto stdout. */
  original_stdout = stdout;
  stdout = stderr;

  /* GLib's GTest framework has historically used g_print() to write
   * its structured output to stdout. This isn't completely future-proof,
   * but in this #else we only need to deal with past versions of GLib. */
  g_set_print_handler (print_to_original_stdout);
#endif
}
