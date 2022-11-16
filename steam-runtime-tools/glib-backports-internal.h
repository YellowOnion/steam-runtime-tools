/*<private_header>*/
/*
 * Functions backported and adapted from GLib
 *
 *  Copyright 2000 Red Hat, Inc.
 *  Copyright 2019 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>

#include <libglnx.h>

#if !GLIB_CHECK_VERSION(2, 34, 0)
/**
 * G_SPAWN_EXIT_ERROR:
 *
 * Error domain used by g_spawn_check_wait_status().  The code
 * will be the program exit code.
 */
#define G_SPAWN_EXIT_ERROR my_g_spawn_exit_error_quark ()
GQuark my_g_spawn_exit_error_quark (void);

#define G_DEFINE_QUARK(QN, q_n)                                         \
GQuark                                                                  \
q_n##_quark (void)                                                      \
{                                                                       \
  static GQuark q;                                                      \
                                                                        \
  if G_UNLIKELY (q == 0)                                                \
    q = g_quark_from_static_string (#QN);                               \
                                                                        \
  return q;                                                             \
}
#endif

#if !GLIB_CHECK_VERSION(2, 36, 0)
#define g_close my_g_close
gboolean my_g_close (gint fd,
                     GError **error);
#endif

#if !GLIB_CHECK_VERSION(2, 34, 0)
#define g_spawn_check_wait_status my_g_spawn_check_wait_status
gboolean my_g_spawn_check_wait_status (gint wait_status,
                                       GError  **error);
#elif !GLIB_CHECK_VERSION(2, 70, 0)
/* In GLib 2.34 to 2.68, this was available under a misleading name */
#define g_spawn_check_wait_status g_spawn_check_exit_status
#endif

#if !GLIB_CHECK_VERSION(2, 40, 0)
#define g_ptr_array_insert my_g_ptr_array_insert
void my_g_ptr_array_insert (GPtrArray *arr,
                            gint index_,
                            gpointer data);
#endif

#if !GLIB_CHECK_VERSION(2, 36, 0)
#define g_dbus_address_escape_value my_g_dbus_address_escape_value
gchar *my_g_dbus_address_escape_value (const gchar *string);
#endif

#if !GLIB_CHECK_VERSION(2, 36, 0)
#define g_unix_fd_source_new(fd, cond) \
  my_g_unix_fd_source_new (fd, cond)
#define g_unix_fd_add(fd, cond, cb, ud) \
  my_g_unix_fd_add_full (G_PRIORITY_DEFAULT, fd, cond, cb, ud, NULL)
#define g_unix_fd_add_full(prio, fd, cond, cb, ud, destroy) \
  my_g_unix_fd_add_full (prio, fd, cond, cb, ud, destroy)
typedef gboolean (*MyGUnixFDSourceFunc) (gint, GIOCondition, gpointer);
guint my_g_unix_fd_add_full (int priority,
                             int fd,
                             GIOCondition condition,
                             MyGUnixFDSourceFunc func,
                             gpointer user_data,
                             GDestroyNotify destroy);
GSource *my_g_unix_fd_source_new (int fd,
                                  GIOCondition condition);
#endif

#if !GLIB_CHECK_VERSION(2, 58, 0)
#define g_canonicalize_filename(f, r) my_g_canonicalize_filename (f, r)
gchar *my_g_canonicalize_filename (const gchar *filename,
                                   const gchar *relative_to);
#endif

#if !GLIB_CHECK_VERSION(2, 40, 0)
#define g_hash_table_get_keys_as_array(h, l) \
  my_g_hash_table_get_keys_as_array (h, l)
gpointer *my_g_hash_table_get_keys_as_array (GHashTable *hash,
                                             guint *len);
#endif

#if !GLIB_CHECK_VERSION(2, 50, 0)
#define g_log_writer_is_journald(fd) my_g_log_writer_is_journald (fd)
gboolean my_g_log_writer_is_journald (gint output_fd);
#endif

#if !GLIB_CHECK_VERSION(2, 52, 0)
#define g_utf8_make_valid(s,l) my_g_utf8_make_valid (s, l)
gchar *my_g_utf8_make_valid (const gchar *str,
                             gssize len);
#endif

#if !GLIB_CHECK_VERSION(2, 54, 0)
#define g_ptr_array_find_with_equal_func(h, n, e, i) my_g_ptr_array_find_with_equal_func (h, n, e, i)
gboolean my_g_ptr_array_find_with_equal_func (GPtrArray *haystack,
                                              gconstpointer needle,
                                              GEqualFunc equal_func,
                                              guint *index_);
#endif

#if !GLIB_CHECK_VERSION(2, 64, 0)
#if defined(G_HAVE_ISO_VARARGS) && (!defined(G_ANALYZER_ANALYZING) || !G_ANALYZER_ANALYZING)
#define g_warning_once(...) \
  G_STMT_START { \
    static int G_PASTE (_GWarningOnceBoolean, __LINE__) = 0;  /* (atomic) */ \
    if (g_atomic_int_compare_and_exchange (&G_PASTE (_GWarningOnceBoolean, __LINE__), \
                                           0, 1)) \
      g_warning (__VA_ARGS__); \
  } G_STMT_END
#elif defined(G_HAVE_GNUC_VARARGS) && (!defined(G_ANALYZER_ANALYZING) || !G_ANALYZER_ANALYZING)
#define g_warning_once(format...) \
  G_STMT_START { \
    static int G_PASTE (_GWarningOnceBoolean, __LINE__) = 0;  /* (atomic) */ \
    if (g_atomic_int_compare_and_exchange (&G_PASTE (_GWarningOnceBoolean, __LINE__), \
                                           0, 1)) \
      g_warning (format); \
  } G_STMT_END
#else
#define g_warning_once g_warning
#endif
#endif

#if !GLIB_CHECK_VERSION(2, 68, 0)
#define g_string_replace(s,f,r,l) my_g_string_replace (s, f, r, l)
guint my_g_string_replace (GString *string,
                           const gchar *find,
                           const gchar *replace,
                           guint limit);
#endif
