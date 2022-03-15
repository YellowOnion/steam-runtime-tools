/*
 * A cut-down version of common/flatpak-utils from Flatpak
 * Last updated: Flatpak 1.12.7
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

#include <string.h>

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



#if !GLIB_CHECK_VERSION (2, 56, 0)
typedef void (* GClearHandleFunc) (guint handle_id);

static inline void
g_clear_handle_id (guint            *tag_ptr,
                   GClearHandleFunc  clear_func)
{
  guint _handle_id;

  _handle_id = *tag_ptr;
  if (_handle_id > 0)
    {
      *tag_ptr = 0;
      clear_func (_handle_id);
    }
}
#endif


#if !GLIB_CHECK_VERSION (2, 58, 0)
static inline gboolean
g_hash_table_steal_extended (GHashTable    *hash_table,
                             gconstpointer  lookup_key,
                             gpointer      *stolen_key,
                             gpointer      *stolen_value)
{
  if (g_hash_table_lookup_extended (hash_table, lookup_key, stolen_key, stolen_value))
    {
      g_hash_table_steal (hash_table, lookup_key);
      return TRUE;
    }
  else
      return FALSE;
}
#endif

gboolean flatpak_g_ptr_array_contains_string (GPtrArray  *array,
                                              const char *str);

/* Returns the first string in subset that is not in strv */
static inline const gchar *
g_strv_subset (const gchar * const *strv,
               const gchar * const *subset)
{
  int i;

  for (i = 0; subset[i]; i++)
    {
      const char *key;

      key = subset[i];
      if (!g_strv_contains (strv, key))
        return key;
    }

  return NULL;
}

gboolean flatpak_argument_needs_quoting (const char *arg);
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

typedef GMainContext GMainContextPopDefault;
static inline void
flatpak_main_context_pop_default_destroy (void *p)
{
  GMainContext *main_context = p;

  if (main_context)
    {
      /* Ensure we don't leave some cleanup callbacks unhandled as we will never iterate this context again. */
      while (g_main_context_pending (main_context))
        g_main_context_iteration (main_context, TRUE);

      g_main_context_pop_thread_default (main_context);
      g_main_context_unref (main_context);
    }
}

static inline GMainContextPopDefault *
flatpak_main_context_new_default (void)
{
  GMainContext *main_context = g_main_context_new ();

  g_main_context_push_thread_default (main_context);
  return main_context;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GMainContextPopDefault, flatpak_main_context_pop_default_destroy)

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
