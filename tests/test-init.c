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

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/utils-internal.h"

#if !GLIB_CHECK_VERSION(2, 68, 0)
static FILE *original_stdout = NULL;

static void
print_to_original_stdout (const char *message)
{
  FILE *fh = original_stdout;

  if (fh == NULL)
    fh = stdout;

  fputs (message, fh);
  fflush (fh);
}
#endif

static gboolean global_debug_log_to_stderr = FALSE;

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
#if !GLIB_CHECK_VERSION(2, 68, 0)
  int original_stdout_fd;
  int flags;
#endif

  g_return_if_fail (!global_debug_log_to_stderr);
  global_debug_log_to_stderr = TRUE;

#if GLIB_CHECK_VERSION(2, 68, 0)
  /* Send g_debug() and g_info() to stderr instead of the default stdout. */
  g_log_writer_default_set_use_stderr (TRUE);
#else
  /* This is a mini version of _srt_divert_stdout_to_stderr(), with
   * more fragile error handling since this is only test code */
  g_assert_no_errno ((original_stdout_fd = dup (STDOUT_FILENO)));
  g_assert_no_errno ((flags = fcntl (original_stdout_fd, F_GETFD, 0)));
  g_assert_no_errno (fcntl (original_stdout_fd, F_SETFD, flags | FD_CLOEXEC));
  g_assert_no_errno (dup2 (STDERR_FILENO, STDOUT_FILENO));
  g_assert_no_errno ((original_stdout = fdopen (original_stdout_fd, "w")) ? 0 : -1);

  /* GLib's GTest framework has historically used g_print() to write
   * its structured output to stdout. This isn't completely future-proof,
   * but in this #else we only need to deal with past versions of GLib. */
  g_set_print_handler (print_to_original_stdout);
#endif
}

#if !GLIB_CHECK_VERSION(2, 70, 0)
static GLogFunc gtest_log_func = NULL;

/*
 * Work around GLib < 2.70 not escaping newlines in log messages when
 * passing them to g_test_message().
 */
static void
split_log_func (const char *log_domain,
                GLogLevelFlags log_level,
                const gchar *message,
                gpointer user_data)
{
  g_return_if_fail (gtest_log_func != NULL);

  if (strchr (message, '\n') == NULL
      || (log_level & (G_LOG_FLAG_RECURSION|G_LOG_FLAG_FATAL)) != 0)
    {
      /* For messages with no newline (common case), just output it.
       * For fatal messages, also just output it, otherwise we'd crash
       * after outputting the first split line. */
      gtest_log_func (log_domain, log_level, message, NULL);
    }
  else
    {
      /* GLib < 2.70 didn't replace newlines with "\n# " when outputting
       * a multi-line g_test_message(), so we have to split the message
       * into lines to avoid corrupting stdout. */
      g_auto(GStrv) lines = g_strsplit (message, "\n", -1);
      gchar **iter;

      for (iter = lines; iter != NULL && *iter != NULL; iter++)
        gtest_log_func (log_domain, log_level, *iter, NULL);
    }
}
#endif

static gboolean tests_init_done = FALSE;

gboolean
_srt_tests_init_was_called (void)
{
  return tests_init_done;
}

void
_srt_tests_init (int *argc,
                 char ***argv,
                 const char *reserved)
{
  const char *env;

  g_return_if_fail (!tests_init_done);
  g_return_if_fail (reserved == NULL);
  tests_init_done = TRUE;

  _srt_tests_global_debug_log_to_stderr ();
  g_test_init (argc, argv, NULL);

#if !GLIB_CHECK_VERSION(2, 70, 0)
  /* Do this *after* g_test_init so we can hijack its log handler */
  gtest_log_func = g_log_set_default_handler (split_log_func, NULL);
#endif

  env = g_getenv ("STEAM_RUNTIME");

  if (env != NULL && env[0] == '/')
    {
      gchar *helpers = g_build_filename (env, "usr", "libexec",
                                         "steam-runtime-tools-0", NULL);

      g_setenv ("SRT_HELPERS_PATH", helpers, TRUE);
      g_free (helpers);
    }
}
