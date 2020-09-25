/*
 * adverb — Run a child process with various environmental adjustments.
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

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sysexits.h>

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/glib-backports-internal.h"

static gboolean opt_print_version = FALSE;
static gboolean opt_ignore_sigchld = FALSE;
static gboolean opt_unignore_sigchld = FALSE;
static gboolean opt_block_sigchld = FALSE;
static gboolean opt_unblock_sigchld = FALSE;
static gboolean opt_show_signal_dispositions = FALSE;

static GOptionEntry options[] =
{
  { "show-signals", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_show_signal_dispositions,
    "Show signal dispositions", NULL },
  { "ignore-sigchld", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_ignore_sigchld,
    "Ignore SIGCHLD with SIG_IGN", NULL },
  { "unignore-sigchld", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_unignore_sigchld,
    "Don't ignore SIGCHLD (restore default disposition SIG_DFL)", NULL },
  { "block-sigchld", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_block_sigchld,
    "Block SIGCHLD with sigprocmask()", NULL },
  { "unblock-sigchld", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_unblock_sigchld,
    "Unblock SIGCHLD with sigprocmask()", NULL },

  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { NULL }
};

/* Return the original stdout fd. */
static int
divert_stdout_to_stderr (GError **error)
{
  int original_stdout_fd;

  /* Duplicate the original stdout so that we still have a way to write
   * machine-readable output. */
  original_stdout_fd = dup (STDOUT_FILENO);

  if (original_stdout_fd < 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Unable to duplicate fd %d: %s",
                   STDOUT_FILENO, g_strerror (saved_errno));
      return -1;
    }

  /* If something like g_debug writes to stdout, make it come out of
   * our original stderr. */
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    {
      int saved_errno = errno;

      close (original_stdout_fd);
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Unable to make fd %d a copy of fd %d: %s",
                   STDOUT_FILENO, STDERR_FILENO, g_strerror (saved_errno));
      return -1;
    }

  return original_stdout_fd;
}

static gboolean
put_back_original_stdout (int original_stdout_fd,
                          GError **error)
{
  if (dup2 (original_stdout_fd, STDOUT_FILENO) != STDOUT_FILENO)
    {
      int saved_errno = errno;

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Unable to make fd %d a copy of fd %d: %s",
                   STDOUT_FILENO, original_stdout_fd,
                   g_strerror (saved_errno));
      return FALSE;
    }

  return TRUE;
}

int
main (int argc,
      char *argv[])
{
  GError *local_error = NULL;
  int original_stdout_fd;
  int ret = EX_USAGE;
  char **command_and_args;
  int saved_errno;
  GOptionContext *context;

  context = g_option_context_new (
      "[COMMAND [ARG...]]\n"
      "Run COMMAND [ARG...] with environmental adjustments.\n"
      "If no COMMAND is given, just print current status.\n");

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &local_error))
    {
      if (local_error->domain == G_OPTION_ERROR)
        ret = EX_USAGE;
      else
        ret = EX_UNAVAILABLE;

      goto out;
    }

  if (opt_print_version)
    {
      g_print (
          "%s:\n"
          " Package: steam-runtime-tools\n"
          " Version: %s\n",
          argv[0], VERSION);
      ret = 0;
      goto out;
    }

  original_stdout_fd = divert_stdout_to_stderr (&local_error);

  if (original_stdout_fd < 0)
    {
      ret = EX_OSERR;
      goto out;
    }

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    command_and_args = NULL;
  else
    command_and_args = argv + 1;

  if (opt_block_sigchld && opt_unblock_sigchld)
    {
      g_printerr ("%s: Cannot both block and unblock SIGCHLD",
                  g_get_prgname ());
      goto out;
    }

  if (opt_ignore_sigchld && opt_unignore_sigchld)
    {
      g_printerr ("%s: Cannot both ignore and unignore SIGCHLD",
                  g_get_prgname ());
      goto out;
    }

  ret = EX_OSERR;

  if (opt_block_sigchld || opt_unblock_sigchld)
    {
      int sig;
      int how;
      sigset_t old_set;
      sigset_t new_set;

      sigemptyset (&old_set);
      sigemptyset (&new_set);
      sigaddset (&new_set, SIGCHLD);

      if (opt_block_sigchld)
        {
          how = SIG_BLOCK;
          g_message ("Blocking SIGCHLD");
        }
      else
        {
          how = SIG_UNBLOCK;
          g_message ("Unblocking SIGCHLD");
        }

      if (sigprocmask (how, NULL, &old_set) != 0)
        {
          saved_errno = errno;
          g_set_error (&local_error, G_IO_ERROR,
                       g_io_error_from_errno (saved_errno),
                       "get sigprocmask: %s", g_strerror (saved_errno));
          goto out;
        }

      for (sig = 1; sig < 64; sig++)
        {
          /* sigismember returns -1 for non-signals, which we ignore */
          if (sigismember (&old_set, sig) == 1)
            g_message ("Before: signal %d (%s) blocked", sig, g_strsignal (sig));
        }

      if (sigprocmask (how, &new_set, &old_set) != 0)
        {
          saved_errno = errno;
          g_set_error (&local_error, G_IO_ERROR,
                       g_io_error_from_errno (saved_errno),
                       "set sigprocmask: %s", g_strerror (saved_errno));
          goto out;
        }

      if (sigprocmask (how, NULL, &new_set) != 0)
        {
          saved_errno = errno;
          g_set_error (&local_error, G_IO_ERROR,
                       g_io_error_from_errno (saved_errno),
                       "get sigprocmask: %s", g_strerror (saved_errno));
          goto out;
        }

      for (sig = 1; sig < 64; sig++)
        {
          if (sigismember (&new_set, sig) == 1)
            g_message ("After: signal %d (%s) blocked", sig, g_strsignal (sig));
        }
    }

  if (opt_ignore_sigchld || opt_unignore_sigchld)
    {
      struct sigaction action = { .sa_handler = SIG_DFL };
      struct sigaction old_action = { .sa_handler = SIG_DFL };

      if (opt_ignore_sigchld)
        {
          g_message ("Ignoring SIGCHLD");
          action.sa_handler = SIG_IGN;
        }
      else
        {
          g_message ("Unignoring SIGCHLD");
          action.sa_handler = SIG_DFL;
        }

      if (sigaction (SIGCHLD, &action, &old_action) != 0)
        {
          saved_errno = errno;
          g_set_error (&local_error, G_IO_ERROR,
                       g_io_error_from_errno (saved_errno),
                       "sigaction: %s", g_strerror (saved_errno));
          goto out;
        }

      g_message ("SIG_DFL:            %p", (void *) SIG_DFL);
      g_message ("SIG_IGN:            %p", (void *) SIG_IGN);
      g_message ("Old signal handler: %p", old_action.sa_handler);
      g_message ("Old flags:          0x%x", old_action.sa_flags);
      g_message ("New signal handler: %p", action.sa_handler);
      g_message ("New flags:          0x%x", action.sa_flags);
    }

  if (opt_show_signal_dispositions)
    {
      int sig;
      sigset_t set;

      sigemptyset (&set);

      if (sigprocmask (SIG_UNBLOCK, NULL, &set) != 0)
        {
          saved_errno = errno;
          g_set_error (&local_error, G_IO_ERROR,
                       g_io_error_from_errno (saved_errno),
                       "get sigprocmask: %s", g_strerror (saved_errno));
          goto out;
        }

      for (sig = 1; sig < 64; sig++)
        {
          if (sigismember (&set, sig) == 1)
            g_message ("Signal %s (%d) is blocked", g_strsignal (sig), sig);
        }

      sigfillset (&set);

      for (sig = 1; sig < 64; sig++)
        {
          if (sigismember (&set, sig) == 1)
            {
              int masked;
              struct sigaction action = { .sa_handler = SIG_DFL };

              if (sigaction (sig, NULL, &action) != 0)
                {
                  saved_errno = errno;
                  g_set_error (&local_error, G_IO_ERROR,
                               g_io_error_from_errno (saved_errno),
                               "get sigaction: %s", g_strerror (saved_errno));
                  goto out;
                }

              if (action.sa_handler != SIG_DFL || action.sa_flags != 0)
                {
                  g_message ("Signal %s (%d) handler: %p",
                             g_strsignal (sig), sig,
                             action.sa_handler);
                  g_message ("Signal %s (%d) flags: 0x%x",
                             g_strsignal (sig), sig,
                             action.sa_flags);
                }

              for (masked = 1; masked < 64; masked++)
                {
                  if (sigismember (&action.sa_mask, masked) == 1)
                    {
                      g_message ("Signal %s (%d) blocks signal %s (%d)",
                                 g_strsignal (sig), sig,
                                 g_strsignal (masked), masked);
                    }
                }
            }
        }
    }

  if (!put_back_original_stdout (original_stdout_fd, &local_error))
    goto out;

  if (command_and_args == NULL)
    {
      ret = EX_OK;
    }
  else
    {
      /* Doesn't return if successful */
      execvp (command_and_args[0], command_and_args);

      saved_errno = errno;
      g_set_error (&local_error, G_IO_ERROR,
                   g_io_error_from_errno (saved_errno),
                   "execvp %s: %s",
                   command_and_args[0], g_strerror (saved_errno));
    }

out:
  g_option_context_free (context);

  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  g_clear_error (&local_error);
  return ret;
}
