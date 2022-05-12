/*<private_header>*/
/*
 * Copyright © 2019-2021 Collabora Ltd.
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

#pragma once

#include <stdio.h>

#include <glib.h>

#include <json-glib/json-glib.h>

#include "steam-runtime-tools/container.h"
#include "steam-runtime-tools/cpu-feature.h"
#include "steam-runtime-tools/desktop-entry.h"
#include "steam-runtime-tools/library.h"
#include "steam-runtime-tools/os-internal.h"
#include "steam-runtime-tools/runtime.h"
#include "steam-runtime-tools/simple-input-device-internal.h"
#include "steam-runtime-tools/steam.h"
#include "steam-runtime-tools/virtualization.h"

gboolean _srt_architecture_can_run_from_report (JsonObject *json_obj);
SrtContainerInfo *_srt_container_info_get_from_report (JsonObject *json_obj);
SrtVirtualizationInfo *_srt_virtualization_info_get_from_report (JsonObject *json_obj);
SrtX86FeatureFlags _srt_feature_get_x86_flags_from_report (JsonObject *json_obj,
                                                           SrtX86FeatureFlags *known);
GList *_srt_get_steam_desktop_entries_from_json_report (JsonObject *json_obj);
SrtLibraryIssues _srt_library_get_issues_from_report (JsonObject *json_obj);
void _srt_os_release_populate_from_report (JsonObject *json_obj,
                                           SrtOsRelease *self);
SrtRuntimeIssues _srt_runtime_get_issues_from_report (JsonObject *json_obj);
SrtSimpleInputDevice *_srt_simple_input_device_new_from_json (JsonObject *obj);
SrtSteam *_srt_steam_get_from_report (JsonObject *json_obj);
