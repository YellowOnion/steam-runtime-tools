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

#include "steam-runtime-tools/utils-internal.h"

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
