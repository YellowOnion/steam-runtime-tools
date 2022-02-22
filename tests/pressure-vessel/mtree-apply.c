/*
 * Copyright © 2021 Collabora Ltd.
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

#include "config.h"

#include <locale.h>
#include <sysexits.h>

#include "libglnx/libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "mtree.h"
#include "utils.h"

static GOptionEntry options[] =
{
  { NULL }
};

int
main (int argc,
      char *argv[])
{
  glnx_autofd int fd = -1;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  PvMtreeApplyFlags flags = PV_MTREE_APPLY_FLAGS_NONE;
  int ret = EX_USAGE;

  setlocale (LC_ALL, "");
  _srt_setenv_disable_gio_modules ();

  context = g_option_context_new ("MTREE ROOT");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 3 || argc > 4)
    {
      g_printerr ("Usage: %s MTREE ROOT [SOURCE]\n", g_get_prgname ());
      goto out;
    }

  ret = EX_UNAVAILABLE;

  if (!glnx_opendirat (AT_FDCWD, argv[2], TRUE, &fd, error))
    goto out;

  if (!pv_mtree_apply (argv[1], argv[2], fd, argv[3], flags, error))
    goto out;

  ret = 0;

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  return ret;
}
