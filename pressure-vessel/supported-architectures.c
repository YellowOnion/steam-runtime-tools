/*
 * Copyright © 2020-2022 Collabora Ltd.
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

#include "supported-architectures.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "steam-runtime-tools/architecture.h"
#include "libglnx.h"

/*
 * Supported Debian-style multiarch tuples
 */
const char * const pv_multiarch_tuples[PV_N_SUPPORTED_ARCHITECTURES + 1] =
{
  /* The conditional branches here need to be kept in sync with
   * pv_multiarch_details and PV_N_SUPPORTED_ARCHITECTURES */
#if defined(__i386__) || defined(__x86_64__)
  "x86_64-linux-gnu",
  "i386-linux-gnu",
#elif defined(__aarch64__)
  "aarch64-linux-gnu",
#elif defined(_SRT_MULTIARCH)
  _SRT_MULTIARCH,
#else
#error Architecture not supported by pressure-vessel
#endif
  NULL
};

/*
 * More details, in the same order as pv_multiarch_tuples
 */
const PvMultiarchDetails pv_multiarch_details[PV_N_SUPPORTED_ARCHITECTURES] =
{
  /* The conditional branches here need to be kept in sync with
   * pv_multiarch_tuples and PV_N_SUPPORTED_ARCHITECTURES */
#if defined(__i386__) || defined(__x86_64__)
  {
    .tuple = "x86_64-linux-gnu",
    .machine_type = SRT_MACHINE_TYPE_X86_64,
    .multilib = { "x86_64-pc-linux-gnu/lib", "lib64", NULL },
    .other_ld_so_cache = { "ld-x86_64-pc-linux-gnu.cache", NULL },
    .platforms = { "xeon_phi", "haswell", "x86_64", NULL },
    .gameoverlayrenderer_dir = "ubuntu12_64",
  },
  {
    .tuple = "i386-linux-gnu",
    .machine_type = SRT_MACHINE_TYPE_386,
    .multilib = { "i686-pc-linux-gnu/lib", "lib32", NULL },
    .other_ld_so_cache = { "ld-i686-pc-linux-gnu.cache", NULL },
    .platforms = { "i686", "i586", "i486", "i386", NULL },
    .gameoverlayrenderer_dir = "ubuntu12_32",
  },
#elif defined(__aarch64__)
  {
    .tuple = "aarch64-linux-gnu",
    .machine_type = SRT_MACHINE_TYPE_AARCH64,
    .multilib = { "aarch64-unknown-linux-gnueabi/lib", "lib64", NULL },
    .other_ld_so_cache = { "ld-aarch64-unknown-linux-gnueabi.cache", NULL },
    .platforms = { "aarch64", NULL },
  },
#elif defined(_SRT_MULTIARCH)
  {
    .tuple = _SRT_MULTIARCH,
    .machine_type = SRT_MACHINE_TYPE_UNKNOWN,
  },
#else
#error Architecture not supported by pressure-vessel
#endif
};

const char * const pv_multiarch_as_emulator_tuples[PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST + 1] =
{
  /* The conditional branches here need to be kept in sync with
   * pv_multiarch_as_emulator_details and
   * PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST */
#if defined(__i386__) || defined(__x86_64__)
  "aarch64-linux-gnu",
#else
  /* No supported architectures as emulator */
#endif
  NULL
};

const PvMultiarchDetails pv_multiarch_as_emulator_details[PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST] =
{
#if defined(__i386__) || defined(__x86_64__)
  {
    .tuple = "aarch64-linux-gnu",
    .machine_type = SRT_MACHINE_TYPE_AARCH64,
    .multilib = { "aarch64-unknown-linux-gnueabi/lib", "lib64", NULL },
    .other_ld_so_cache = { "ld-aarch64-unknown-linux-gnueabi.cache", NULL },
    .platforms = { "aarch64", NULL },
  },
#else
  /* No supported architectures as emulator */
#endif
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

/* Architecture-independent ld.so.conf filenames, other than the
 * conventional filename /etc/ld.so.conf used upstream and in Debian
 * (we assume this is also what's used in our runtimes). */
const char * const pv_other_ld_so_conf[] =
{
  /* Solus */
  "/usr/share/defaults/etc/ld.so.conf",
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

/*
 * Returns: %TRUE if @machine is included in the supported architectures list
 */
gboolean
pv_supported_architectures_include_machine_type (SrtMachineType machine)
{
  gsize i;

  if (machine == SRT_MACHINE_TYPE_UNKNOWN)
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (pv_multiarch_details); i++)
    {
      if (machine == pv_multiarch_details[i].machine_type)
        return TRUE;
    }

  return FALSE;
}
