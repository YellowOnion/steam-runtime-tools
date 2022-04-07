/*
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <locale.h>
#include <sysexits.h>

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "tree-copy.h"
#include "utils.h"

static gboolean opt_expect_hard_links = FALSE;
static gboolean opt_usrmerge = FALSE;

static GOptionEntry options[] =
{
  { "expect-hard-links", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_expect_hard_links,
    "Show a warning if we can't use hard-links.",
    NULL },

  { "usrmerge", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_usrmerge,
    "Assume SOURCE is a sysroot, and carry out the /usr merge in DEST.",
    NULL },

  { NULL }
};

int
main (int argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  PvCopyFlags flags = PV_COPY_FLAGS_NONE;
  int ret = EX_USAGE;

  setlocale (LC_ALL, "");
  _srt_setenv_disable_gio_modules ();

  context = g_option_context_new ("SOURCE DEST");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc != 3)
    {
      g_printerr ("Usage: %s SOURCE DEST\n", g_get_prgname ());
      goto out;
    }

  ret = EX_UNAVAILABLE;

  if (opt_usrmerge)
    flags |= PV_COPY_FLAGS_USRMERGE;

  if (opt_expect_hard_links)
    flags |= PV_COPY_FLAGS_EXPECT_HARD_LINKS;

  if (!pv_cheap_tree_copy (argv[1], argv[2], flags, error))
    goto out;

  ret = 0;

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  return ret;
}
