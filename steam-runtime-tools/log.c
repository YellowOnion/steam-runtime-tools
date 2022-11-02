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

#include "steam-runtime-tools/log-internal.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <libglnx.h>

#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/*
 * SRT_LOG_LEVEL_FAILURE:
 *
 * A log level for logging fatal errors that do not indicate a programming
 * error, for example an invalid command-line option, or being asked to
 * open a file that does not exist.
 *
 * Only use this in programs that have called
 * _srt_util_set_glib_log_handler().
 *
 * This is functionally equivalent to %G_LOG_LEVEL_MESSAGE, but
 * the handler set up by _srt_util_set_glib_log_handler() prints it
 * as though it was a fatal error. Use it in command-line utilities
 * to log a GError that will cause program termination.
 *
 * Unlike %G_LOG_LEVEL_ERROR, this is not considered to indicate a
 * programming error, and does not cause a core dump.
 */

/*
 * _srt_log_failure:
 * @...: format string and arguments, as for g_message()
 *
 * Convenience macro to log at level %SRT_LOG_LEVEL_FAILURE.
 * Only use this in programs that have called
 * _srt_util_set_glib_log_handler().
 *
 * This is functionally equivalent to g_message(), but with a different
 * log level.
 */

typedef void *AutoLibraryHandle;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC (AutoLibraryHandle, dlclose, NULL);

typedef int (*type_of_sd_journal_stream_fd) (const char *, int, int);
static type_of_sd_journal_stream_fd our_sd_journal_stream_fd;

static int
no_sd_journal_stream_fd (const char *identifier,
                         int priority,
                         int use_prefix)
{
  return -ENOSYS;
}

/*
 * _srt_stdio_to_journal:
 * @identifier: Identifier to be used in log messages
 * @error: Used to raise an error on failure
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_stdio_to_journal (const char *identifier,
                       int target_fd,
                       int priority,
                       GError **error)
{
  glnx_autofd int fd = -1;

  g_return_val_if_fail (target_fd >= 0, FALSE);
  g_return_val_if_fail (identifier != NULL, FALSE);
  g_return_val_if_fail (priority >= LOG_EMERG, FALSE);
  g_return_val_if_fail (priority <= LOG_DEBUG, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (g_once_init_enter (&our_sd_journal_stream_fd))
    {
      g_auto(AutoLibraryHandle) handle = NULL;
      type_of_sd_journal_stream_fd sym = NULL;
      const char *message;

      handle = dlopen ("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);

      if (handle == NULL)
        {
          g_once_init_leave (&our_sd_journal_stream_fd, no_sd_journal_stream_fd);
          return glnx_throw (error, "%s", dlerror ());
        }

      (void) dlerror ();
      sym = (type_of_sd_journal_stream_fd) dlsym (handle, "sd_journal_stream_fd");

      message = dlerror ();

      if (message != NULL)
        {
          g_once_init_leave (&our_sd_journal_stream_fd, no_sd_journal_stream_fd);
          return glnx_throw (error, "%s", message);
        }
      if (sym == NULL)
        {
          g_once_init_leave (&our_sd_journal_stream_fd, no_sd_journal_stream_fd);
          return glnx_throw (error, "sd_journal_stream_fd resolved to NULL");
        }

      g_once_init_leave (&our_sd_journal_stream_fd, sym);
    }

  fd = our_sd_journal_stream_fd (identifier, priority, FALSE);

  if (fd < 0)
    {
      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_errno (-fd),
                   "sd_journal_stream_fd: %s",
                   g_strerror (-fd));
      return FALSE;
    }

  if (dup2 (fd, target_fd) != target_fd)
    return glnx_throw_errno_prefix (error,
                                    "Unable to make fd %d a copy of %d",
                                    target_fd, fd);

  return TRUE;
}

/*
 * Return whether stat_buf identifies char device (1,3), which is the
 * device number for /dev/null on Linux. This is part of the ABI, and
 * various container frameworks rely on it, so it's safe to hard-code.
 */
static gboolean
st_buf_is_dev_null (const struct stat *stat_buf)
{
  if (!S_ISCHR (stat_buf->st_mode))
    return FALSE;

  return (stat_buf->st_rdev == ((1 << 8) + 3));
}

/*
 * _srt_util_set_up_logging:
 * @identifier: the name of this program, passed to
 *  g_set_prgname() and `sd_journal_stream_fd`
 *
 * Set up logging for a command-line program such as steam-runtime-urlopen.
 */
void
_srt_util_set_up_logging (const char *identifier)
{
  struct stat stat_buf = {};

  g_return_if_fail (identifier != NULL);

  g_set_prgname (identifier);

  /* If specifically told to use the Journal, do so. */
  if (_srt_boolean_environment ("SRT_LOG_TO_JOURNAL", FALSE))
    {
      g_autoptr(GError) local_error = NULL;

      if (!_srt_stdio_to_journal (identifier, STDOUT_FILENO, LOG_INFO, &local_error))
        {
          g_warning ("%s: %s", identifier, local_error->message);
          g_clear_error (&local_error);
        }

      if (!_srt_stdio_to_journal (identifier, STDERR_FILENO, LOG_NOTICE, &local_error))
        {
          g_warning ("%s: %s", identifier, local_error->message);
          g_clear_error (&local_error);
        }

      return;
    }

  /* If stdout is /dev/null, replace it with the Journal. */
  if (fstat (STDOUT_FILENO, &stat_buf) != 0)
    {
      g_warning ("%s: Unable to stat stdout: %s",
                 identifier, g_strerror (errno));
    }
  else if (st_buf_is_dev_null (&stat_buf))
    {
      g_autoptr(GError) local_error = NULL;

      if (!_srt_stdio_to_journal (identifier, STDOUT_FILENO, LOG_INFO, &local_error))
        g_warning ("%s: %s", identifier, local_error->message);
    }

  /* If stderr is /dev/null, replace it with the Journal. */
  if (fstat (STDERR_FILENO, &stat_buf) != 0)
    {
      g_warning ("%s: Unable to stat stderr: %s",
                 identifier, g_strerror (errno));
    }
  else if (st_buf_is_dev_null (&stat_buf))
    {
      g_autoptr(GError) local_error = NULL;

      if (!_srt_stdio_to_journal (identifier, STDERR_FILENO, LOG_NOTICE, &local_error))
        g_warning ("%s: %s", identifier, local_error->message);
    }
}

static struct
{
  int pid;
  const gchar *prgname;
  SrtLogFlags flags;
} log_settings =
{
  .pid = -1,
};

/*
 * get_level_prefix:
 * @log_level: A GLib log level, which should normally only have one bit set
 *
 * Returns: A short prefix for log messages, for example "W" for warnings
 *  or "Internal error" for assertion failures.
 */
static const char *
get_level_prefix (GLogLevelFlags log_level)
{
  if (log_level & (G_LOG_FLAG_RECURSION
                   | G_LOG_FLAG_FATAL
                   | G_LOG_LEVEL_ERROR
                   | G_LOG_LEVEL_CRITICAL))
    return "Internal error";

  if (log_level & SRT_LOG_LEVEL_FAILURE)
    return "E";

  if (log_level & G_LOG_LEVEL_WARNING)
    return "W";

  if (log_level & G_LOG_LEVEL_MESSAGE)
    return "N";   /* consistent with apt, which calls this a "notice" */

  if (log_level & G_LOG_LEVEL_INFO)
    return "I";

  if (log_level & G_LOG_LEVEL_DEBUG)
    return "D";

  return "?!";
}

/*
 * log_handler:
 * @log_domain: the log domain of the message
 * @log_level: the log level of the message
 * @message: the message to process
 * @user_data: not used
 *
 * A #GLogFunc which uses our global @log_settings.
 */
static void
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer user_data)
{
  g_autofree gchar *timestamp_prefix = NULL;

  if (log_settings.flags & SRT_LOG_FLAGS_TIMESTAMP)
    {
      g_autoptr(GDateTime) date_time = g_date_time_new_now_local ();
      /* We can't use the format specifier "%f" for microseconds because it
       * was introduced in GLib 2.66 and we are targeting an older version */
      g_autofree gchar *timestamp = g_date_time_format (date_time, "%T");

      timestamp_prefix = g_strdup_printf ("%s.%06i: ", timestamp,
                                          g_date_time_get_microsecond (date_time));
    }

  g_printerr ("%s%s[%d]: %s: %s\n",
              (timestamp_prefix ?: ""),
              log_settings.prgname, log_settings.pid,
              get_level_prefix (log_level), message);

  if (log_level & (G_LOG_FLAG_RECURSION
                   | G_LOG_FLAG_FATAL
                   | G_LOG_LEVEL_ERROR))
    G_BREAKPOINT ();
}

/*
 * SrtLogFlags:
 * @SRT_LOG_FLAGS_DEBUG: Enable g_debug() messages
 * @SRT_LOG_FLAGS_INFO: Enable g_info() messages
 * @SRT_LOG_FLAGS_TIMESTAMP: Prefix log output with timestamps
 * @SRT_LOG_FLAGS_NONE: None of the above
 *
 * Flags affecting logging.
 */

static const GDebugKey log_enable[] =
{
  { "debug", SRT_LOG_FLAGS_DEBUG },
  { "info", SRT_LOG_FLAGS_INFO },
  { "timestamp", SRT_LOG_FLAGS_TIMESTAMP },
};

/*
 * _srt_util_set_glib_log_handler:
 * @extra_log_domain: (nullable): A log domain, usually %G_LOG_DOMAIN
 * @flags: Flags affecting logging
 *
 * Configure GLib to log to stderr with a message format suitable for
 * command-line programs, for example
 *
 * ```
 * my-program[123]: W: Resonance cascade scenario occurred
 * ```
 *
 * The chosen message format is used for @extra_log_domain, and also
 * for the `steam-runtime-tools` log domain used by the
 * steam-runtime-tools library.
 *
 * Logging can be configured by the `SRT_LOG` environment variable,
 * which is a sequence of tokens separated by colons, spaces or commas
 * matching the nicknames of #SrtLogFlags members. See
 * g_parse_debug_string().
 */
void
_srt_util_set_glib_log_handler (const char *extra_log_domain,
                                SrtLogFlags flags)
{
  GLogLevelFlags log_levels = (G_LOG_LEVEL_ERROR
                               | G_LOG_LEVEL_CRITICAL
                               | SRT_LOG_LEVEL_FAILURE
                               | G_LOG_LEVEL_WARNING
                               | G_LOG_LEVEL_MESSAGE);
  const char *log_env = g_getenv ("SRT_LOG");

  flags |= g_parse_debug_string (log_env, log_enable, G_N_ELEMENTS (log_enable));

  if (_srt_boolean_environment ("PRESSURE_VESSEL_LOG_WITH_TIMESTAMP", FALSE))
    flags |= SRT_LOG_FLAGS_TIMESTAMP;

  if (_srt_boolean_environment ("PRESSURE_VESSEL_LOG_INFO", FALSE))
    flags |= SRT_LOG_FLAGS_INFO;

  log_settings.flags = flags;
  log_settings.pid = getpid ();
  log_settings.prgname = g_get_prgname ();

  if (flags & SRT_LOG_FLAGS_INFO)
    log_levels |= G_LOG_LEVEL_INFO;

  if (flags & SRT_LOG_FLAGS_DEBUG)
    {
      log_levels |= G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO;
      _srt_profiling_enable ();
    }

  g_log_set_handler (extra_log_domain, log_levels, log_handler, NULL);
  g_log_set_handler (G_LOG_DOMAIN, log_levels, log_handler, NULL);
}
