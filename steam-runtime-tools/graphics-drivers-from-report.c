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

#include "steam-runtime-tools/graphics.h"
#include "steam-runtime-tools/graphics-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"

/*
 * _srt_dri_driver_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "dri_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "dri_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtDriDriver that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_dri_driver_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *dri_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "dri_drivers"))
    {
      array = json_object_get_array_member (json_obj, "dri_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *dri_path = NULL;
          gboolean is_extra = FALSE;
          JsonObject *json_dri_obj = json_array_get_object_element (array, j);
          dri_path = json_object_get_string_member_with_default (json_dri_obj, "library_path",
                                                                 NULL);
          is_extra = json_object_get_boolean_member_with_default (json_dri_obj, "is_extra",
                                                                  FALSE);

          dri_drivers = g_list_prepend (dri_drivers, srt_dri_driver_new (dri_path, is_extra));
        }
    }

out:
  return g_list_reverse (dri_drivers);
}

/*
 * _srt_glx_icd_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "glx_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "glx_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtGlxIcd that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_glx_icd_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *glx_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "glx_drivers"))
    {
      array = json_object_get_array_member (json_obj, "glx_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *glx_path = NULL;
          const gchar *glx_soname = NULL;
          JsonObject *json_glx_obj = json_array_get_object_element (array, j);
          glx_path = json_object_get_string_member_with_default (json_glx_obj, "library_path",
                                                                 NULL);
          glx_soname = json_object_get_string_member_with_default (json_glx_obj, "library_soname",
                                                                   NULL);

          glx_drivers = g_list_prepend (glx_drivers, srt_glx_icd_new (glx_soname, glx_path));
        }
    }

out:
  return g_list_reverse (glx_drivers);
}

/*
 * _srt_va_api_driver_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "va-api_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "va-api_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtVaApiDriver that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_va_api_driver_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *va_api_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "va-api_drivers"))
    {
      array = json_object_get_array_member (json_obj, "va-api_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *va_api_path = NULL;
          gboolean is_extra = FALSE;
          JsonObject *json_va_api_obj = json_array_get_object_element (array, j);
          va_api_path = json_object_get_string_member_with_default (json_va_api_obj,
                                                                    "library_path",
                                                                    NULL);
          is_extra = json_object_get_boolean_member_with_default (json_va_api_obj,
                                                                  "is_extra",
                                                                  FALSE);

          va_api_drivers = g_list_prepend (va_api_drivers, srt_va_api_driver_new (va_api_path, is_extra));
        }
    }

out:
  return g_list_reverse (va_api_drivers);
}

/*
 * _srt_vdpau_driver_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "vdpau_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "vdpau_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtVdpauDriver that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_vdpau_driver_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *vdpau_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "vdpau_drivers"))
    {
      array = json_object_get_array_member (json_obj, "vdpau_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *vdpau_path = NULL;
          const gchar *vdpau_link = NULL;
          gboolean is_extra = FALSE;
          JsonObject *json_vdpau_obj = json_array_get_object_element (array, j);
          vdpau_path = json_object_get_string_member_with_default (json_vdpau_obj, "library_path",
                                                                   NULL);
          vdpau_link = json_object_get_string_member_with_default (json_vdpau_obj, "library_link",
                                                                   NULL);
          is_extra = json_object_get_boolean_member_with_default (json_vdpau_obj, "is_extra",
                                                                  FALSE);

          vdpau_drivers = g_list_prepend (vdpau_drivers, srt_vdpau_driver_new (vdpau_path, vdpau_link, is_extra));
        }
    }

out:
  return g_list_reverse (vdpau_drivers);
}

/*
 * get_driver_loadables_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for Icd or Layer
 *  properties
 * @which: Used to choose which loadable to search, it can be
 *  %SRT_TYPE_EGL_ICD, %SRT_TYPE_VULKAN_ICD or %SRT_TYPE_VULKAN_LAYER
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 *  Currently this value is used only if @which is %SRT_TYPE_VULKAN_LAYER
 *
 * Returns: A list of #SrtEglIcd (if @which is %SRT_TYPE_EGL_ICD) or
 *  #SrtVulkanIcd (if @which is %SRT_TYPE_VULKAN_ICD) or #SrtVulkanLayer (if
 *  @which is %SRT_TYPE_VULKAN_LAYER) that have been found, or
 *  %NULL if none has been found.
 */
static GList *
get_driver_loadables_from_json_report (JsonObject *json_obj,
                                       GType which,
                                       gboolean explicit)
{
  const gchar *member;
  const gchar *sub_member;
  JsonObject *json_sub_obj;
  JsonArray *array;
  GList *driver_info = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (which == SRT_TYPE_EGL_ICD)
    {
      member = "egl";
      sub_member = "icds";
    }
  else if (which == SRT_TYPE_VULKAN_ICD)
    {
      member = "vulkan";
      sub_member = "icds";
    }
  else if (which == SRT_TYPE_VULKAN_LAYER)
    {
      member = "vulkan";
      if (explicit)
        sub_member = "explicit_layers";
      else
        sub_member = "implicit_layers";
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  if (json_object_has_member (json_obj, member))
    {
      json_sub_obj = json_object_get_object_member (json_obj, member);

      /* We are expecting an object here */
      if (json_sub_obj == NULL)
        {
          g_debug ("'%s' is not a JSON object as expected", member);
          goto out;
        }

      if (json_object_has_member (json_sub_obj, sub_member))
        {
          array = json_object_get_array_member (json_sub_obj, sub_member);

          /* We are expecting an array of icds here */
          if (array == NULL)
            {
              g_debug ("'%s' is not an array as expected", sub_member);
              goto out;
            }

          for (guint i = 0; i < json_array_get_length (array); i++)
            {
              const gchar *json_path = NULL;
              const gchar *name = NULL;
              const gchar *type = NULL;
              const gchar *description = NULL;
              const gchar *library_path = NULL;
              const gchar *api_version = NULL;
              const gchar *implementation_version = NULL;
              g_auto(GStrv) component_layers = NULL;
              SrtVulkanLayer *layer = NULL;
              SrtLoadableIssues issues;
              GQuark error_domain;
              gint error_code;
              const gchar *error_message;
              GError *error = NULL;
              JsonObject *json_elem_obj = json_array_get_object_element (array, i);
              if (json_object_has_member (json_elem_obj, "json_path"))
                {
                  json_path = json_object_get_string_member (json_elem_obj, "json_path");
                }
              else
                {
                  g_debug ("The parsed '%s' member is missing the expected 'json_path' member, skipping...",
                           sub_member);
                  continue;
                }

              if (which == SRT_TYPE_VULKAN_LAYER)
                {
                  name = json_object_get_string_member_with_default (json_elem_obj, "name", NULL);
                  type = json_object_get_string_member_with_default (json_elem_obj, "type", NULL);
                  implementation_version = json_object_get_string_member_with_default (json_elem_obj,
                                                                                       "implementation_version",
                                                                                       NULL);
                  description = json_object_get_string_member_with_default (json_elem_obj,
                                                                            "description",
                                                                            NULL);

                  component_layers = _srt_json_object_dup_strv_member (json_elem_obj,
                                                                       "component_layers",
                                                                       NULL);

                  /* Don't distinguish between absent, and present with empty value */
                  if (component_layers != NULL && component_layers[0] == NULL)
                    g_clear_pointer (&component_layers, g_free);
                }

              library_path = json_object_get_string_member_with_default (json_elem_obj,
                                                                         "library_path",
                                                                         NULL);
              api_version = json_object_get_string_member_with_default (json_elem_obj,
                                                                        "api_version",
                                                                        NULL);
              issues = srt_get_flags_from_json_array (SRT_TYPE_LOADABLE_ISSUES,
                                                      json_elem_obj,
                                                      "issues",
                                                      SRT_LOADABLE_ISSUES_UNKNOWN);
              error_domain = g_quark_from_string (json_object_get_string_member_with_default (json_elem_obj,
                                                                                              "error-domain",
                                                                                              NULL));
              error_code = json_object_get_int_member_with_default (json_elem_obj, "error-code", -1);
              error_message = json_object_get_string_member_with_default (json_elem_obj,
                                                                          "error",
                                                                          "(missing error message)");

              if (which == SRT_TYPE_VULKAN_LAYER &&
                  (name != NULL &&
                   type != NULL &&
                   api_version != NULL &&
                   implementation_version != NULL &&
                   description != NULL &&
                   ( (library_path != NULL && component_layers == NULL) ||
                     (library_path == NULL && component_layers != NULL) )))
                {
                  layer = srt_vulkan_layer_new (json_path, name, type,
                                                library_path, api_version,
                                                implementation_version,
                                                description, component_layers,
                                                issues);
                  driver_info = g_list_prepend (driver_info, layer);
                }
              else if ((which == SRT_TYPE_EGL_ICD || which == SRT_TYPE_VULKAN_ICD) &&
                       library_path != NULL)
                {
                  if (which == SRT_TYPE_EGL_ICD)
                    driver_info = g_list_prepend (driver_info, srt_egl_icd_new (json_path,
                                                                                library_path,
                                                                                issues));
                  else if (which == SRT_TYPE_VULKAN_ICD)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_icd_new (json_path,
                                                                                   api_version,
                                                                                   library_path,
                                                                                   issues));
                  else
                    g_return_val_if_reached (NULL);
                }
              else
                {
                  if (error_domain == 0)
                    {
                      error_domain = G_IO_ERROR;
                      error_code = G_IO_ERROR_FAILED;
                    }
                  g_set_error_literal (&error,
                                       error_domain,
                                       error_code,
                                       error_message);
                  if (which == SRT_TYPE_EGL_ICD)
                    driver_info = g_list_prepend (driver_info, srt_egl_icd_new_error (json_path,
                                                                                      issues,
                                                                                      error));
                  else if (which == SRT_TYPE_VULKAN_ICD)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_icd_new_error (json_path,
                                                                                         issues,
                                                                                         error));
                  else if (which == SRT_TYPE_VULKAN_LAYER)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_layer_new_error (json_path,
                                                                                           issues,
                                                                                           error));
                  else
                    g_return_val_if_reached (NULL);

                  g_clear_error (&error);
                }
            }
        }
    }
out:
  return g_list_reverse (driver_info);
}

/*
 * _srt_get_egl_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for "egl" property
 *
 * Returns: A list of #SrtEglIcd that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_get_egl_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_EGL_ICD, FALSE);
}

/*
 * _srt_get_explicit_vulkan_layers_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "explicit_layers" property in "vulkan"
 *
 * Returns: A list of explicit #SrtVulkanLayer that have been found, or %NULL
 *  if none has been found.
 */
GList *
_srt_get_explicit_vulkan_layers_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_VULKAN_LAYER, TRUE);
}

/*
 * _srt_get_implicit_vulkan_layers_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "implicit_layers" property in "vulkan"
 *
 * Returns: A list of implicit #SrtVulkanLayer that have been found, or %NULL
 *  if none has been found.
 */
GList *
_srt_get_implicit_vulkan_layers_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_VULKAN_LAYER, FALSE);
}

/*
 * _srt_get_vulkan_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for "vulkan" property
 *
 * Returns: A list of #SrtVulkanIcd that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_get_vulkan_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_VULKAN_ICD, FALSE);
}
