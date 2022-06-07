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

#pragma once

#include <glib.h>

#if defined(__i386__) || defined(__x86_64__)
#define PV_N_SUPPORTED_ARCHITECTURES 2
#else
/* x86 is a special case where we have a biarch setup. For all the other
 * cases we expect a single supported architecture */
#define PV_N_SUPPORTED_ARCHITECTURES 1
#endif

extern const char * const pv_multiarch_tuples[PV_N_SUPPORTED_ARCHITECTURES + 1];

typedef struct
{
  const char *tuple;

  /* Directories other than /usr/lib that we must search for loadable
   * modules, least-ambiguous first, most-ambiguous last, not including
   * Debian-style multiarch directories which are automatically derived
   * from @tuple.
   * - Exherbo <GNU-tuple>/lib
   * - Red-Hat- or Arch-style lib<QUAL>
   * - etc.
   * Size is completely arbitrary, expand as needed */
  const char *multilib[3];

  /* Alternative paths for ld.so.cache, other than ld.so.cache itself.
   * Size is completely arbitrary, expand as needed */
  const char *other_ld_so_cache[2];

  /* Known values that ${PLATFORM} can expand to.
   * Refer to sysdeps/x86/cpu-features.c and sysdeps/x86/dl-procinfo.c
   * in glibc.
   * Size is completely arbitrary, expand as needed */
  const char *platforms[5];

  /* Directory used in Steam for gameoverlayrenderer.so. */
  const char *gameoverlayrenderer_dir;
} PvMultiarchDetails;

extern const PvMultiarchDetails pv_multiarch_details[PV_N_SUPPORTED_ARCHITECTURES];

extern const char * const pv_other_ld_so_cache[];
extern const char * const pv_other_ld_so_conf[];

/*
 * PvMultiarchLibdirsFlags:
 * @PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN:
 *  Return all library directories from which we might need to delete
 *  overridden libraries shipped in the runtime.
 */
typedef enum
{
  PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN = (1 << 0),
  PV_MULTIARCH_LIBDIRS_FLAGS_NONE = 0
} PvMultiarchLibdirsFlags;

GPtrArray *pv_multiarch_details_get_libdirs (const PvMultiarchDetails *self,
                                             PvMultiarchLibdirsFlags flags);
