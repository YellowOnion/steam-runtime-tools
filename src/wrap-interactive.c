/*
 * Copyright © 2017-2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "wrap-interactive.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gstdio.h>

void
pv_bwrap_wrap_in_xterm (FlatpakBwrap *wrapped_command)
{
  g_return_if_fail (wrapped_command != NULL);

  flatpak_bwrap_add_args (wrapped_command,
                          "xterm", "-e",
                          /* Original command will go here and become
                           * the argv of xterm -e */
                          NULL);
}

void
pv_bwrap_wrap_interactive (FlatpakBwrap *wrapped_command,
                           PvShell shell)
{
  static const char preamble[] =
    {
      "prgname=\"$1\"\n"
      "shift\n"
    };
  const char *script = "";
  static const char start_shell[] =
    {
      "echo\n"
      "echo\n"
      "echo\n"
      "echo \"$1: Starting interactive shell (original command is in "
      "\\\"\\$@\\\")\"\n"
      "echo\n"
      "echo\n"
      "echo\n"
      "exec bash -i -s \"$@\"\n"
    };
  gchar *command;

  g_return_if_fail (wrapped_command != NULL);

  switch (shell)
    {
      case PV_SHELL_NONE:
        script =
          "e=0\n"
          "\"$@\" || e=$?\n"
          "echo\n"
          "echo \"Press Enter or ^D to continue...\"\n"
          "read reply || true\n"
          "exit \"$e\"\n";
        break;

      case PV_SHELL_AFTER:
        script =
          "e=0\n"
          "\"$@\" || e=$?\n";
        break;

      case PV_SHELL_FAIL:
        script =
          "if \"$@\"; then exit 0; else e=\"$?\"; fi\n"
          "echo \"$1: command exit status $e\"\n";
        break;

      case PV_SHELL_INSTEAD:
        break;

      default:
        g_return_if_reached ();
    }

  command = g_strdup_printf ("%s%s%s",
                             preamble,
                             script,
                             shell == PV_SHELL_NONE ? "" : start_shell);

  flatpak_bwrap_add_args (wrapped_command,
                          "sh", "-euc",
                          command,
                          "sh",   /* $0 for sh */
                          g_get_prgname (),   /* $1 for sh */
                          /* Original command will go here and become
                           * the argv of command, and eventually
                           * the argv of bash -i -s */
                          NULL);
}

gboolean
pv_bwrap_wrap_tty (FlatpakBwrap *wrapped_command,
                   GError **error)
{
  int fd;

  g_return_val_if_fail (wrapped_command != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fflush (stdout);
  fflush (stderr);

  fd = open ("/dev/tty", O_RDONLY);

  if (fd < 0)
    return glnx_throw_errno_prefix (error,
                                    "Cannot open /dev/tty for reading");

  if (dup2 (fd, 0) < 0)
    return glnx_throw_errno_prefix (error, "Cannot use /dev/tty as stdin");

  g_close (fd, NULL);

  fd = open ("/dev/tty", O_WRONLY);

  if (fd < 0)
    return glnx_throw_errno_prefix (error, "Cannot open /dev/tty for writing");

  if (dup2 (fd, 1) < 0)
    return glnx_throw_errno_prefix (error, "Cannot use /dev/tty as stdout");

  if (dup2 (fd, 2) < 0)
    return glnx_throw_errno_prefix (error, "Cannot use /dev/tty as stderr");

  g_close (fd, NULL);
  return TRUE;
}
