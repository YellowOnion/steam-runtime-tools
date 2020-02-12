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
 * The output is a JSON object with the following keys:
 *
 * can-write-uinput:
 *   The values are boolean: true if we can write to `/dev/uninput`,
 *   false if we are not able to do it.
 *
 * architectures:
 *   An object. The keys are multiarch tuples like %SRT_ABI_I386,
 *   as used in Debian and the freedesktop.org SDK runtime.
 *   The values are objects with more details of the architecture:
 *
 *   can-run:
 *     The values are boolean: true if we can definitely run
 *     executables of this architecture, or false if we cannot prove
 *     that we can do so.
 *
 *   library-issues-summary:
 *     A string array listing all the libraries problems that has been found
 *     in the running system. Possible values can be: "cannot-load",
 *     "missing-symbols", "misversioned-symbols" and "internal-error".
 *     If "can-run" for this architecture is false we skip the library check
 *     and this "library-issues-summary" will not be printed at all.
 *
 *   library-details:
 *     An object. The keys are library SONAMEs, such as `libjpeg.so.62`.
 *     The values are objects with more details of the library:
 *
 *     path:
 *       The value is a string representing the full path about where the
 *       @library has been found. The value is `null` if the library is
 *       not available in the system.
 *
 *     messages:
 *       If present, the value is a string containing diagnostic messages
 *       that were encountered when attempting to load the library.
 *       This member will only be available if there were some messages.
 *
 *     issues:
 *       A string array listing all the @library problems that has been
 *       found in the running system. For possible values check
 *       `library-issues-summary`. This object will be available only if
 *       there are some issues.
 *
 *     missing-symbols:
 *       A string array listing all the symbols that were expected to be
 *       provided by @library but were not found. This object will be
 *       available only if there are sone missing symbols.
 *
 *     misversioned-symbols:
 *       A string array listing all the symbols that were expected to be
 *       provided by @library but were available with a different version.
 *       This object will be available only if there are some misversioned
 *       symbols.
 *
 * graphics:
 *   An object. The keys are multiarch tuples like %SRT_ABI_I386,
 *   as used in Debian and the freedesktop.org SDK runtime.
 *   The values are objects with more details of the graphics results:
 *
 * locale-issues:
 *  A string array listing locale-related issues.
 *
 * locales:
 *  An object. The keys are either `<default>` (representing passing
 *  the empty string to `setlocale()`), or locale names that can be
 *  requested with `setlocale()`. They will include at least `C`,
 *  `C.UTF-8`, `en_US.UTF-8` and `<default>`, and may include more
 *  in future versions of steam-runtime-tools. The values are objects
 *  containing either:
 *
 *    error:
 *      A string: The error that was encountered when trying to
 *      set this locale
 *    error-domain:
 *      A string: The GError domain
 *    error-code:
 *      An integer: The GError code
 *
 *  or:
 *
 *    resulting-name:
 *      A string: the locale name as returned by setlocale(), if
 *      different
 *    charset:
 *      A string: the character set
 *    is_utf8:
 *      A boolean: whether the character set is UTF-8
 */

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <glib.h>

#include <json-glib/json-glib.h>

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

static FILE *
divert_stdout_to_stderr (GError **error)
{
  int original_stdout_fd;
  FILE *original_stdout;

  /* Duplicate the original stdout so that we still have a way to write
   * machine-readable output. */
  original_stdout_fd = dup (STDOUT_FILENO);

  if (original_stdout_fd < 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to duplicate fd %d: %s",
                   STDOUT_FILENO, g_strerror (saved_errno));
      return NULL;
    }

  /* If something like g_debug writes to stdout, make it come out of
   * our original stderr. */
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    {
      int saved_errno = errno;

      close (original_stdout_fd);
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to make fd %d a copy of fd %d: %s",
                   STDOUT_FILENO, STDERR_FILENO, g_strerror (saved_errno));
      return NULL;
    }

  /* original_stdout takes ownership of original_stdout_fd on success */
  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    {
      int saved_errno = errno;

      close (original_stdout_fd);
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to create a stdio wrapper for fd %d: %s",
                   original_stdout_fd, g_strerror (saved_errno));
      return NULL;
    }

  return original_stdout;
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
jsonify_graphics_library_vendor (JsonBuilder *builder,
                                 SrtGraphicsLibraryVendor vendor)
{
  const char *s = srt_enum_value_to_nick (SRT_TYPE_GRAPHICS_LIBRARY_VENDOR, vendor);

  if (s != NULL)
    {
      json_builder_add_string_value (builder, s);
    }
  else
    {
      gchar *fallback = g_strdup_printf ("(unknown value %d)", vendor);

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
print_libraries_details (JsonBuilder *builder,
                         GList *libraries,
                         gboolean verbose)
{
  json_builder_set_member_name (builder, "library-details");
  json_builder_begin_object (builder);
  for (GList *l = libraries; l != NULL; l = l->next)
    {
      if (verbose || srt_library_get_issues (l->data) != SRT_LIBRARY_ISSUES_NONE)
        {
          const char *messages;
          const char * const *missing_symbols;
          const char * const *misversioned_symbols;
          json_builder_set_member_name (builder, srt_library_get_soname (l->data));
          json_builder_begin_object (builder);

          messages = srt_library_get_messages (l->data);

          if (messages != NULL)
            {
              json_builder_set_member_name (builder, "messages");
              json_builder_add_string_value (builder, messages);
            }

          json_builder_set_member_name (builder, "path");
          json_builder_add_string_value (builder, srt_library_get_absolute_path (l->data));

          if (srt_library_get_issues (l->data) != SRT_LIBRARY_ISSUES_NONE)
            {
              json_builder_set_member_name (builder, "issues");
              json_builder_begin_array (builder);
              jsonify_library_issues (builder, srt_library_get_issues (l->data));
              json_builder_end_array (builder);

              int exit_status = srt_library_get_exit_status (l->data);
              if (exit_status != 0 && exit_status != -1)
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
          if (missing_symbols[0] != NULL)
            {
              json_builder_set_member_name (builder, "missing-symbols");
              json_builder_begin_array (builder);
              for (gsize i = 0; missing_symbols[i] != NULL; i++)
                json_builder_add_string_value (builder, missing_symbols[i]);
              json_builder_end_array (builder);
            }

          misversioned_symbols = srt_library_get_misversioned_symbols (l->data);
          if (misversioned_symbols[0] != NULL)
            {
              json_builder_set_member_name (builder, "misversioned-symbols");
              json_builder_begin_array (builder);
              for (gsize i = 0; misversioned_symbols[i] != NULL; i++)
                json_builder_add_string_value (builder, misversioned_symbols[i]);
              json_builder_end_array (builder);
            }

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

      json_builder_set_member_name (builder, parameters);
      json_builder_begin_object (builder);

      messages = srt_graphics_get_messages (g->data);

      if (messages != NULL)
        {
          json_builder_set_member_name (builder, "messages");
          json_builder_add_string_value (builder, messages);
        }

      json_builder_set_member_name (builder, "renderer");
      json_builder_add_string_value (builder, srt_graphics_get_renderer_string (g->data));
      json_builder_set_member_name (builder, "version");
      json_builder_add_string_value (builder, srt_graphics_get_version_string (g->data));
      if (srt_graphics_get_rendering_interface (g->data) != SRT_RENDERING_INTERFACE_VULKAN)
        {
          json_builder_set_member_name (builder, "library-vendor");
          srt_graphics_library_is_vendor_neutral (g->data, &library_vendor);
          jsonify_graphics_library_vendor (builder, library_vendor);
        }

      if (srt_graphics_get_issues (g->data) != SRT_GRAPHICS_ISSUES_NONE)
        {
          json_builder_set_member_name (builder, "issues");
          json_builder_begin_array (builder);
          jsonify_graphics_issues (builder, srt_graphics_get_issues (g->data));
          json_builder_end_array (builder);
          int exit_status = srt_graphics_get_exit_status (g->data);
          if (exit_status != 0 && exit_status != -1)
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
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, srt_dri_driver_get_library_path (iter->data));
          if (srt_dri_driver_is_extra (iter->data))
            {
              json_builder_set_member_name (builder, "is_extra");
              json_builder_add_boolean_value (builder, TRUE);
            }
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
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, srt_va_api_driver_get_library_path (iter->data));
          if (srt_va_api_driver_is_extra (iter->data))
            {
              json_builder_set_member_name (builder, "is_extra");
              json_builder_add_boolean_value (builder, TRUE);
            }
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
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "library_path");
          json_builder_add_string_value (builder, srt_vdpau_driver_get_library_path (iter->data));
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
          json_builder_end_object (builder);
        }
    }
  json_builder_end_array (builder); // End vdpau_drivers
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
  char *expectations = NULL;
  gboolean verbose = FALSE;
  JsonBuilder *builder;
  JsonGenerator *generator;
  gboolean can_run = FALSE;
  gchar *json_output;
  gchar *version = NULL;
  gchar *inst_path = NULL;
  gchar *data_path = NULL;
  gchar *rt_path = NULL;
  gchar **overrides = NULL;
  gchar **messages = NULL;
  gchar **values = NULL;
  int opt;
  static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64, NULL };
  GList *icds;
  const GList *icd_iter;
  SrtDriverFlags extra_driver_flags = SRT_DRIVER_FLAGS_INCLUDE_ALL;

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
  original_stdout = divert_stdout_to_stderr (&error);

  if (original_stdout == NULL)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return 1;
    }

  _srt_unblock_signals ();

  info = srt_system_info_new (expectations);

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

      if (g_strcmp0 (rt_path, "/") == 0)
        {
          overrides = srt_system_info_list_pressure_vessel_overrides (info, &messages);

          json_builder_set_member_name (builder, "overrides");
          json_builder_begin_object (builder);
          if (overrides != NULL && overrides[0] != NULL)
            {
              json_builder_set_member_name (builder, "list");
              json_builder_begin_array (builder);
              for (gsize i = 0; overrides[i] != NULL; i++)
                json_builder_add_string_value (builder, overrides[i]);
              json_builder_end_array (builder);
            }
          if (messages != NULL && messages[0] != NULL)
            {
              json_builder_set_member_name (builder, "messages");
              json_builder_begin_array (builder);
              for (gsize i = 0; messages[i] != NULL; i++)
                json_builder_add_string_value (builder, messages[i]);
              json_builder_end_array (builder);
            }
          json_builder_end_object (builder);

          g_strfreev (overrides);
          g_strfreev (messages);
        }

      if (rt_path != NULL && g_strcmp0 (rt_path, "/") != 0)
      {
        values = srt_system_info_list_pinned_libs_32 (info, &messages);

        json_builder_set_member_name (builder, "pinned_libs_32");
        json_builder_begin_object (builder);
        if (values != NULL && values[0] != NULL)
          {
            json_builder_set_member_name (builder, "list");
            json_builder_begin_array (builder);
            for (gsize i = 0; values[i] != NULL; i++)
              json_builder_add_string_value (builder, values[i]);
            json_builder_end_array (builder);
          }
        if (messages != NULL && messages[0] != NULL)
          {
            json_builder_set_member_name (builder, "messages");
            json_builder_begin_array (builder);
            for (gsize i = 0; messages[i] != NULL; i++)
              json_builder_add_string_value (builder, messages[i]);
            json_builder_end_array (builder);
          }
        json_builder_end_object (builder);

        g_strfreev (values);
        g_strfreev (messages);
        values = srt_system_info_list_pinned_libs_64 (info, &messages);

        json_builder_set_member_name (builder, "pinned_libs_64");
        json_builder_begin_object (builder);
        if (values != NULL && values[0] != NULL)
          {
            json_builder_set_member_name (builder, "list");
            json_builder_begin_array (builder);
            for (gsize i = 0; values[i] != NULL; i++)
              json_builder_add_string_value (builder, values[i]);
            json_builder_end_array (builder);
          }
        if (messages != NULL && messages[0] != NULL)
          {
            json_builder_set_member_name (builder, "messages");
            json_builder_begin_array (builder);
            for (gsize i = 0; messages[i] != NULL; i++)
              json_builder_add_string_value (builder, messages[i]);
            json_builder_end_array (builder);
          }
        json_builder_end_object (builder);

        g_strfreev (values);
        g_strfreev (messages);
      }
    }
  json_builder_end_object (builder);

  json_builder_set_member_name (builder, "os-release");
  json_builder_begin_object (builder);
    {
      GStrv strv;
      gsize i;
      gchar *tmp;

      tmp = srt_system_info_dup_os_id (info);

      if (tmp != NULL)
        {
          json_builder_set_member_name (builder, "id");
          json_builder_add_string_value (builder, tmp);
          g_free (tmp);
        }

      strv = srt_system_info_dup_os_id_like (info, FALSE);

      if (strv != NULL)
        {
          json_builder_set_member_name (builder, "id_like");
          json_builder_begin_array (builder);
            {
              for (i = 0; strv[i] != NULL; i++)
                json_builder_add_string_value (builder, strv[i]);
            }
          json_builder_end_array (builder);
          g_strfreev (strv);
        }

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

  json_builder_set_member_name (builder, "driver_environment");
  json_builder_begin_array (builder);
    {
      GStrv strv = srt_system_info_list_driver_environment (info);
      if (strv != NULL)
        {
          for (gsize i = 0; strv[i] != NULL; i++)
            json_builder_add_string_value (builder, strv[i]);

          g_strfreev (strv);
        }
    }
  json_builder_end_array (builder);

  json_builder_set_member_name (builder, "architectures");
  json_builder_begin_object (builder);

  g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
    {
      GList *libraries = NULL;
      GList *dri_list = NULL;
      GList *va_api_list = NULL;
      GList *vdpau_list = NULL;

      json_builder_set_member_name (builder, multiarch_tuples[i]);
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "can-run");
      can_run = srt_system_info_can_run (info, multiarch_tuples[i]);
      json_builder_add_boolean_value (builder, can_run);

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

      json_builder_end_object (builder); // End multiarch_tuple object
      g_list_free_full (libraries, g_object_unref);
      g_list_free_full (graphics_list, g_object_unref);
      g_list_free_full (dri_list, g_object_unref);
      g_list_free_full (va_api_list, g_object_unref);
      g_list_free_full (vdpau_list, g_object_unref);
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
          json_builder_set_member_name (builder, "error-domain");
          json_builder_add_string_value (builder,
                                         g_quark_to_string (error->domain));
          json_builder_set_member_name (builder, "error-code");
          json_builder_add_int_value (builder, error->code);
          json_builder_set_member_name (builder, "error");
          json_builder_add_string_value (builder, error->message);
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
          json_builder_set_member_name (builder, "error-domain");
          json_builder_add_string_value (builder,
                                         g_quark_to_string (error->domain));
          json_builder_set_member_name (builder, "error-code");
          json_builder_add_int_value (builder, error->code);
          json_builder_set_member_name (builder, "error");
          json_builder_add_string_value (builder, error->message);
          g_clear_error (&error);
        }

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
          json_builder_set_member_name (builder, "error-domain");
          json_builder_add_string_value (builder,
                                         g_quark_to_string (error->domain));
          json_builder_set_member_name (builder, "error-code");
          json_builder_add_int_value (builder, error->code);
          json_builder_set_member_name (builder, "error");
          json_builder_add_string_value (builder, error->message);
          g_clear_error (&error);
        }

      json_builder_end_object (builder);
    }

  g_list_free_full (icds, g_object_unref);
  json_builder_end_array (builder);   // vulkan.icds
  json_builder_end_object (builder);  // vulkan

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
  g_free (rt_path);
  g_free (data_path);
  g_free (inst_path);
  g_free (version);

  return 0;
}
