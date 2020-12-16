/*
 * Copyright © 2014-2018 Red Hat, Inc
 * Copyright © 2020 Collabora Ltd.
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

#include "libglnx/libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"

typedef struct _PvEnviron PvEnviron;

PvEnviron *pv_environ_new (void);
void pv_environ_free (PvEnviron *self);

void pv_environ_lock_env (PvEnviron *self,
                          const char *var,
                          const char *val);
void pv_environ_lock_inherit_env (PvEnviron *self,
                                  const char *var);
void pv_environ_set_env_overridable (PvEnviron *self,
                                     const char *var,
                                     const char *val);

GList *pv_environ_get_vars (PvEnviron *self);
GList *pv_environ_get_locked (PvEnviron *self);
const char *pv_environ_getenv (PvEnviron *self,
                               const char *var);
gboolean pv_environ_is_locked (PvEnviron *self,
                               const char *var);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvEnviron, pv_environ_free)
