/*
 * A cut-down version of common/flatpak-utils from Flatpak
 *
 * Copyright © 2014 Red Hat, Inc
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
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#ifndef __FLATPAK_UTILS_H__
#define __FLATPAK_UTILS_H__

#include "libglnx/libglnx.h"
#include <flatpak-common-types-private.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include "flatpak-context-private.h"
#include "flatpak-error.h"

#define AUTOFS_SUPER_MAGIC 0x0187

/* https://bugzilla.gnome.org/show_bug.cgi?id=766370 */
#if !GLIB_CHECK_VERSION (2, 49, 3)
#define FLATPAK_VARIANT_BUILDER_INITIALIZER {{0, }}
#define FLATPAK_VARIANT_DICT_INITIALIZER {{0, }}
#else
#define FLATPAK_VARIANT_BUILDER_INITIALIZER {{{0, }}}
#define FLATPAK_VARIANT_DICT_INITIALIZER {{{0, }}}
#endif

/* https://github.com/GNOME/libglnx/pull/38
 * Note by using #define rather than wrapping via a static inline, we
 * don't have to re-define attributes like G_GNUC_PRINTF.
 */
#define flatpak_fail glnx_throw

gboolean flatpak_fail_error (GError     **error,
                             FlatpakError code,
                             const char  *fmt,
                             ...) G_GNUC_PRINTF (3, 4);

#define flatpak_debug2 g_debug

gint flatpak_strcmp0_ptr (gconstpointer a,
                          gconstpointer b);

/* Sometimes this is /var/run which is a symlink, causing weird issues when we pass
 * it as a path into the sandbox */
char * flatpak_get_real_xdg_runtime_dir (void);

gboolean  flatpak_has_path_prefix (const char *str,
                                   const char *prefix);


gboolean flatpak_g_ptr_array_contains_string (GPtrArray  *array,
                                              const char *str);

char * flatpak_quote_argv (const char *argv[],
                           gssize      len);

const char *flatpak_file_get_path_cached (GFile *file);


gboolean flatpak_mkdir_p (GFile        *dir,
                          GCancellable *cancellable,
                          GError      **error);
gboolean flatpak_buffer_to_sealed_memfd_or_tmpfile (GLnxTmpfile *tmpf,
                                                    const char  *name,
                                                    const char  *str,
                                                    size_t       len,
                                                    GError     **error);

#if !GLIB_CHECK_VERSION (2, 43, 4)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GUnixFDList, g_object_unref)
#endif

static inline void
null_safe_g_ptr_array_unref (gpointer data)
{
  g_clear_pointer (&data, g_ptr_array_unref);
}

int flatpak_envp_cmp (const void *p1,
                      const void *p2);
#endif /* __FLATPAK_UTILS_H__ */
