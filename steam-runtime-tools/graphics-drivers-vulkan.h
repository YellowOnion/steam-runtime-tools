/*
 * Copyright © 2019-2022 Collabora Ltd.
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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>

typedef struct _SrtVulkanIcd SrtVulkanIcd;
typedef struct _SrtVulkanIcdClass SrtVulkanIcdClass;

#define SRT_TYPE_VULKAN_ICD (srt_vulkan_icd_get_type ())
#define SRT_VULKAN_ICD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VULKAN_ICD, SrtVulkanIcd))
#define SRT_VULKAN_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VULKAN_ICD, SrtVulkanIcdClass))
#define SRT_IS_VULKAN_ICD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VULKAN_ICD))
#define SRT_IS_VULKAN_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VULKAN_ICD))
#define SRT_VULKAN_ICD_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VULKAN_ICD, SrtVulkanIcdClass)
_SRT_PUBLIC
GType srt_vulkan_icd_get_type (void);

_SRT_PUBLIC
gboolean srt_vulkan_icd_check_error (SrtVulkanIcd *self,
                                     GError **error);
_SRT_PUBLIC
const gchar *srt_vulkan_icd_get_api_version (SrtVulkanIcd *self);
_SRT_PUBLIC
const gchar *srt_vulkan_icd_get_json_path (SrtVulkanIcd *self);
_SRT_PUBLIC
const gchar *srt_vulkan_icd_get_library_path (SrtVulkanIcd *self);
_SRT_PUBLIC
SrtLoadableIssues srt_vulkan_icd_get_issues (SrtVulkanIcd *self);
_SRT_PUBLIC
gchar *srt_vulkan_icd_resolve_library_path (SrtVulkanIcd *self);
_SRT_PUBLIC
SrtVulkanIcd *srt_vulkan_icd_new_replace_library_path (SrtVulkanIcd *self,
                                                       const char *path);
_SRT_PUBLIC
gboolean srt_vulkan_icd_write_to_file (SrtVulkanIcd *self,
                                       const char *path,
                                       GError **error);

typedef struct _SrtVulkanLayer SrtVulkanLayer;
typedef struct _SrtVulkanLayerClass SrtVulkanLayerClass;

#define SRT_TYPE_VULKAN_LAYER (srt_vulkan_layer_get_type ())
#define SRT_VULKAN_LAYER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VULKAN_LAYER, SrtVulkanLayer))
#define SRT_VULKAN_LAYER_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VULKAN_LAYER, SrtVulkanLayerClass))
#define SRT_IS_VULKAN_LAYER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VULKAN_LAYER))
#define SRT_IS_VULKAN_LAYER_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VULKAN_LAYER))
#define SRT_VULKAN_LAYER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VULKAN_LAYER, SrtVulkanLayerClass)
_SRT_PUBLIC
GType srt_vulkan_layer_get_type (void);

_SRT_PUBLIC
gboolean srt_vulkan_layer_check_error (const SrtVulkanLayer *self,
                                       GError **error);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_json_path (SrtVulkanLayer *self);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_library_path (SrtVulkanLayer *self);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_name (SrtVulkanLayer *self);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_description (SrtVulkanLayer *self);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_api_version (SrtVulkanLayer *self);
_SRT_PUBLIC
gchar *srt_vulkan_layer_resolve_library_path (SrtVulkanLayer *self);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_type_value (SrtVulkanLayer *self);
_SRT_PUBLIC
const gchar *srt_vulkan_layer_get_implementation_version (SrtVulkanLayer *self);
_SRT_PUBLIC
const char * const *srt_vulkan_layer_get_component_layers (SrtVulkanLayer *self);
_SRT_PUBLIC
SrtLoadableIssues srt_vulkan_layer_get_issues (SrtVulkanLayer *self);
_SRT_PUBLIC
SrtVulkanLayer *srt_vulkan_layer_new_replace_library_path (SrtVulkanLayer *self,
                                                           const char *path);
#ifndef __GTK_DOC_IGNORE__
_SRT_PUBLIC
G_DEPRECATED_FOR (srt_system_info_list_explicit_vulkan_layers or
srt_system_info_list_implicit_vulkan_layers)
#endif
GList *_srt_load_vulkan_layers (const char *sysroot,
                                gchar **envp,
                                gboolean explicit);
_SRT_PUBLIC
gboolean srt_vulkan_layer_write_to_file (SrtVulkanLayer *self,
                                         const char *path,
                                         GError **error);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVulkanIcd, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVulkanLayer, g_object_unref)
#endif
