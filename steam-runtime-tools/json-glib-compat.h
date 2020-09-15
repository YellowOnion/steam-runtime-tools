/*<private_header>*/
/*
 * Copyright © 2019 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <libglnx.h>

#include <json-glib/json-glib.h>

#if !JSON_CHECK_VERSION(1, 1, 2)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonBuilder, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonGenerator, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonParser, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonPath, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonReader, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonArray, array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonNode, json_node_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonObject, json_object_unref)
#endif
