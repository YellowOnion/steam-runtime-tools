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
