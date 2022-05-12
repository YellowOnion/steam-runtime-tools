/*
 * Copyright © 2019-2022 Collabora Ltd.
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
#include "per-arch-dirs.h"

#include <glib/gstdio.h>

#include "steam-runtime-tools/system-info.h"
#include "steam-runtime-tools/utils-internal.h"

void
pv_per_arch_dirs_free (PvPerArchDirs *self)
{
  gsize abi;

  if (self->root_path != NULL)
    _srt_rm_rf (self->root_path);

  g_clear_pointer (&self->root_path, g_free);
  g_clear_pointer (&self->libdl_token_path, g_free);

  for (abi = 0; abi < G_N_ELEMENTS (self->abi_paths); abi++)
    g_clear_pointer (&self->abi_paths[abi], g_free);

  g_free (self);
}

PvPerArchDirs *
pv_per_arch_dirs_new (GError **error)
{
  g_autoptr(PvPerArchDirs) self = g_new0 (PvPerArchDirs, 1);
  g_autoptr(SrtSystemInfo) info = NULL;
  gsize abi;

  info = srt_system_info_new (NULL);

  self->root_path = g_dir_make_tmp ("pressure-vessel-libs-XXXXXX", error);
  if (self->root_path == NULL)
    return glnx_prefix_error_null (error,
                                   "Cannot create temporary directory for platform specific libraries");

  self->libdl_token_path = g_build_filename (self->root_path,
                                             "${PLATFORM}", NULL);

  for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
    {
      g_autofree gchar *libdl_platform = NULL;
      g_autofree gchar *abi_path = NULL;

      if (g_getenv ("PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM") != NULL)
        {
          /* In unit tests it isn't straightforward to find the real
           * ${PLATFORM}, so we use a predictable mock implementation:
           * for x86 we use whichever platform happens to be listed first
           * and for all the other cases we simply use "mock". */
#if defined(__i386__) || defined(__x86_64__)
          libdl_platform = g_strdup (pv_multiarch_details[abi].platforms[0]);
#else
          libdl_platform = g_strdup("mock");
#endif
        }
      else
        {
          libdl_platform = srt_system_info_dup_libdl_platform (info,
                                                               pv_multiarch_details[abi].tuple,
                                                               error);
          if (!libdl_platform)
            return glnx_prefix_error_null (error,
                                           "Unknown expansion of the dl string token $PLATFORM");
        }

      abi_path = g_build_filename (self->root_path, libdl_platform, NULL);

      if (g_mkdir (abi_path, 0700) != 0)
        return glnx_null_throw_errno_prefix (error, "Unable to create \"%s\"", abi_path);

      self->abi_paths[abi] = g_steal_pointer (&abi_path);
    }

  return g_steal_pointer (&self);
}
