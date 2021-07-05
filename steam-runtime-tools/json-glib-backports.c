/*
 * Backports from JSON-GLib
 *
 * Copyright (C) 2007  OpenedHand Ltd.
 * Copyright (C) 2009  Intel Corp.
 * Copyright © 2020-2021 Collabora Ltd.
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

#if !JSON_CHECK_VERSION(1, 4, 0)
/* json_from_string() was introduced from json-glib 1.2.0, but since
 * version 1.4.0 its handling of an empty @str changed. In 1.2.0 if you
 * provided an empty @str, you would face an assertion error. Instead,
 * in 1.4.0 its behaviour changed and now it simply returns an empty JsonNode
 * without setting @error. For this reason we override our backported
 * json_from_string() everytime the json-glib version is older than 1.4.0 */

/**
 * json_from_string:
 * @str: a valid UTF-8 string containing JSON data
 * @error: return location for a #GError
 *
 * Parses the string in @str and returns a #JsonNode representing
 * the JSON tree. If @str is empty, this function will return %NULL.
 *
 * In case of parsing error, this function returns %NULL and sets
 * @error appropriately.
 *
 * Returns: (transfer full) (nullable): a #JsonNode, or %NULL
 */
JsonNode *
my_json_from_string (const char *str,
                     GError **error)
{
  JsonParser *parser;
  JsonNode *root;
  JsonNode *retval = NULL;

  g_return_val_if_fail (str != NULL, NULL);

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, str, -1, error))
    {
      g_object_unref (parser);
      return NULL;
    }

  root = json_parser_get_root (parser);
  if (root != NULL)
    retval = json_node_copy (root);

  g_object_unref (parser);

  return retval;
}
#endif
