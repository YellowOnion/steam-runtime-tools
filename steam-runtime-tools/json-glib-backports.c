/*
 * Backports from JSON-GLib
 *
 * Copyright (C) 2007  OpenedHand Ltd.
 * Copyright (C) 2009  Intel Corp.
 * Copyright © 2020 Collabora Ltd.
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

#include "steam-runtime-tools/json-glib-backports-internal.h"

#include <glib-object.h>

#if !JSON_CHECK_VERSION(1, 6, 0)
/*
 * json_object_get_string_member_with_default:
 * @object: a #JsonObject
 * @member_name: the name of the object member
 * @default_value: the value to return if member_name is not valid
 *
 * Convenience function that retrieves the string value stored in
 * @member_name of object.
 *
 * If @member_name does not exist, does not contain a scalar value, or
 * contains %NULL, then @default_value is returned instead.
 *
 * Returns: the string value of the object's member, or the given default
 */
const char *
my_json_object_get_string_member_with_default (JsonObject *object,
                                               const char *member_name,
                                               const char *default_value)
{
  g_return_val_if_fail (object != NULL, default_value);
  g_return_val_if_fail (member_name != NULL, default_value);

  JsonNode *node = json_object_get_member (object, member_name);

  if (node == NULL)
    return default_value;

  if (JSON_NODE_HOLDS_NULL (node))
    return default_value;

  g_return_val_if_fail (JSON_NODE_TYPE (node) == JSON_NODE_VALUE, default_value);

  return json_node_get_string (node);
}

/*
 * json_object_get_boolean_member_with_default:
 * @object: a #JsonObject
 * @member_name: the name of the object member
 * @default_value: the value to return if member_name is not valid
 *
 * Convenience function that retrieves the boolean value stored in
 * @member_name of object.
 *
 * If @member_name does not exist, does not contain a scalar value, or
 * contains %NULL, then @default_value is returned instead.
 *
 * Returns: the boolean value of the object's member, or the given default
 */
gboolean
my_json_object_get_boolean_member_with_default (JsonObject *object,
                                                const char *member_name,
                                                gboolean default_value)
{
  g_return_val_if_fail (object != NULL, default_value);
  g_return_val_if_fail (member_name != NULL, default_value);

  JsonNode *node = json_object_get_member (object, member_name);

  if (node == NULL)
    return default_value;

  if (JSON_NODE_HOLDS_NULL (node))
    return default_value;

  g_return_val_if_fail (JSON_NODE_TYPE (node) == JSON_NODE_VALUE, default_value);

  return json_node_get_boolean (node);
}

/*
 * json_object_get_int_member_with_default:
 * @object: a #JsonObject
 * @member_name: the name of the object member
 * @default_value: the value to return if member_name is not valid
 *
 * Convenience function that retrieves the integer value stored in
 * @member_name of object.
 *
 * If @member_name does not exist, does not contain a scalar value, or
 * contains %NULL, then @default_value is returned instead.
 *
 * Returns: the integer value of the object's member, or the given default
 */
gint64
my_json_object_get_int_member_with_default (JsonObject *object,
                                            const char *member_name,
                                            gint64 default_value)
{
  g_return_val_if_fail (object != NULL, default_value);
  g_return_val_if_fail (member_name != NULL, default_value);

  JsonNode *node = json_object_get_member (object, member_name);

  if (node == NULL)
    return default_value;

  if (JSON_NODE_HOLDS_NULL (node))
    return default_value;

  g_return_val_if_fail (JSON_NODE_TYPE (node) == JSON_NODE_VALUE, default_value);

  return json_node_get_int (node);
}
#endif
