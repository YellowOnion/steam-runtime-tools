/*
 * Copyright © 2019-2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "steam-runtime-tools/json-utils-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * srt_get_flags_from_json_array:
 * @flags_type: The type of the flag
 * @json_obj: (not nullable): A JSON Object used to search for
 *  @array_member property
 * @array_member: (not nullable): The JSON member to look up
 * @flag_if_unknown: flag to use in case of parsing error
 *
 * Get the flags from a given JSON object array member.
 * If @json_obj doesn't have the provided @member, or it is malformed, the
 * @flag_if_unknown will be returned.
 * If the parsed JSON array_member has some elements that we can't parse,
 * @flag_if_unknown will be added to the returned flags.
 *
 * Returns: the found flags from the provided @json_obj
 */
guint
srt_get_flags_from_json_array (GType flags_type,
                               JsonObject *json_obj,
                               const gchar *array_member,
                               guint flag_if_unknown)
{
  JsonArray *array;
  guint ret = flag_if_unknown;

  g_return_val_if_fail (G_TYPE_IS_FLAGS (flags_type), 0);
  g_return_val_if_fail (json_obj != NULL, 0);
  g_return_val_if_fail (array_member != NULL, 0);

  if (json_object_has_member (json_obj, array_member))
    {
      array = json_object_get_array_member (json_obj, array_member);

      if (array == NULL)
        goto out;

      /* We reset the value out because we found the member we were looking for */
      ret = 0;

      for (guint j = 0; j < json_array_get_length (array); j++)
        {
          const gchar *issue_string = json_array_get_string_element (array, j);
          if (!srt_add_flag_from_nick (flags_type, issue_string, &ret, NULL))
            ret |= flag_if_unknown;
        }
    }

out:
  return ret;
}

/**
 * _srt_json_object_dup_strv_member:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  @array_member property
 * @array_member: (not nullable): The JSON member to look up
 * @placeholder: (nullable): If an item in the array is not a string,
 *  substitute this non-%NULL value; or if %NULL, behave as though it
 *  was not present
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type utf8) (nullable):
 *  A string array from the given @json_obj, or %NULL if it doesn't have a
 *  property @array_member
 */
gchar **
_srt_json_object_dup_strv_member (JsonObject *json_obj,
                                  const gchar *array_member,
                                  const gchar *placeholder)
{
  JsonArray *array;
  JsonNode *arr_node;
  guint length;
  gchar **ret = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);
  g_return_val_if_fail (array_member != NULL, NULL);

  arr_node = json_object_get_member (json_obj, array_member);

  if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
    {
      guint j = 0;

      array = json_node_get_array (arr_node);

      if (array == NULL)
        return ret;

      length = json_array_get_length (array);
      ret = g_new0 (gchar *, length + 1);

      for (guint i = 0; i < length; i++)
        {
          JsonNode *node = json_array_get_element (array, i);
          const gchar *element = json_node_get_string (node);

          if (element == NULL)
            element = placeholder;

          if (element == NULL)
            continue;

          ret[j++] = g_strdup (element);
        }

      g_assert (j <= length);
      ret[j] = NULL;
    }

  return ret;
}

/**
 * _srt_json_object_dup_array_of_lines_member:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  @array_member property
 * @array_member: (not nullable): The JSON member to look up
 *
 * If @json_obj has a member named @array_member and it is an array
 * of strings, concatenate the strings (adding a trailing newline to
 * each one if not already present) and return them. Otherwise,
 * return %NULL.
 *
 * Returns: (transfer full) (element-type utf8) (nullable):
 *  A string, or %NULL if not found. Free with g_free().
 */
gchar *
_srt_json_object_dup_array_of_lines_member (JsonObject *json_obj,
                                            const gchar *array_member)
{
  JsonArray *array;
  JsonNode *arr_node;
  guint length;
  g_autoptr(GString) ret = g_string_new ("");

  g_return_val_if_fail (json_obj != NULL, NULL);
  g_return_val_if_fail (array_member != NULL, NULL);

  arr_node = json_object_get_member (json_obj, array_member);

  if (arr_node == NULL || !JSON_NODE_HOLDS_ARRAY (arr_node))
    return NULL;

  array = json_node_get_array (arr_node);

  if (array == NULL)
    return NULL;

  length = json_array_get_length (array);

  for (guint i = 0; i < length; i++)
    {
      JsonNode *node = json_array_get_element (array, i);
      const gchar *element = json_node_get_string (node);

      if (element != NULL)
        g_string_append (ret, element);

      if (element == NULL || element[strlen (element) - 1] != '\n')
        g_string_append_c (ret, '\n');
    }

  return g_string_free (g_steal_pointer (&ret), FALSE);
}
