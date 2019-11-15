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

#if !GLIB_CHECK_VERSION (2, 42, 0)
#define G_OPTION_FLAG_NONE 0
#endif
