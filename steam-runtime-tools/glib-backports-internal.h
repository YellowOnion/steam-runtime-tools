/*
 * Functions backported and adapted from GLib
 *
 *  Copyright 2000 Red Hat, Inc.
 *  Copyright 2019 Collabora Ltd.
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

#if !GLIB_CHECK_VERSION (2, 42, 0)
# define G_OPTION_FLAG_NONE (0)
#endif

#ifndef G_SPAWN_DEFAULT
#define G_SPAWN_DEFAULT 0
#endif

#if !GLIB_CHECK_VERSION(2, 34, 0)
/**
 * G_SPAWN_EXIT_ERROR:
 *
 * Error domain used by g_spawn_check_exit_status().  The code
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
#define g_spawn_check_exit_status my_g_spawn_check_exit_status
gboolean my_g_spawn_check_exit_status (gint exit_status,
                                       GError  **error);
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

#if !GLIB_CHECK_VERSION (2, 67, 0)
#define G_DBUS_METHOD_INVOCATION_HANDLED TRUE
#define G_DBUS_METHOD_INVOCATION_UNHANDLED FALSE
#endif

#if !GLIB_CHECK_VERSION(2, 52, 0)
#define g_utf8_make_valid(s,l) my_g_utf8_make_valid (s, l)
gchar *my_g_utf8_make_valid (const gchar *str,
                             gssize len);
#endif

#ifndef g_info
#define g_info(...)     g_log (G_LOG_DOMAIN,         \
                               G_LOG_LEVEL_INFO,     \
                               __VA_ARGS__)
#endif
