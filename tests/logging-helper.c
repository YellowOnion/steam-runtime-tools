/*
 * Copyright © 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "libglnx.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static gboolean opt_allow_journal = FALSE;
static gboolean opt_divert_stdout = FALSE;
static gboolean opt_keep_prgname = FALSE;

static const GOptionEntry option_entries[] =
{
  { "allow-journal", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_allow_journal, "Set OPTIONALLY_JOURNAL flag", NULL },
  { "divert-stdout", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_divert_stdout, "Set DIVERT_STDOUT flag", NULL },
  { "keep-prgname", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_keep_prgname, "Don't call g_set_prgname()", NULL },
  { NULL }
};

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) option_context = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;
  const char *prgname = "srt-tests-logging-helper";
  const char *env;
  SrtLogFlags flags = SRT_LOG_FLAGS_NONE;

  option_context = g_option_context_new ("MESSAGE");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      g_warning ("%s: %s\n", prgname, error->message);
      return 1;
    }

  if (opt_allow_journal)
    flags |= SRT_LOG_FLAGS_OPTIONALLY_JOURNAL;

  if (opt_divert_stdout)
    flags |= SRT_LOG_FLAGS_DIVERT_STDOUT;

  if (opt_keep_prgname)
    prgname = NULL;

  if (!_srt_util_set_glib_log_handler (prgname, G_LOG_DOMAIN, flags,
                                       &original_stdout, &original_stderr, &error))
    {
      g_warning ("%s: %s\n", g_get_prgname (), error->message);
      return 1;
    }

  /* _srt_util_set_glib_log_handler() ensures the three standard fds are
   * open, even if only pointing to /dev/null */
  g_assert_no_errno (fcntl (STDIN_FILENO, F_GETFL, 0));
  g_assert_no_errno (fcntl (STDOUT_FILENO, F_GETFL, 0));
  g_assert_no_errno (fcntl (STDERR_FILENO, F_GETFL, 0));

  g_message ("%s", argv[1]);

  env = g_getenv ("SRT_LOG");
  g_message ("SRT_LOG=%s", env ?: "(null)");

  if (_srt_boolean_environment ("SRT_LOG_TO_JOURNAL", FALSE))
    g_message ("SRT_LOG_TO_JOURNAL is true");

  if (_srt_boolean_environment ("PRESSURE_VESSEL_LOG_INFO", FALSE))
    g_message ("P_V_LOG_INFO is true");

  if (_srt_boolean_environment ("PRESSURE_VESSEL_LOG_WITH_TIMESTAMP", FALSE))
    g_message ("P_V_LOG_WITH_TIMESTAMP is true");

  g_message ("flags: allow_journal=%d divert_stdout=%d keep_prgname=%d",
             opt_allow_journal, opt_divert_stdout, opt_keep_prgname);

  g_print ("stdout while running\n");
  g_printerr ("stderr while running\n");
  g_debug ("debug message");
  g_info ("info message");
  g_message ("notice message");

  if ((original_stdout >= 0
       && !_srt_util_restore_saved_fd (original_stdout, STDOUT_FILENO, &error))
      || (original_stderr >= 0
          && !_srt_util_restore_saved_fd (original_stderr, STDERR_FILENO, &error)))
    {
      g_warning ("%s: %s\n", prgname, error->message);
      return 1;
    }

  g_print ("original stdout\n");
  g_printerr ("original stderr\n");

  return 0;
}
