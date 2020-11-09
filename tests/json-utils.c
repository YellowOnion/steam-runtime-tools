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
test_dup_strv_member (Fixture *f,
                      gconstpointer context)
{
  g_autoptr(JsonArray) arr = json_array_new ();
  g_autoptr(JsonNode) arr_node = json_node_alloc ();
  g_autoptr(JsonObject) obj = json_object_new ();
  g_autoptr(JsonObject) empty = json_object_new ();
  g_auto(GStrv) missing = NULL;
  g_auto(GStrv) with_placeholder = NULL;
  g_auto(GStrv) without_placeholder = NULL;

  json_array_add_string_element (arr, "one");
  json_array_add_string_element (arr, "two");
  json_array_add_object_element (arr, g_steal_pointer (&empty));
  json_array_add_string_element (arr, "four");
  json_node_init_array (arr_node, arr);
  json_object_set_member (obj, "arr", g_steal_pointer (&arr_node));

  missing = _srt_json_object_dup_strv_member (obj, "missing", NULL);
  g_assert_null (missing);

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

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/json-utils/dup-strv-member", Fixture, NULL,
              setup, test_dup_strv_member, teardown);

  return g_test_run ();
}
