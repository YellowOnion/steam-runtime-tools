/*
 * Copyright © 2019 Collabora Ltd.
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

/*
 * Output basic information about the system on which the tool is run.
 * See system-info.md for details.
 */

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <glib.h>

#include <json-glib/json-glib.h>

#include <steam-runtime-tools/json-utils-internal.h>
#include <steam-runtime-tools/utils-internal.h>

enum
{
  OPTION_HELP = 1,
  OPTION_EXPECTATION,
  OPTION_IGNORE_EXTRA_DRIVERS,
  OPTION_VERBOSE,
  OPTION_VERSION,
};

struct option long_options[] =
{
    { "expectations", required_argument, NULL, OPTION_EXPECTATION },
    { "ignore-extra-drivers", no_argument, NULL, OPTION_IGNORE_EXTRA_DRIVERS },
    { "verbose", no_argument, NULL, OPTION_VERBOSE },
    { "version", no_argument, NULL, OPTION_VERSION },
    { "help", no_argument, NULL, OPTION_HELP },
    { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS]\n",
           program_invocation_short_name);
  exit (code);
}

static void
jsonify_flags (JsonBuilder *builder,
               GType flags_type,
               unsigned int values)
{
  GFlagsClass *class;
  GFlagsValue *flags_value;

  g_return_if_fail (G_TYPE_IS_FLAGS (flags_type));

  class = g_type_class_ref (flags_type);

  while (values != 0)
    {
      flags_value = g_flags_get_first_value (class, values);

      if (flags_value == NULL)
        break;

      json_builder_add_string_value (builder, flags_value->value_nick);
      values &= ~flags_value->value;
    }

  if (values)
    {
      gchar *rest = g_strdup_printf ("0x%x", values);

      json_builder_add_string_value (builder, rest);

      g_free (rest);
    }

  g_type_class_unref (class);
}

static void
jsonify_flags_string_bool_map (JsonBuilder *builder,
                               GType flags_type,
                               unsigned int present,
                               unsigned int known)
{
  GFlagsClass *class;
  GFlagsValue *flags_value;

  g_return_if_fail (G_TYPE_IS_FLAGS (flags_type));

  class = g_type_class_ref (flags_type);

  for (flags_value = class->values; flags_value->value_name; flags_value++)
    {
      /* Skip the numerically zero flag (usually "none") */
      if (flags_value->value == 0)
        continue;

      /* Skip the unknown flag */
      if (g_strcmp0 (flags_value->value_nick, "unknown") == 0)
        {
          if ((flags_value->value & present) == flags_value->value)
            present &= ~flags_value->value;
          continue;
        }

      if ((flags_value->value & present) == flags_value->value)
        {
          json_builder_set_member_name (builder, flags_value->value_nick);
          json_builder_add_boolean_value (builder, TRUE);
          present &= ~flags_value->value;
          known &= ~flags_value->value;
        }
      else if ((flags_value->value & known) == flags_value->value)
        {
          json_builder_set_member_name (builder, flags_value->value_nick);
          json_builder_add_boolean_value (builder, FALSE);
          known &= ~flags_value->value;
        }
    }

  if (present)
    {
      gchar *rest = g_strdup_printf ("0x%x", present);

      json_builder_set_member_name (builder, rest);
      json_builder_add_boolean_value (builder, TRUE);

      g_free (rest);
    }

  if (known)
    {
      gchar *rest = g_strdup_printf ("0x%x", known);

      json_builder_set_member_name (builder, rest);
      json_builder_add_boolean_value (builder, FALSE);

      g_free (rest);
    }

  g_type_class_unref (class);
}

static void
jsonify_library_issues (JsonBuilder *builder,
                        SrtLibraryIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_LIBRARY_ISSUES, issues);
}

static void
jsonify_graphics_issues (JsonBuilder *builder,
                         SrtGraphicsIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_GRAPHICS_ISSUES, issues);
}

static void
jsonify_loadable_issues (JsonBuilder *builder,
                         SrtLoadableIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_LOADABLE_ISSUES, issues);
}

static void
jsonify_enum (JsonBuilder *builder,
              GType type,
              int value)
{
  const char *s = srt_enum_value_to_nick (type, value);

  if (s != NULL)
    {
      json_builder_add_string_value (builder, s);
    }
  else
    {
      gchar *fallback = g_strdup_printf ("(unknown value %d)", value);

      json_builder_add_string_value (builder, fallback);
      g_free (fallback);
    }
}

static void
jsonify_steam_issues (JsonBuilder *builder,
                      SrtSteamIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_STEAM_ISSUES, issues);
}

static void
jsonify_runtime_issues (JsonBuilder *builder,
                        SrtRuntimeIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_RUNTIME_ISSUES, issues);
}

static void
jsonify_locale_issues (JsonBuilder *builder,
                       SrtLocaleIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_LOCALE_ISSUES, issues);
}

static void
jsonify_xdg_portal_issues (JsonBuilder *builder,
                           SrtXdgPortalIssues issues)
{
  jsonify_flags (builder, SRT_TYPE_XDG_PORTAL_ISSUES, issues);
}

static void
jsonify_x86_features (JsonBuilder *builder,
                      SrtX86FeatureFlags present,
                      SrtX86FeatureFlags known)
{
  jsonify_flags_string_bool_map (builder, SRT_TYPE_X86_FEATURE_FLAGS, present, known);
}

static void
print_libraries_details (JsonBuilder *builder,
                         GList *libraries,
                         gboolean verbose)
{
  json_builder_set_member_name (builder, "library-details");
  json_builder_begin_object (builder);
  for (GList *l = libraries; l != NULL; l = l->next)
    {
      const char *name = srt_library_get_requested_name (l->data);
      const char *soname = srt_library_get_real_soname (l->data);

      if (verbose ||
          srt_library_get_issues (l->data) != SRT_LIBRARY_ISSUES_NONE ||
          g_strcmp0 (name, soname) != 0)
        {
          const char *messages;
          const char * const *missing_symbols;
          const char * const *misversioned_symbols;
          json_builder_set_member_name (builder, name);
          json_builder_begin_object (builder);

          messages = srt_library_get_messages (l->data);

          if (messages != NULL)
            _srt_json_builder_add_array_of_lines (builder, "messages", messages);

          _srt_json_builder_add_string_force_utf8 (builder, "soname", soname);
          _srt_json_builder_add_string_force_utf8 (builder, "path",
                                                   srt_library_get_absolute_path (l->data));

          if (srt_library_get_issues (l->data) != SRT_LIBRARY_ISSUES_NONE)
            {
              json_builder_set_member_name (builder, "issues");
              json_builder_begin_array (builder);
              jsonify_library_issues (builder, srt_library_get_issues (l->data));
              json_builder_end_array (builder);

              int exit_status = srt_library_get_exit_status (l->data);
              if (exit_status != 0)
                {
                  json_builder_set_member_name (builder, "exit-status");
                  json_builder_add_int_value (builder, exit_status);
                }

              int terminating_signal = srt_library_get_terminating_signal (l->data);
              if (terminating_signal != 0)
                {
                  json_builder_set_member_name (builder, "terminating-signal");
                  json_builder_add_int_value (builder, terminating_signal);

                  json_builder_set_member_name (builder, "terminating-signal-name");
                  json_builder_add_string_value (builder, strsignal (terminating_signal));
                }

            }

          missing_symbols = srt_library_get_missing_symbols (l->data);
          _srt_json_builder_add_strv_value (builder, "missing-symbols", missing_symbols, FALSE);

          misversioned_symbols = srt_library_get_misversioned_symbols (l->data);
          _srt_json_builder_add_strv_value (builder, "misversioned-symbols", misversioned_symbols, FALSE);

          json_builder_end_object (builder);
        }
    }
  json_builder_end_object (builder);

  return;
}

static void
print_graphics_details(JsonBuilder *builder,
                       GList *graphics_list)
{
  json_builder_set_member_name (builder, "graphics-details");
  json_builder_begin_object (builder);
  for (GList *g = graphics_list; g != NULL; g = g->next)
    {
      gchar *parameters = srt_graphics_dup_parameters_string (g->data);
      const char *messages;
      SrtGraphicsLibraryVendor library_vendor;
      SrtRenderingInterface rendering_interface;

      json_builder_set_member_name (builder, parameters);
      json_builder_begin_object (builder);

      messages = srt_graphics_get_messages (g->data);

      if (messages != NULL)
        _srt_json_builder_add_array_of_lines (builder, "messages", messages);

      json_builder_set_member_name (builder, "renderer");
      json_builder_add_string_value (builder, srt_graphics_get_renderer_string (g->data));
      json_builder_set_member_name (builder, "version");
      json_builder_add_string_value (builder, srt_graphics_get_version_string (g->data));

      rendering_interface = srt_graphics_get_rendering_interface (g->data);
      if (rendering_interface != SRT_RENDERING_INTERFACE_VULKAN &&
          rendering_interface != SRT_RENDERING_INTERFACE_VDPAU &&
          rendering_interface != SRT_RENDERING_INTERFACE_VAAPI)
        {
          json_builder_set_member_name (builder, "library-vendor");
          srt_graphics_library_is_vendor_neutral (g->data, &library_vendor);
          jsonify_enum (builder, SRT_TYPE_GRAPHICS_LIBRARY_VENDOR, library_vendor);
        }

      if (srt_graphics_get_issues (g->data) != SRT_GRAPHICS_ISSUES_NONE)
        {
          json_builder_set_member_name (builder, "issues");
          json_builder_begin_array (builder);
          jsonify_graphics_issues (builder, srt_graphics_get_issues (g->data));
          json_builder_end_array (builder);
          int exit_status = srt_graphics_get_exit_status (g->data);
          if (exit_status != 0)
            {
              json_builder_set_member_name (builder, "exit-status");
              json_builder_add_int_value (builder, exit_status);
            }

          int terminating_signal = srt_graphics_get_terminating_signal (g->data);
          if (terminating_signal != 0)
            {
              json_builder_set_member_name (builder, "terminating-signal");
              json_builder_add_int_value (builder, terminating_signal);

              json_builder_set_member_name (builder, "terminating-signal-name");
              json_builder_add_string_value (builder, strsignal (terminating_signal));
            }

        }

      if (rendering_interface == SRT_RENDERING_INTERFACE_VULKAN)
        {
          g_autoptr(SrtObjectList) devices = srt_graphics_get_devices (g->data);
          const GList *iter;
          json_builder_set_member_name (builder, "devices");
          json_builder_begin_array (builder);
            {
              for (iter = devices; iter != NULL; iter = iter->next)
                {
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, "name");
                  json_builder_add_string_value (builder,
                                                 srt_graphics_device_get_name (iter->data));
                  json_builder_set_member_name (builder, "api-version");
                  json_builder_add_string_value (builder,
                                                 srt_graphics_device_get_api_version (iter->data));
                  json_builder_set_member_name (builder, "driver-version");
                  json_builder_add_string_value (builder,
                                                 srt_graphics_device_get_driver_version (iter->data));
                  json_builder_set_member_name (builder, "vendor-id");
                  json_builder_add_string_value (builder,
                                                 srt_graphics_device_get_vendor_id (iter->data));
                  json_builder_set_member_name (builder, "device-id");
                  json_builder_add_string_value (builder,
                                                 srt_graphics_device_get_device_id (iter->data));
                  json_builder_set_member_name (builder, "type");
                  jsonify_enum (builder, SRT_TYPE_VK_PHYSICAL_DEVICE_TYPE,
                                srt_graphics_device_get_device_type (iter->data));

                  messages = srt_graphics_device_get_messages (iter->data);
                  if (messages != NULL)
                    _srt_json_builder_add_array_of_lines (builder, "messages", messages);

                  if (srt_graphics_device_get_issues (iter->data) != SRT_GRAPHICS_ISSUES_NONE)
                    {
                      json_builder_set_member_name (builder, "issues");
                      json_builder_begin_array (builder);
                      jsonify_graphics_issues (builder, srt_graphics_device_get_issues (iter->data));
                      json_builder_end_array (builder);
                    }
                  json_builder_end_object (builder);
                }
            }
          json_builder_end_array (builder);
        }

      json_builder_end_object (builder); // End object for parameters
      g_free (parameters);
    }
  json_builder_end_object (builder); // End garphics-details
}

static void
print_dri_details (JsonBuilder *builder,
                   GList *dri_list)
{
  GList *iter;

  json_builder_set_member_name (builder, "dri_drivers");
  json_builder_begin_array (builder);
    {
      for (iter = dri_list; iter != NULL; iter = iter->next)
        {
          const gchar *library;
          gchar *resolved = NULL;
          json_builder_begin_object (builder);
          library = srt_dri_driver_get_library_path (iter->data);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, library);
          resolved = srt_dri_driver_resolve_library_path (iter->data);
          if (g_strcmp0 (library, resolved) != 0)
            {
              json_builder_set_member_name (builder, "library_path_resolved");
              json_builder_add_string_value (builder, resolved);
            }
          if (srt_dri_driver_is_extra (iter->data))
            {
              json_builder_set_member_name (builder, "is_extra");
              json_builder_add_boolean_value (builder, TRUE);
            }
          g_free (resolved);
          json_builder_end_object (builder);
        }
    }
  json_builder_end_array (builder); // End dri_drivers
}

static void
print_va_api_details (JsonBuilder *builder,
                      GList *va_api_list)
{
  GList *iter;

  json_builder_set_member_name (builder, "va-api_drivers");
  json_builder_begin_array (builder);
    {
      for (iter = va_api_list; iter != NULL; iter = iter->next)
        {
          const gchar *library;
          gchar *resolved = NULL;
          json_builder_begin_object (builder);
          library = srt_va_api_driver_get_library_path (iter->data);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, library);
          resolved = srt_va_api_driver_resolve_library_path (iter->data);
          if (g_strcmp0 (library, resolved) != 0)
            {
              json_builder_set_member_name (builder, "library_path_resolved");
              json_builder_add_string_value (builder, resolved);
            }
          if (srt_va_api_driver_is_extra (iter->data))
            {
              json_builder_set_member_name (builder, "is_extra");
              json_builder_add_boolean_value (builder, TRUE);
            }
          g_free (resolved);
          json_builder_end_object (builder);
        }
    }
  json_builder_end_array (builder); // End va-api_drivers
}

static void
print_vdpau_details (JsonBuilder *builder,
                     GList *vdpau_list)
{
  GList *iter;

  json_builder_set_member_name (builder, "vdpau_drivers");
  json_builder_begin_array (builder);
    {
      for (iter = vdpau_list; iter != NULL; iter = iter->next)
        {
          const gchar *library;
          gchar *resolved = NULL;
          json_builder_begin_object (builder);
          library = srt_vdpau_driver_get_library_path (iter->data);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, library);
          resolved = srt_vdpau_driver_resolve_library_path (iter->data);
          if (g_strcmp0 (library, resolved) != 0)
            {
              json_builder_set_member_name (builder, "library_path_resolved");
              json_builder_add_string_value (builder, resolved);
            }
          if (srt_vdpau_driver_get_library_link (iter->data) != NULL)
            {
              json_builder_set_member_name (builder, "library_link");
              json_builder_add_string_value (builder, srt_vdpau_driver_get_library_link (iter->data));
            }
          if (srt_vdpau_driver_is_extra (iter->data))
            {
              json_builder_set_member_name (builder, "is_extra");
              json_builder_add_boolean_value (builder, TRUE);
            }
          g_free (resolved);
          json_builder_end_object (builder);
        }
    }
  json_builder_end_array (builder); // End vdpau_drivers
}

static void
jsonify_os_release (JsonBuilder *builder,
                    SrtSystemInfo *info)
{
  json_builder_set_member_name (builder, "os-release");
  json_builder_begin_object (builder);
    {
      g_auto(GStrv) strv = NULL;
      gchar *tmp;

      tmp = srt_system_info_dup_os_id (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "id");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      strv = srt_system_info_dup_os_id_like (info, FALSE);

      _srt_json_builder_add_strv_value (builder, "id_like", (const gchar * const *)strv,
                                        FALSE);

      tmp = srt_system_info_dup_os_name (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "name");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      tmp = srt_system_info_dup_os_pretty_name (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "pretty_name");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      tmp = srt_system_info_dup_os_version_id (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "version_id");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      tmp = srt_system_info_dup_os_version_codename (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "version_codename");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      tmp = srt_system_info_dup_os_build_id (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "build_id");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      tmp = srt_system_info_dup_os_variant_id (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "variant_id");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      tmp = srt_system_info_dup_os_variant (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "variant");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }
    }
  json_builder_end_object (builder);
}

static void
jsonify_container (JsonBuilder *builder,
                   SrtSystemInfo *info)
{
  SrtContainerInfo *container_info = srt_system_info_check_container (info);
  SrtContainerType type = SRT_CONTAINER_TYPE_UNKNOWN;
  const gchar *flatpak_version = NULL;
  const gchar *host_directory = NULL;
  g_autoptr(SrtSystemInfo) host = NULL;

  type = srt_container_info_get_container_type (container_info);
  flatpak_version = srt_container_info_get_flatpak_version (container_info);
  host_directory = srt_container_info_get_container_host_directory (container_info);

  json_builder_set_member_name (builder, "container");
  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "type");
      jsonify_enum (builder, SRT_TYPE_CONTAINER_TYPE, type);

      if (type != SRT_CONTAINER_TYPE_NONE)
        {
          if (flatpak_version != NULL)
            {
              json_builder_set_member_name (builder, "flatpak_version");
              json_builder_add_string_value (builder, flatpak_version);
            }

          json_builder_set_member_name (builder, "host");
          json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "path");
              json_builder_add_string_value (builder, host_directory);

              if (host_directory != NULL)
                {
                  host = srt_system_info_new (NULL);
                  srt_system_info_set_sysroot (host, host_directory);
                  jsonify_os_release (builder, host);
                }
            }
          json_builder_end_object (builder);
        }
    }
  json_builder_end_object (builder);
}

static void
print_glx_details (JsonBuilder *builder,
                   GList *glx_list)
{
  GList *iter;

  json_builder_set_member_name (builder, "glx_drivers");
  json_builder_begin_array (builder);
    {
      for (iter = glx_list; iter != NULL; iter = iter->next)
        {
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "library_soname");
          json_builder_add_string_value (builder, srt_glx_icd_get_library_soname (iter->data));
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, srt_glx_icd_get_library_path (iter->data));
          json_builder_end_object (builder);
        }
    }
  json_builder_end_array (builder); // End glx_drivers
}

static void
print_layer_details (JsonBuilder *builder,
                     GList *layer_list,
                     gboolean explicit)
{
  GList *iter;
  const gchar *member_name;
  const gchar *library_path;
  const gchar *const *component_layers;
  SrtLoadableIssues loadable_issues = SRT_LOADABLE_ISSUES_NONE;

  if (explicit)
    member_name = "explicit_layers";
  else
    member_name = "implicit_layers";

  json_builder_set_member_name (builder, member_name);
  json_builder_begin_array (builder);
    {
      for (iter = layer_list; iter != NULL; iter = iter->next)
      {
        g_autoptr(GError) error = NULL;
        json_builder_begin_object (builder);
        json_builder_set_member_name (builder, "json_path");
        json_builder_add_string_value (builder,
                                      srt_vulkan_layer_get_json_path (iter->data));
        if (srt_vulkan_layer_check_error (iter->data, &error))
          {
            json_builder_set_member_name (builder, "name");
            json_builder_add_string_value (builder,
                                          srt_vulkan_layer_get_name (iter->data));
            json_builder_set_member_name (builder, "description");
            json_builder_add_string_value (builder,
                                          srt_vulkan_layer_get_description (iter->data));
            json_builder_set_member_name (builder, "type");
            json_builder_add_string_value (builder,
                                          srt_vulkan_layer_get_type_value (iter->data));
            json_builder_set_member_name (builder, "api_version");
            json_builder_add_string_value (builder,
                                          srt_vulkan_layer_get_api_version (iter->data));
            json_builder_set_member_name (builder, "implementation_version");
            json_builder_add_string_value (builder,
                                          srt_vulkan_layer_get_implementation_version (iter->data));
            library_path = srt_vulkan_layer_get_library_path (iter->data);
            if (library_path != NULL)
              {
                g_autofree gchar *tmp = NULL;

                json_builder_set_member_name (builder, "library_path");
                json_builder_add_string_value (builder, library_path);

                tmp = srt_vulkan_layer_resolve_library_path (iter->data);
                if (g_strcmp0 (library_path, tmp) != 0)
                  {
                    json_builder_set_member_name (builder, "dlopen");
                    json_builder_add_string_value (builder, tmp);
                  }
              }
            component_layers = srt_vulkan_layer_get_component_layers (iter->data);
            _srt_json_builder_add_strv_value (builder, "component_layers",
                                              (const gchar * const *) component_layers,
                                              FALSE);
          }
        else
          {
            json_builder_set_member_name (builder, "error-domain");
            json_builder_add_string_value (builder,
                                          g_quark_to_string (error->domain));
            json_builder_set_member_name (builder, "error-code");
            json_builder_add_int_value (builder, error->code);
            json_builder_set_member_name (builder, "error");
            json_builder_add_string_value (builder, error->message);
          }
        json_builder_set_member_name (builder, "issues");
        json_builder_begin_array (builder);
        loadable_issues = srt_vulkan_layer_get_issues (iter->data);
        jsonify_loadable_issues (builder, loadable_issues);
        json_builder_end_array (builder);
        json_builder_end_object (builder);
      }
    }
  json_builder_end_array (builder);
}

static const char * const locales[] =
{
  "",
  "C",
  "C.UTF-8",
  "en_US.UTF-8",
};

int
main (int argc,
      char **argv)
{
  FILE *original_stdout = NULL;
  GError *error = NULL;
  SrtSystemInfo *info;
  SrtLibraryIssues library_issues = SRT_LIBRARY_ISSUES_NONE;
  SrtSteamIssues steam_issues = SRT_STEAM_ISSUES_NONE;
  SrtRuntimeIssues runtime_issues = SRT_RUNTIME_ISSUES_NONE;
  SrtLocaleIssues locale_issues = SRT_LOCALE_ISSUES_NONE;
  SrtLoadableIssues loadable_issues = SRT_LOADABLE_ISSUES_NONE;
  SrtXdgPortalIssues xdg_portal_issues = SRT_XDG_PORTAL_ISSUES_NONE;
  SrtX86FeatureFlags x86_features = SRT_X86_FEATURE_NONE;
  SrtX86FeatureFlags known_x86_features = SRT_X86_FEATURE_NONE;
  g_autoptr(SrtObjectList) portal_backends = NULL;
  g_autoptr(SrtObjectList) portal_interfaces = NULL;
  g_autoptr(SrtObjectList) explicit_layers = NULL;
  g_autoptr(SrtObjectList) implicit_layers = NULL;
  g_auto(GStrv) driver_environment = NULL;
  char *expectations = NULL;
  gboolean verbose = FALSE;
  JsonBuilder *builder;
  JsonGenerator *generator;
  gboolean can_run = FALSE;
  const gchar *test_json_path = NULL;
  g_autofree gchar *steamscript_path = NULL;
  g_autofree gchar *steamscript_version = NULL;
  g_autofree gchar *xdg_portal_messages = NULL;
  gchar *json_output;
  gchar *version = NULL;
  gchar *inst_path = NULL;
  gchar *data_path = NULL;
  gchar *bin32_path = NULL;
  gchar *rt_path = NULL;
  gchar **overrides = NULL;
  gchar **messages = NULL;
  gchar **values = NULL;
  int opt;

#if defined(__i386__) || defined(__x86_64__)
  static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64, NULL };
#elif defined(_SRT_MULTIARCH)
  static const char * const multiarch_tuples[] = { _SRT_MULTIARCH, NULL };
#else
#warning Unknown architecture, steam-runtime-system-info will assume x86
  static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64, NULL };
#endif

  GList *icds;
  GList *desktop_entries;
  const GList *icd_iter;
  SrtDriverFlags extra_driver_flags = SRT_DRIVER_FLAGS_INCLUDE_ALL;

  _srt_setenv_disable_gio_modules ();

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_EXPECTATION:
            expectations = optarg;
            break;

          case OPTION_VERBOSE:
            verbose = TRUE;
            break;

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                argv[0], VERSION);
            return 0;

          case OPTION_IGNORE_EXTRA_DRIVERS:
            extra_driver_flags = SRT_DRIVER_FLAGS_NONE;
            break;

          case OPTION_HELP:
            usage (0);
            break;

          case '?':
          default:
            usage (1);
            break;  /* not reached */
        }
    }

  if (optind != argc)
    usage (1);

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (&error);

  if (original_stdout == NULL)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return 1;
    }

  _srt_unblock_signals ();

  test_json_path = g_getenv ("SRT_TEST_PARSE_JSON");

  if (test_json_path)
    {
      /* Get the system info from a JSON, used for unit testing */
      info = srt_system_info_new_from_json (test_json_path, &error);
      if (info == NULL)
        {
          g_warning ("%s", error->message);
          g_clear_error (&error);
          return 1;
        }
    }
  else
    {
      info = srt_system_info_new (expectations);

      /* For unit testing */
      srt_system_info_set_sysroot (info, g_getenv ("SRT_TEST_SYSROOT"));
    }

  builder = json_builder_new ();
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "can-write-uinput");
  json_builder_add_boolean_value (builder, srt_system_info_can_write_to_uinput (info));

  json_builder_set_member_name (builder, "steam-installation");
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "path");
  inst_path = srt_system_info_dup_steam_installation_path (info);
  json_builder_add_string_value (builder, inst_path);
  json_builder_set_member_name (builder, "data_path");
  data_path = srt_system_info_dup_steam_data_path (info);
  json_builder_add_string_value (builder, data_path);
  json_builder_set_member_name (builder, "bin32_path");
  bin32_path = srt_system_info_dup_steam_bin32_path (info);
  json_builder_add_string_value (builder, bin32_path);
  json_builder_set_member_name (builder, "steamscript_path");
  steamscript_path = srt_system_info_dup_steamscript_path (info);
  json_builder_add_string_value (builder, steamscript_path);
  json_builder_set_member_name (builder, "steamscript_version");
  steamscript_version = srt_system_info_dup_steamscript_version (info);
  json_builder_add_string_value (builder, steamscript_version);

  json_builder_set_member_name (builder, "issues");
  json_builder_begin_array (builder);
  steam_issues = srt_system_info_get_steam_issues (info);
  jsonify_steam_issues (builder, steam_issues);
  json_builder_end_array (builder);
  json_builder_end_object (builder);

  json_builder_set_member_name (builder, "runtime");
  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "path");
      rt_path = srt_system_info_dup_runtime_path (info);
      json_builder_add_string_value (builder, rt_path);
      json_builder_set_member_name (builder, "version");
      version = srt_system_info_dup_runtime_version (info);
      json_builder_add_string_value (builder, version);
      json_builder_set_member_name (builder, "issues");
      json_builder_begin_array (builder);
      runtime_issues = srt_system_info_get_runtime_issues (info);
      jsonify_runtime_issues (builder, runtime_issues);
      json_builder_end_array (builder);

      overrides = srt_system_info_list_pressure_vessel_overrides (info, &messages);

      if ((overrides != NULL && overrides[0] != NULL)
          || (messages != NULL && messages[0] != NULL))
        {
          json_builder_set_member_name (builder, "overrides");
          json_builder_begin_object (builder);

          _srt_json_builder_add_strv_value (builder, "list",
                                            (const gchar * const *)overrides,
                                            FALSE);

          _srt_json_builder_add_strv_value (builder, "messages",
                                            (const gchar * const *)messages,
                                            FALSE);

          json_builder_end_object (builder);

          g_strfreev (overrides);
          g_strfreev (messages);
        }

      if (rt_path != NULL && g_strcmp0 (rt_path, "/") != 0)
      {
        values = srt_system_info_list_pinned_libs_32 (info, &messages);

        json_builder_set_member_name (builder, "pinned_libs_32");
        json_builder_begin_object (builder);

        _srt_json_builder_add_strv_value (builder, "list",
                                          (const gchar * const *)values,
                                          FALSE);

        _srt_json_builder_add_strv_value (builder, "messages",
                                          (const gchar * const *)messages,
                                          FALSE);

        json_builder_end_object (builder);

        g_strfreev (values);
        g_strfreev (messages);
        values = srt_system_info_list_pinned_libs_64 (info, &messages);

        json_builder_set_member_name (builder, "pinned_libs_64");
        json_builder_begin_object (builder);

        _srt_json_builder_add_strv_value (builder, "list",
                                          (const gchar * const *)values,
                                          FALSE);

        _srt_json_builder_add_strv_value (builder, "messages",
                                          (const gchar * const *)messages,
                                          FALSE);

        json_builder_end_object (builder);

        g_strfreev (values);
        g_strfreev (messages);
      }
    }
  json_builder_end_object (builder);

  jsonify_os_release (builder, info);
  jsonify_container (builder, info);

  driver_environment = srt_system_info_list_driver_environment (info);
  _srt_json_builder_add_strv_value (builder, "driver_environment",
                                    (const gchar * const *)driver_environment,
                                    TRUE);

  json_builder_set_member_name (builder, "architectures");
  json_builder_begin_object (builder);

  g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
    {
      GList *libraries = NULL;
      GList *dri_list = NULL;
      GList *va_api_list = NULL;
      GList *vdpau_list = NULL;
      GList *glx_list = NULL;
      g_autofree gchar *libdl_lib = NULL;
      g_autofree gchar *libdl_platform = NULL;
      g_autoptr(GError) libdl_lib_error = NULL;
      g_autoptr(GError) libdl_platform_error = NULL;
      const char *ld_so;

      json_builder_set_member_name (builder, multiarch_tuples[i]);
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "can-run");
      can_run = srt_system_info_can_run (info, multiarch_tuples[i]);
      json_builder_add_boolean_value (builder, can_run);

      json_builder_set_member_name (builder, "libdl-LIB");
      libdl_lib = srt_system_info_dup_libdl_lib (info, multiarch_tuples[i],
                                                 &libdl_lib_error);
      if (libdl_lib != NULL)
        {
          json_builder_add_string_value (builder, libdl_lib);
        }
      else
        {
          json_builder_begin_object (builder);
          _srt_json_builder_add_error_members (builder, libdl_lib_error);
          json_builder_end_object (builder);
        }

      json_builder_set_member_name (builder, "libdl-PLATFORM");
      libdl_platform = srt_system_info_dup_libdl_platform (info,
                                                           multiarch_tuples[i],
                                                           &libdl_platform_error);
      if (libdl_platform != NULL)
        {
          json_builder_add_string_value (builder, libdl_platform);
        }
      else
        {
          json_builder_begin_object (builder);
          _srt_json_builder_add_error_members (builder, libdl_platform_error);
          json_builder_end_object (builder);
        }

      ld_so = srt_architecture_get_expected_runtime_linker (multiarch_tuples[i]);

      if (ld_so != NULL)
        {
          json_builder_set_member_name (builder, "runtime-linker");
          json_builder_begin_object (builder);
            {
              g_autoptr(GError) local_error = NULL;
              g_autofree gchar *real = NULL;

              json_builder_set_member_name (builder, "path");
              json_builder_add_string_value (builder, ld_so);

              if (srt_system_info_check_runtime_linker (info,
                                                        multiarch_tuples[i],
                                                        &real, &local_error))
                {
                  json_builder_set_member_name (builder, "resolved");
                  json_builder_add_string_value (builder, real);
                }
              else
                {
                  _srt_json_builder_add_error_members (builder, local_error);
                }
            }
          json_builder_end_object (builder);
        }

      if (can_run)
        {
          json_builder_set_member_name (builder, "library-issues-summary");
          json_builder_begin_array (builder);
          library_issues = srt_system_info_check_libraries (info,
                                                            multiarch_tuples[i],
                                                            &libraries);
          jsonify_library_issues (builder, library_issues);
          json_builder_end_array (builder);
        }

      if (libraries != NULL && (library_issues != SRT_LIBRARY_ISSUES_NONE || verbose))
          print_libraries_details (builder, libraries, verbose);

      GList *graphics_list = srt_system_info_check_all_graphics (info,
                                                                 multiarch_tuples[i]);
      print_graphics_details (builder, graphics_list);

      dri_list = srt_system_info_list_dri_drivers (info, multiarch_tuples[i],
                                                   extra_driver_flags);
      print_dri_details (builder, dri_list);

      va_api_list = srt_system_info_list_va_api_drivers (info, multiarch_tuples[i],
                                                         extra_driver_flags);
      print_va_api_details (builder, va_api_list);

      vdpau_list = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[i],
                                                       extra_driver_flags);
      print_vdpau_details (builder, vdpau_list);

      glx_list = srt_system_info_list_glx_icds (info, multiarch_tuples[i],
                                                SRT_DRIVER_FLAGS_INCLUDE_ALL);
      print_glx_details (builder, glx_list);

      json_builder_end_object (builder); // End multiarch_tuple object
      g_list_free_full (libraries, g_object_unref);
      g_list_free_full (graphics_list, g_object_unref);
      g_list_free_full (dri_list, g_object_unref);
      g_list_free_full (va_api_list, g_object_unref);
      g_list_free_full (vdpau_list, g_object_unref);
      g_list_free_full (glx_list, g_object_unref);
    }

  json_builder_end_object (builder);

  json_builder_set_member_name (builder, "locale-issues");
  json_builder_begin_array (builder);
  locale_issues = srt_system_info_get_locale_issues (info);
  jsonify_locale_issues (builder, locale_issues);
  json_builder_end_array (builder);

  json_builder_set_member_name (builder, "locales");
  json_builder_begin_object (builder);

  for (gsize i = 0; i < G_N_ELEMENTS (locales); i++)
    {
      SrtLocale *locale = srt_system_info_check_locale (info, locales[i],
                                                        &error);

      if (locales[i][0] == '\0')
        json_builder_set_member_name (builder, "<default>");
      else
        json_builder_set_member_name (builder, locales[i]);

      json_builder_begin_object (builder);

      if (locale != NULL)
        {
          json_builder_set_member_name (builder, "resulting-name");
          json_builder_add_string_value (builder,
                                         srt_locale_get_resulting_name (locale));
          json_builder_set_member_name (builder, "charset");
          json_builder_add_string_value (builder,
                                         srt_locale_get_charset (locale));
          json_builder_set_member_name (builder, "is_utf8");
          json_builder_add_boolean_value (builder,
                                          srt_locale_is_utf8 (locale));
        }
      else
        {
          _srt_json_builder_add_error_members (builder, error);
        }

      json_builder_end_object (builder);
      g_clear_object (&locale);
      g_clear_error (&error);
    }

  json_builder_end_object (builder);

  json_builder_set_member_name (builder, "egl");
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "icds");
  json_builder_begin_array (builder);
  icds = srt_system_info_list_egl_icds (info, multiarch_tuples);

  for (icd_iter = icds; icd_iter != NULL; icd_iter = icd_iter->next)
    {
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "json_path");
      json_builder_add_string_value (builder,
                                     srt_egl_icd_get_json_path (icd_iter->data));

      if (srt_egl_icd_check_error (icd_iter->data, &error))
        {
          const gchar *library;
          gchar *tmp;

          library = srt_egl_icd_get_library_path (icd_iter->data);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, library);

          tmp = srt_egl_icd_resolve_library_path (icd_iter->data);

          if (g_strcmp0 (library, tmp) != 0)
            {
              json_builder_set_member_name (builder, "dlopen");
              json_builder_add_string_value (builder, tmp);
            }

          g_free (tmp);
        }
      else
        {
          _srt_json_builder_add_error_members (builder, error);
          g_clear_error (&error);
        }

      json_builder_set_member_name (builder, "issues");
      json_builder_begin_array (builder);
      loadable_issues = srt_egl_icd_get_issues (icd_iter->data);
      jsonify_loadable_issues (builder, loadable_issues);
      json_builder_end_array (builder);

      json_builder_end_object (builder);
    }

  g_list_free_full (icds, g_object_unref);
  json_builder_end_array (builder);   // egl.icds
  json_builder_end_object (builder);  // egl

  json_builder_set_member_name (builder, "vulkan");
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "icds");
  json_builder_begin_array (builder);
  icds = srt_system_info_list_vulkan_icds (info, multiarch_tuples);

  for (icd_iter = icds; icd_iter != NULL; icd_iter = icd_iter->next)
    {
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "json_path");
      json_builder_add_string_value (builder,
                                     srt_vulkan_icd_get_json_path (icd_iter->data));

      if (srt_vulkan_icd_check_error (icd_iter->data, &error))
        {
          const gchar *library;
          gchar *tmp;

          library = srt_vulkan_icd_get_library_path (icd_iter->data);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, library);
          json_builder_set_member_name (builder, "api_version");
          json_builder_add_string_value (builder,
                                         srt_vulkan_icd_get_api_version (icd_iter->data));

          tmp = srt_vulkan_icd_resolve_library_path (icd_iter->data);

          if (g_strcmp0 (library, tmp) != 0)
            {
              json_builder_set_member_name (builder, "dlopen");
              json_builder_add_string_value (builder, tmp);
            }

          g_free (tmp);
        }
      else
        {
          _srt_json_builder_add_error_members (builder, error);
          g_clear_error (&error);
        }

      json_builder_set_member_name (builder, "issues");
      json_builder_begin_array (builder);
      loadable_issues = srt_vulkan_icd_get_issues (icd_iter->data);
      jsonify_loadable_issues (builder, loadable_issues);
      json_builder_end_array (builder);

      json_builder_end_object (builder);
    }

  g_list_free_full (icds, g_object_unref);
  json_builder_end_array (builder);   // vulkan.icds

  explicit_layers = srt_system_info_list_explicit_vulkan_layers (info);
  print_layer_details (builder, explicit_layers, TRUE);

  implicit_layers = srt_system_info_list_implicit_vulkan_layers (info);
  print_layer_details (builder, implicit_layers, FALSE);

  json_builder_end_object (builder);  // vulkan

  json_builder_set_member_name (builder, "desktop-entries");
  json_builder_begin_array (builder);
    {
      desktop_entries = srt_system_info_list_desktop_entries (info);
      for (GList *iter = desktop_entries; iter != NULL; iter = iter->next)
        {
          json_builder_begin_object (builder);
          if (srt_desktop_entry_get_id (iter->data) != NULL)
            {
              json_builder_set_member_name (builder, "id");
              json_builder_add_string_value (builder, srt_desktop_entry_get_id (iter->data));
            }

          if (srt_desktop_entry_get_commandline (iter->data) != NULL)
            {
              json_builder_set_member_name (builder, "commandline");
              json_builder_add_string_value (builder, srt_desktop_entry_get_commandline (iter->data));
            }

          if (srt_desktop_entry_get_filename (iter->data) != NULL)
            {
              json_builder_set_member_name (builder, "filename");
              json_builder_add_string_value (builder, srt_desktop_entry_get_filename (iter->data));
            }

          json_builder_set_member_name (builder, "default_steam_uri_handler");
          json_builder_add_boolean_value (builder, srt_desktop_entry_is_default_handler (iter->data));

          json_builder_set_member_name (builder, "steam_uri_handler");
          json_builder_add_boolean_value (builder, srt_desktop_entry_is_steam_handler (iter->data));

          json_builder_end_object (builder);
        }
      g_list_free_full (desktop_entries, g_object_unref);
    }
  json_builder_end_array (builder);

  json_builder_set_member_name (builder, "xdg-portals");
  json_builder_begin_object (builder);
    {
      portal_interfaces = srt_system_info_list_xdg_portal_interfaces (info);
      portal_backends = srt_system_info_list_xdg_portal_backends (info);
      if (portal_interfaces != NULL || portal_backends != NULL)
        {
          json_builder_set_member_name (builder, "details");
          json_builder_begin_object (builder);

          if (portal_interfaces != NULL)
            {
              json_builder_set_member_name (builder, "interfaces");
              json_builder_begin_object (builder);
              for (const GList *iter = portal_interfaces; iter != NULL; iter = iter->next)
                {
                  gboolean is_available;
                  json_builder_set_member_name (builder, srt_xdg_portal_interface_get_name (iter->data));
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, "available");
                  is_available = srt_xdg_portal_interface_is_available (iter->data);
                  json_builder_add_boolean_value (builder, is_available);
                  if (is_available)
                    {
                      json_builder_set_member_name (builder, "version");
                      json_builder_add_int_value (builder, srt_xdg_portal_interface_get_version (iter->data));
                    }
                  json_builder_end_object (builder);
                }
              json_builder_end_object (builder);
            }

          if (portal_backends != NULL)
            {
              json_builder_set_member_name (builder, "backends");
              json_builder_begin_object (builder);
              for (const GList *iter = portal_backends; iter != NULL; iter = iter->next)
                {
                  json_builder_set_member_name (builder, srt_xdg_portal_backend_get_name (iter->data));
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, "available");
                  json_builder_add_boolean_value (builder, srt_xdg_portal_backend_is_available (iter->data));
                  json_builder_end_object (builder);
                }
              json_builder_end_object (builder);
            }
          json_builder_end_object (builder);
        }

      json_builder_set_member_name (builder, "issues");
      json_builder_begin_array (builder);
      xdg_portal_issues = srt_system_info_get_xdg_portal_issues (info, &xdg_portal_messages);
      jsonify_xdg_portal_issues (builder, xdg_portal_issues);
      json_builder_end_array (builder);

      if (xdg_portal_messages != NULL)
        _srt_json_builder_add_array_of_lines (builder, "messages", xdg_portal_messages);
    }
  json_builder_end_object (builder);

  json_builder_set_member_name (builder, "cpu-features");
  json_builder_begin_object (builder);
    {
      known_x86_features = srt_system_info_get_known_x86_features (info);
      x86_features = srt_system_info_get_x86_features (info);
      jsonify_x86_features (builder, x86_features, known_x86_features);
    }
  json_builder_end_object (builder);

  json_builder_end_object (builder); // End global object

  JsonNode *root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_output = json_generator_to_data (generator, NULL);

  if (fputs (json_output, original_stdout) < 0)
    g_warning ("Unable to write output: %s", g_strerror (errno));

  if (fputs ("\n", original_stdout) < 0)
    g_warning ("Unable to write final newline: %s", g_strerror (errno));

  if (fclose (original_stdout) != 0)
    g_warning ("Unable to close stdout: %s", g_strerror (errno));

  g_free (json_output);
  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);
  g_object_unref (info);
  g_free (bin32_path);
  g_free (rt_path);
  g_free (data_path);
  g_free (inst_path);
  g_free (version);

  return 0;
}
