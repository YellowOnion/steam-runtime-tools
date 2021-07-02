/*<private_header>*/
/*
 * Backports from JSON-GLib
 *
 * Copyright (C) 2007  OpenedHand Ltd.
 * Copyright (C) 2009  Intel Corp.
 * Copyright © 2019-2021 Collabora Ltd.
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

#include <libglnx.h>

#include <json-glib/json-glib.h>

#if !JSON_CHECK_VERSION(1, 1, 2)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonBuilder, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonGenerator, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonParser, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonPath, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonReader, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonArray, json_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonNode, json_node_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonObject, json_object_unref)
#endif

#if !JSON_CHECK_VERSION(1, 6, 0)
#define json_object_get_string_member_with_default my_json_object_get_string_member_with_default
const char * my_json_object_get_string_member_with_default (JsonObject *object,
                                                            const char *member_name,
                                                            const char *default_value);

#define json_object_get_boolean_member_with_default my_json_object_get_boolean_member_with_default
gboolean my_json_object_get_boolean_member_with_default (JsonObject *object,
                                                         const char *member_name,
                                                         gboolean default_value);

#define json_object_get_int_member_with_default my_json_object_get_int_member_with_default
gint64 my_json_object_get_int_member_with_default (JsonObject *object,
                                                   const char *member_name,
                                                   gint64 default_value);
#endif

#if !JSON_CHECK_VERSION(1, 4, 0)
#define json_from_string(s, e) my_json_from_string(s, e)
JsonNode *my_json_from_string (const char  *str,
                               GError     **error);
#endif
