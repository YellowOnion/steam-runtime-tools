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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "subprojects/libglnx/config.h"

#include "supported-architectures.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

/*
 * Supported Debian-style multiarch tuples
 */
const char * const pv_multiarch_tuples[PV_N_SUPPORTED_ARCHITECTURES + 1] =
{
  "x86_64-linux-gnu",
  "i386-linux-gnu",
  NULL
};

/*
 * More details, in the same order as pv_multiarch_tuples
 */
const PvMultiarchDetails pv_multiarch_details[PV_N_SUPPORTED_ARCHITECTURES] =
{
  {
    .tuple = "x86_64-linux-gnu",
    .multilib = { "x86_64-pc-linux-gnu/lib", "lib64", NULL },
    .other_ld_so_cache = { "ld-x86_64-pc-linux-gnu.cache", NULL },
    .platforms = { "xeon_phi", "haswell", "x86_64", NULL },
    .gameoverlayrenderer_dir = "ubuntu12_64",
  },
  {
    .tuple = "i386-linux-gnu",
    .multilib = { "i686-pc-linux-gnu/lib", "lib32", NULL },
    .other_ld_so_cache = { "ld-i686-pc-linux-gnu.cache", NULL },
    .platforms = { "i686", "i586", "i486", "i386", NULL },
    .gameoverlayrenderer_dir = "ubuntu12_32",
  },
};

/* Architecture-independent ld.so.cache filenames, other than the
 * conventional filename /etc/ld.so.cache used upstream and in Debian
 * (we assume this is also what's used in our runtimes). */
const char * const pv_other_ld_so_cache[] =
{
  /* Clear Linux */
  "/var/cache/ldconfig/ld.so.cache",
  NULL
};

/*
 * Get the library directories associated with @self, most important or
 * unambiguous first.
 *
 * Returns: (transfer container) (element-type filename):
 */
GPtrArray *
pv_multiarch_details_get_libdirs (const PvMultiarchDetails *self,
                                  PvMultiarchLibdirsFlags flags)
{
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  gsize j;

  /* Multiarch is the least ambiguous so we put it first.
   *
   * We historically searched /usr/lib before /lib, but Debian actually
   * does the opposite, and we follow that here.
   *
   * Arguably we should search /usr/local/lib before /lib before /usr/lib,
   * but we don't currently try /usr/local/lib. We could add a flag
   * for that if we don't want to do it unconditionally. */
  g_ptr_array_add (dirs,
                   g_build_filename ("/lib", self->tuple, NULL));
  g_ptr_array_add (dirs,
                   g_build_filename ("/usr", "lib", self->tuple, NULL));

  if (flags & PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN)
    g_ptr_array_add (dirs,
                     g_build_filename ("/usr", "lib", self->tuple, "mesa",
                                       NULL));

  /* Try other multilib variants next. This includes
   * Exherbo/cross-compilation-style per-architecture prefixes,
   * Red-Hat-style lib64 and Arch-style lib32. */
  for (j = 0; j < G_N_ELEMENTS (self->multilib); j++)
    {
      if (self->multilib[j] == NULL)
        break;

      g_ptr_array_add (dirs,
                       g_build_filename ("/", self->multilib[j], NULL));
      g_ptr_array_add (dirs,
                       g_build_filename ("/usr", self->multilib[j], NULL));
    }

  /* /lib and /usr/lib are lowest priority because they're the most
   * ambiguous: we don't know whether they're meant to contain 32- or
   * 64-bit libraries. */
  g_ptr_array_add (dirs, g_strdup ("/lib"));
  g_ptr_array_add (dirs, g_strdup ("/usr/lib"));

  return g_steal_pointer (&dirs);
}
