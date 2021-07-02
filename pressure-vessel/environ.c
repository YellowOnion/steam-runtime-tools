/*
 * Copyright © 2014-2018 Red Hat, Inc
 * Copyright © 2020 Collabora Ltd.
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

#include "environ.h"

/*
 * PvEnviron:
 *
 * Each environment variable we deal with in pressure-vessel has
 * the following possible values:
 *
 * - Set to a value (empty or non-empty)
 * - Forced to be unset
 * - Inherited from the execution environment of bwrap(1)
 *
 * together with a *locked* flag. The locked flag indicates that if the
 * container is running pressure-vessel-launcher(1), then the variable
 * cannot be overridden by pressure-vessel-launch(1).
 *
 * In particular, the locked flag can be combined with any value, and
 * a variable can also be locked to the inherited value.
 *
 * We represent this as follows:
 *
 * - Set to a value: values[VAR] = VAL
 * - Forced to be unset: values[VAR] = NULL
 * - Inherited from the execution environment of bwrap(1): VAR not in `values`
 *
 * Variables that are locked are also added to `locked`.
 */
struct _PvEnviron
{
  /* (element-type filename filename) */
  GHashTable *values;
  /* (element-type filename ignored) */
  GHashTable *locked;
};

PvEnviron *
pv_environ_new (void)
{
  g_autoptr(PvEnviron) self = g_slice_new0 (PvEnviron);

  self->values = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, g_free);
  self->locked = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, NULL);
  return g_steal_pointer (&self);
}

void
pv_environ_free (PvEnviron *self)
{
  g_return_if_fail (self != NULL);

  g_clear_pointer (&self->values, g_hash_table_unref);
  g_clear_pointer (&self->locked, g_hash_table_unref);
  g_slice_free (PvEnviron, self);
}

/*
 * Lock @var to be set to @val, which may be %NULL to unset it.
 * It cannot be overridden by a subsequent pressure-vessel-launch
 * invocation.
 */
void
pv_environ_lock_env (PvEnviron *self,
                     const char *var,
                     const char *val)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  /* val may be NULL, to lock it to be unset */

  g_hash_table_replace (self->values, g_strdup (var), g_strdup (val));
  g_hash_table_add (self->locked, g_strdup (var));
}

/*
 * Lock @var to whatever value it happens to have inherited, in a way
 * that cannot be overridden by a subsequent pressure-vessel-launch
 * invocation.
 */
void
pv_environ_lock_inherit_env (PvEnviron *self,
                             const char *var)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);

  g_hash_table_remove (self->values, var);
  g_hash_table_add (self->locked, g_strdup (var));
}

/*
 * Set @var to @val, in a way that can be overridden by a subsequent
 * pressure-vessel-launch invocation.
 */
void
pv_environ_set_env_overridable (PvEnviron *self,
                                const char *var,
                                const char *val)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  /* val may be NULL, to lock it to be unset */

  g_hash_table_replace (self->values, g_strdup (var), g_strdup (val));
  g_hash_table_remove (self->locked, var);
}

static int
generic_strcmp (gconstpointer a,
                gconstpointer b)
{
  return strcmp (a, b);
}

/*
 * Returns: (transfer container): The variables that are set or forced
 *  to be unset, but not the variables that are locked to be inherited
 */
GList *
pv_environ_get_vars (PvEnviron *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return g_list_sort (g_hash_table_get_keys (self->values), generic_strcmp);
}

/*
 * Returns: (transfer container): The variables that are locked in some way
 */
GList *
pv_environ_get_locked (PvEnviron *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return g_list_sort (g_hash_table_get_keys (self->locked), generic_strcmp);
}

/*
 * Returns: %TRUE if @var is locked
 */
gboolean
pv_environ_is_locked (PvEnviron *self,
                      const char *var)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return g_hash_table_contains (self->locked, var);
}

/*
 * Returns: (nullable): The value of @var, or %NULL if @var is either
 *  locked-to-be-unset, locked-to-be-inherited or unspecified
 */
const char *
pv_environ_getenv (PvEnviron *self,
                   const char *var)
{
  g_return_val_if_fail (self != NULL, NULL);

  return g_hash_table_lookup (self->values, var);
}
