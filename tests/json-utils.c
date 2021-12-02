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

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "test-utils.h"

typedef struct
{
  int unused;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
test_dup_array_of_lines_member (Fixture *f,
                                gconstpointer context)
{
  g_autoptr(JsonArray) arr = json_array_new ();
  g_autoptr(JsonNode) arr_node = json_node_alloc ();
  g_autoptr(JsonObject) obj = json_object_new ();
  g_autofree gchar *missing = NULL;
  g_autofree gchar *not_array = NULL;
  g_autofree gchar *text = NULL;

  json_array_add_string_element (arr, "one");
  json_array_add_string_element (arr, "two\n");
  json_array_add_object_element (arr, json_object_new ());
  json_array_add_string_element (arr, "four");
  json_node_init_array (arr_node, arr);
  json_object_set_member (obj, "arr", g_steal_pointer (&arr_node));
  json_object_set_double_member (obj, "not-array", 42.0);

  missing = _srt_json_object_dup_array_of_lines_member (obj, "missing");
  g_assert_null (missing);

  not_array = _srt_json_object_dup_array_of_lines_member (obj, "not-array");
  g_assert_null (not_array);

  text = _srt_json_object_dup_array_of_lines_member (obj, "arr");
  g_assert_cmpstr (text, ==,
                   "one\n"
                   "two\n"
                   "\n"
                   "four\n");
}

static void
test_dup_strv_member (Fixture *f,
                      gconstpointer context)
{
  g_autoptr(JsonArray) arr = json_array_new ();
  g_autoptr(JsonNode) arr_node = json_node_alloc ();
  g_autoptr(JsonObject) obj = json_object_new ();
  g_autoptr(JsonObject) empty = json_object_new ();
  g_auto(GStrv) missing = NULL;
  g_auto(GStrv) not_array = NULL;
  g_auto(GStrv) with_placeholder = NULL;
  g_auto(GStrv) without_placeholder = NULL;

  json_array_add_string_element (arr, "one");
  json_array_add_string_element (arr, "two");
  json_array_add_object_element (arr, g_steal_pointer (&empty));
  json_array_add_string_element (arr, "four");
  json_node_init_array (arr_node, arr);
  json_object_set_member (obj, "arr", g_steal_pointer (&arr_node));
  json_object_set_double_member (obj, "not-array", 42.0);

  missing = _srt_json_object_dup_strv_member (obj, "missing", NULL);
  g_assert_null (missing);

  not_array = _srt_json_object_dup_strv_member (obj, "not-array", NULL);
  g_assert_null (not_array);

  with_placeholder = _srt_json_object_dup_strv_member (obj, "arr", "?!");
  g_assert_nonnull (with_placeholder);
  g_assert_cmpstr (with_placeholder[0], ==, "one");
  g_assert_cmpstr (with_placeholder[1], ==, "two");
  g_assert_cmpstr (with_placeholder[2], ==, "?!");
  g_assert_cmpstr (with_placeholder[3], ==, "four");
  g_assert_cmpstr (with_placeholder[4], ==, NULL);

  without_placeholder = _srt_json_object_dup_strv_member (obj, "arr", NULL);
  g_assert_nonnull (without_placeholder);
  g_assert_cmpstr (without_placeholder[0], ==, "one");
  g_assert_cmpstr (without_placeholder[1], ==, "two");
  g_assert_cmpstr (without_placeholder[2], ==, "four");
  g_assert_cmpstr (without_placeholder[3], ==, NULL);
}

static void
test_get_hex_uint32_member (Fixture *f,
                            gconstpointer context)
{
  g_autoptr(JsonArray) arr = json_array_new ();
  g_autoptr(JsonNode) arr_node = json_node_alloc ();
  g_autoptr(JsonObject) obj = json_object_new ();
  guint32 value;

  json_object_set_string_member (obj, "zero", "0");
  json_object_set_string_member (obj, "fortytwo", "0x2a");
  json_object_set_string_member (obj, "twentythree", "0X17");
  json_object_set_string_member (obj, "out-of-range", "0x12345678abcdef");
  json_object_set_string_member (obj, "empty", "");
  json_object_set_string_member (obj, "nil", NULL);
  json_object_set_double_member (obj, "not-string", 42.0);
  json_node_init_array (arr_node, arr);
  json_object_set_member (obj, "arr", g_steal_pointer (&arr_node));

  g_assert_true (_srt_json_object_get_hex_uint32_member (obj, "zero", NULL));
  g_assert_true (_srt_json_object_get_hex_uint32_member (obj, "zero", &value));
  g_assert_cmpuint (value, ==, 0);
  g_assert_true (_srt_json_object_get_hex_uint32_member (obj, "fortytwo", &value));
  g_assert_cmpuint (value, ==, 42);
  g_assert_true (_srt_json_object_get_hex_uint32_member (obj, "twentythree", &value));
  g_assert_cmpuint (value, ==, 23);

  value = 99;
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "missing", NULL));
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "missing", &value));
  g_assert_cmpuint (value, ==, 99);
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "out-of-range", NULL));
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "out-of-range", &value));
  g_assert_cmpuint (value, ==, 99);
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "empty", NULL));
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "empty", &value));
  g_assert_cmpuint (value, ==, 99);
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "nil", NULL));
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "nil", &value));
  g_assert_cmpuint (value, ==, 99);
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "not-string", NULL));
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "not-string", &value));
  g_assert_cmpuint (value, ==, 99);
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "arr", NULL));
  g_assert_false (_srt_json_object_get_hex_uint32_member (obj, "arr", &value));
  g_assert_cmpuint (value, ==, 99);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/json-utils/dup-array-of-lines-member", Fixture, NULL,
              setup, test_dup_array_of_lines_member, teardown);
  g_test_add ("/json-utils/dup-strv-member", Fixture, NULL,
              setup, test_dup_strv_member, teardown);
  g_test_add ("/json-utils/get-hex-uint32-member", Fixture, NULL,
              setup, test_get_hex_uint32_member, teardown);

  return g_test_run ();
}
