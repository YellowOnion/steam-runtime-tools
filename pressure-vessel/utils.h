/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2019 Collabora Ltd.
 *
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

#include <stdio.h>
#include <sys/types.h>

#include <glib.h>

#include "missing.h"

void pv_search_path_append (GString *search_path,
                            const gchar *item);

gboolean pv_run_sync (const char * const * argv,
                      const char * const * envp,
                      int *exit_status_out,
                      char **output_out,
                      GError **error);

gpointer pv_hash_table_get_arbitrary_key (GHashTable *table);

gboolean pv_wait_for_child_processes (pid_t main_process,
                                      int *wait_status_out,
                                      GError **error);

gboolean pv_terminate_all_child_processes (GTimeSpan wait_period,
                                           GTimeSpan grace_period,
                                           GError **error);

gchar *pv_current_namespace_path_to_host_path (const gchar *current_env_path);

void pv_delete_dangling_symlink (int dirfd,
                                 const char *debug_path,
                                 const char *name);
