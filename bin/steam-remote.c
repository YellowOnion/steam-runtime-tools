/*
 * Copyright © 2021 Collabora Ltd.
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
 * Stub Steam executable that tries to directly pass the given commands to
 * the running Steam client.
 */

#include <libglnx.h>

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/log-internal.h>
#include <steam-runtime-tools/utils-internal.h>

int
main (int argc,
      const char **argv)
{
  static const char *default_argv[] = {"steam", "-foreground", NULL};
  g_autoptr(GError) error = NULL;

  _srt_util_set_glib_log_handler ("steam-runtime-steam-remote",
                                  G_LOG_DOMAIN,
                                  SRT_LOG_FLAGS_OPTIONALLY_JOURNAL,
                                  NULL, NULL, NULL);

  if (argc <= 1)
    {
      /* If we don't have arguments, Steam is expected to send the command
       * "-foreground" by default */
      argv = default_argv;
      argc = 2;
    }

  if (!_srt_steam_command_via_pipe (argv + 1, argc - 1, &error))
    {
      g_printerr ("steam-runtime-steam-remote: %s\n", error->message);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
