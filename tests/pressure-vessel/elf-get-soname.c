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
#include "subprojects/libglnx/config.h"

#include <locale.h>
#include <sysexits.h>

#include "libglnx/libglnx.h"

#include "glib-backports.h"

#include "elf-utils.h"
#include "utils.h"

static GOptionEntry options[] =
{
  { NULL }
};

int
main (int argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = EX_USAGE;
  int i;

  setlocale (LC_ALL, "");
  pv_avoid_gvfs ();

  context = g_option_context_new ("LIBRARY...");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      g_printerr ("A library to open is required\n");
      goto out;
    }

  ret = 0;

  for (i = 1; i < argc; i++)
    {
      int fd = open (argv[i], O_RDONLY | O_CLOEXEC);
      g_autoptr(Elf) elf = NULL;
      g_autofree gchar *soname = NULL;

      if (fd < 0)
        {
          g_printerr ("Cannot open %s: %s\n", argv[i], g_strerror (errno));
          ret = 1;
          continue;
        }

      elf = pv_elf_open_fd (fd, error);

      if (elf == NULL)
        {
          g_printerr ("Unable to open %s: %s\n",
                      argv[i], local_error->message);
          g_clear_error (error);
          ret = 1;
          continue;
        }

      soname = pv_elf_get_soname (elf, error);

      if (soname == NULL)
        {
          g_printerr ("Unable to get SONAME of %s: %s\n",
                      argv[i], local_error->message);
          g_clear_error (error);
          ret = 1;
          continue;
        }

      g_print ("%s DT_SONAME: %s\n", argv[i], soname);
    }

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  return ret;
}
