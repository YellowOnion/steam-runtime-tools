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
 * SRT_LOG_LEVEL_WARNING:
 *
 * A log level for logging warnings that do not indicate a programming
 * error.
 *
 * Only use this in programs that have called
 * _srt_util_set_glib_log_handler().
 *
 * This is functionally equivalent to %G_LOG_LEVEL_MESSAGE, but
 * the handler set up by _srt_util_set_glib_log_handler() prints it
 * as though it was a warning. Use to log a warning that should not
 * cause program termination, even during unit testing.
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
typedef int (*type_of_sd_journal_send) (const char *, ...);
static type_of_sd_journal_send our_sd_journal_send = NULL;

static int
no_sd_journal_stream_fd (const char *identifier,
                         int priority,
                         int use_prefix)
{
  return -ENOSYS;
}

#define LOAD_OPTIONAL_SYMBOL(handle, name) \
  our_ ## name = (type_of_ ## name) dlsym (handle, #name);

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

      LOAD_OPTIONAL_SYMBOL (handle, sd_journal_send);

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

static struct
{
  int pid;
  const gchar *prgname;
  SrtLogFlags flags;
  /* Non-NULL if and only if stderr was set up to be the Journal */
  type_of_sd_journal_send journal_send;
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

  if (log_level & (SRT_LOG_LEVEL_WARNING | G_LOG_LEVEL_WARNING))
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
 * get_level_priority:
 * @log_level: A GLib log level, which should normally only have one bit set
 *
 * Returns: A syslog priority
 */
static int
get_level_priority (GLogLevelFlags log_level)
{
  if (log_level & (G_LOG_FLAG_RECURSION
                   | G_LOG_FLAG_FATAL
                   | G_LOG_LEVEL_ERROR
                   | SRT_LOG_LEVEL_FAILURE
                   | G_LOG_LEVEL_CRITICAL))
    return LOG_ERR;

  if (log_level & (SRT_LOG_LEVEL_WARNING | G_LOG_LEVEL_WARNING))
    return LOG_WARNING;

  if (log_level & G_LOG_LEVEL_MESSAGE)
    return LOG_NOTICE;

  if (log_level & G_LOG_LEVEL_INFO)
    return LOG_INFO;

  if (log_level & G_LOG_LEVEL_DEBUG)
    return LOG_DEBUG;

  return LOG_NOTICE;
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

  /* We only set this to be non-NULL if connecting to the Journal succeeded */
  if (log_settings.journal_send != NULL)
    {
      log_settings.journal_send ("GLIB_DOMAIN=%s", log_domain,
                                 "MESSAGE=%s: %s",
                                 get_level_prefix (log_level), message,
                                 "PRIORITY=%d", get_level_priority (log_level),
                                 "SYSLOG_IDENTIFIER=%s", log_settings.prgname,
                                 NULL);
    }

  if (log_settings.journal_send == NULL)
    {
      g_printerr ("%s%s[%d]: %s: %s\n",
                  (timestamp_prefix ?: ""),
                  log_settings.prgname, log_settings.pid,
                  get_level_prefix (log_level), message);
    }

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
 * @SRT_LOG_FLAGS_DIFFABLE: Try not to add information that reduces the
 *  ability to compare logs with `diff(1)`
 * @SRT_LOG_FLAGS_PID: Include process ID in logging, even
 *  if @SRT_LOG_FLAGS_DIFFABLE is set
 *  (enabled by default if @SRT_LOG_FLAGS_DIFFABLE is not set)
 * @SRT_LOG_FLAGS_TIMING: Emit timings for performance profiling
 *  (enabled by default if @SRT_LOG_FLAGS_DEBUG is set and
 *  @SRT_LOG_FLAGS_DIFFABLE is not)
 * @SRT_LOG_FLAGS_DIVERT_STDOUT: Make standard output a duplicate of
 *  standard error, to avoid unstructured diagnostics like g_debug()
 *  being written to the original standard output
 *  (use this in conjunction with the @original_stdout_out parameter of
 *  _srt_util_set_glib_log_handler())
 * @SRT_LOG_FLAGS_OPTIONALLY_JOURNAL: If standard output or standard error
 *  is /dev/null or an invalid file descriptor, or if the user requests
 *  logging to the Journal via environment variables, automatically
 *  enable %SRT_LOG_FLAGS_JOURNAL.
 * @SRT_LOG_FLAGS_JOURNAL: Try to write log messages to the systemd
 *  Journal, and redirect standard output and standard error there.
 * @SRT_LOG_FLAGS_NONE: None of the above
 *
 * Flags affecting logging.
 */

static const GDebugKey log_enable[] =
{
  { "debug", SRT_LOG_FLAGS_DEBUG },
  { "info", SRT_LOG_FLAGS_INFO },
  { "timestamp", SRT_LOG_FLAGS_TIMESTAMP },
  { "diffable", SRT_LOG_FLAGS_DIFFABLE },
  { "pid", SRT_LOG_FLAGS_PID },
  { "timing", SRT_LOG_FLAGS_TIMING },

  /* Intentionally no way to set
   * - SRT_LOG_FLAGS_DIVERT_STDOUT
   * - SRT_LOG_FLAGS_OPTIONALLY_JOURNAL
   * via $SRT_LOG: implementing those flags correctly requires the application
   * to be aware that the original stdout might get altered */

  /* Order matters: _srt_util_set_glib_log_handler() relies on this being
   * the last one, so that it can be disabled in the absence of
   * SRT_LOG_FLAGS_OPTIONALLY_JOURNAL */
  { "journal", SRT_LOG_FLAGS_JOURNAL },
};

/*
 * ensure_fd_not_cloexec:
 *
 * Ensure that @fd is open and not marked for close-on-execute, to avoid
 * weird side-effects if opening an unrelated file descriptor ends up as
 * one of the three standard fds, either in this process or a subprocess.
 */
static gboolean
ensure_fd_not_cloexec (int fd,
                       GError **error)
{
  int flags;
  int errsv;

  flags = fcntl (fd, F_GETFD, 0);

  if (G_UNLIKELY (flags < 0 && errno == EBADF))
    {
      int new_fd;

      /* Unusually, intentionally no O_CLOEXEC here */
      if (fd == STDIN_FILENO)
        new_fd = open ("/dev/null", O_RDONLY | O_NOCTTY);
      else
        new_fd = open ("/dev/null", O_WRONLY | O_NOCTTY);

      if (new_fd < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to open /dev/null to replace fd %d",
                                        fd);

      if (new_fd != fd)
        {
          if (dup2 (new_fd, fd) != fd)
            {
              errsv = errno;
              close (new_fd);
              errno = errsv;
              return glnx_throw_errno_prefix (error,
                                              "Unable to make fd %d a copy of fd %d",
                                              fd, new_fd);
            }

          close (new_fd);
        }

      return TRUE;
    }

  if (G_UNLIKELY (flags < 0))
    return glnx_throw_errno_prefix (error,
                                    "Unable to get flags of fd %d", fd);

  if (flags & FD_CLOEXEC)
    {
      if (fcntl (fd, F_SETFD, flags & ~FD_CLOEXEC) < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to make fd %d stay open on exec",
                                        fd);
    }

  return TRUE;
}

/*
 * set_up_output:
 * @fd: `STDOUT_FILENO` or `STDERR_FILENO`
 * @save_original: (out): If not %NULL, duplicate the original stdout/stderr
 *  here
 */
static gboolean
set_up_output (int fd,
               int *save_original,
               GError **error)
{
  SrtLogFlags required_flags;
  gboolean use_journal = FALSE;

  if (save_original != NULL)
    {
      *save_original = TEMP_FAILURE_RETRY (fcntl (fd, F_DUPFD_CLOEXEC, 3));

      /* Ignore EBADF because there's no guarantee that the standard fds
       * are open yet */
      if (*save_original < 0 && errno != EBADF)
        return glnx_throw_errno_prefix (error,
                                        "Unable to duplicate fd %d", fd);
    }

  if (!ensure_fd_not_cloexec (fd, error))
    return FALSE;

  if (fd == STDOUT_FILENO)
    required_flags = SRT_LOG_FLAGS_JOURNAL | SRT_LOG_FLAGS_DIVERT_STDOUT;
  else
    required_flags = SRT_LOG_FLAGS_JOURNAL;

  if ((log_settings.flags & required_flags) == required_flags)
    use_journal = TRUE;

  /* If it's already pointing to the Journal, open a new Journal stream so
   * that a parent process doesn't get "blamed" for messages that we emit. */
  if (g_log_writer_is_journald (fd))
    use_journal = TRUE;

  /* If it's /dev/null, replace it with the Journal if requested.
   * No need to do this check if we're going to replace it with the
   * Journal anyway. */
  if ((log_settings.flags & SRT_LOG_FLAGS_OPTIONALLY_JOURNAL) && !use_journal)
    {
      struct stat stat_buf = {};

      if (fstat (fd, &stat_buf) != 0)
        {
          _srt_log_warning ("Unable to stat fd %d: %s",
                            fd, g_strerror (errno));
        }
      else if (st_buf_is_dev_null (&stat_buf))
        {
          use_journal = TRUE;
        }
    }

  if (use_journal)
    {
      g_autoptr(GError) local_error = NULL;
      int priority;

      if (fd == STDERR_FILENO)
        {
          g_info ("Redirecting logging and stderr to systemd journal");
          priority = LOG_NOTICE;
        }
      else
        {
          g_info ("Redirecting stdout to systemd journal");
          priority = LOG_INFO;
        }

      /* Unstructured text on stdout/stderr becomes unstructured messages */
      if (_srt_stdio_to_journal (log_settings.prgname, fd, priority, &local_error))
        {
          /* No need to redirect stdout to stderr if stdout is already a
           * separate Journal stream */
          if (fd == STDOUT_FILENO)
            log_settings.flags &= ~SRT_LOG_FLAGS_DIVERT_STDOUT;

          /* Structured GLib logging on stderr becomes structured messages */
          if (fd == STDERR_FILENO)
            {
              log_settings.flags |= SRT_LOG_FLAGS_JOURNAL;
              log_settings.journal_send = our_sd_journal_send;
            }
        }
      else
        {
          if (fd == STDERR_FILENO)
            {
              log_settings.flags &= ~SRT_LOG_FLAGS_JOURNAL;
            }

          /* Just emit a warning instead of failing: this can legitimately
           * fail on systems that don't use the systemd Journal. */
          _srt_log_warning ("%s", local_error->message);
          g_clear_error (&local_error);
        }
    }

  return TRUE;
}

gboolean
_srt_util_restore_saved_fd (int saved_fd,
                            int target_fd,
                            GError **error)
{
  g_return_val_if_fail (saved_fd >= 0, FALSE);

  if (saved_fd == target_fd)
    return TRUE;

  if (dup2 (saved_fd, target_fd) != target_fd)
    return glnx_throw_errno_prefix (error,
                                    "Unable to make fd %d a copy of fd %d",
                                    target_fd, saved_fd);

  if (target_fd > STDERR_FILENO)
    {
      int flags = fcntl (target_fd, F_GETFD, 0);

      if (flags < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to get flags of fd %d",
                                        target_fd);

      fcntl (target_fd, F_SETFD, flags|FD_CLOEXEC);
    }

  return TRUE;
}

/*
 * _srt_util_set_glib_log_handler:
 * @prgname: (nullable): Passed to g_set_prgname() if not %NULL
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
gboolean
_srt_util_set_glib_log_handler (const char *prgname,
                                const char *extra_log_domain,
                                SrtLogFlags flags,
                                int *original_stdout_out,
                                int *original_stderr_out,
                                GError **error)
{
  GLogLevelFlags log_levels = (G_LOG_LEVEL_ERROR
                               | G_LOG_LEVEL_CRITICAL
                               | SRT_LOG_LEVEL_FAILURE
                               | SRT_LOG_LEVEL_WARNING
                               | G_LOG_LEVEL_WARNING
                               | G_LOG_LEVEL_MESSAGE);
  const char *log_env = g_getenv ("SRT_LOG");
  gsize log_env_n_keys = G_N_ELEMENTS (log_enable);

  if (prgname != NULL)
    g_set_prgname (prgname);

  if (flags & SRT_LOG_FLAGS_OPTIONALLY_JOURNAL)
    {
      /* Some CLI tools accepted this as an alternative to SRT_LOG=journal,
       * so check both */
      if (_srt_boolean_environment ("SRT_LOG_TO_JOURNAL", FALSE))
        flags |= SRT_LOG_FLAGS_JOURNAL;
    }
  else
    {
      /* Don't allow SRT_LOG=journal to take effect if the application
       * was not expecting it */
      log_env_n_keys--;
    }

  flags |= g_parse_debug_string (log_env, log_enable, log_env_n_keys);

  /* Specifically setting SRT_LOG_TO_JOURNAL=0 does the opposite */
  if (!_srt_boolean_environment ("SRT_LOG_TO_JOURNAL", TRUE))
    flags &= ~SRT_LOG_FLAGS_JOURNAL;

  if (_srt_boolean_environment ("PRESSURE_VESSEL_LOG_WITH_TIMESTAMP", FALSE))
    flags |= SRT_LOG_FLAGS_TIMESTAMP;

  if (_srt_boolean_environment ("PRESSURE_VESSEL_LOG_INFO", FALSE))
    flags |= SRT_LOG_FLAGS_INFO;

  log_settings.flags = flags;
  log_settings.prgname = g_get_prgname ();

  if ((flags & SRT_LOG_FLAGS_PID)
      || !(flags & SRT_LOG_FLAGS_DIFFABLE))
    log_settings.pid = getpid ();
  else
    log_settings.pid = 0;

  if (flags & SRT_LOG_FLAGS_INFO)
    log_levels |= G_LOG_LEVEL_INFO;

  if (flags & SRT_LOG_FLAGS_DEBUG)
    log_levels |= G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO;

  g_log_set_handler (extra_log_domain, log_levels, log_handler, NULL);
  g_log_set_handler (G_LOG_DOMAIN, log_levels, log_handler, NULL);

  if ((flags & SRT_LOG_FLAGS_TIMING)
      || ((flags & SRT_LOG_FLAGS_DEBUG)
          && !(flags & SRT_LOG_FLAGS_DIFFABLE)))
    _srt_profiling_enable ();

  /* We ensure stdin is open first, because otherwise any fd we open is
   * likely to become unintentionally the new stdin. */
  if (!ensure_fd_not_cloexec (STDIN_FILENO, error)
      || !set_up_output (STDOUT_FILENO, original_stdout_out, error)
      || !set_up_output (STDERR_FILENO, original_stderr_out, error))
    return FALSE;

  if (log_settings.flags & SRT_LOG_FLAGS_DIVERT_STDOUT)
    {
      /* Unusually, intentionally not setting FD_CLOEXEC here */
      if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
        return glnx_throw_errno_prefix (error,
                                        "Unable to make fd %d a copy of fd %d",
                                        STDOUT_FILENO, STDERR_FILENO);
    }

  return TRUE;
}
