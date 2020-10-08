/*
 * Copyright © 2020 Collabora Ltd.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/json-glib-compat.h>
#include <steam-runtime-tools/utils-internal.h>

#if !GLIB_CHECK_VERSION(2, 43, 4)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GDBusProxy, g_object_unref)
#endif

static gboolean opt_print_version = FALSE;

static const GOptionEntry option_entries[] =
{
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { NULL }
};

int
main (int argc,
      char **argv)
{
  g_autofree gchar *json = NULL;
  g_autoptr(FILE) original_stdout = NULL;
  g_autoptr(JsonNode) root = NULL;
  g_autoptr(JsonBuilder) builder = NULL;
  g_autoptr(JsonGenerator) generator = NULL;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GVariant) variant_version = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GOptionContext) option_context = NULL;
  guint32 version;
  GError **error = &local_error;
  gboolean impl_available = FALSE;
  int ret = EXIT_SUCCESS;
  gsize i;

  static const gchar * const portal_interface_name[] = {
    "org.freedesktop.portal.OpenURI",
    "org.freedesktop.portal.Email",
    NULL,
  };

  static const gchar * const portal_impl_name[] = {
    "org.freedesktop.impl.portal.desktop.gtk",
    "org.freedesktop.impl.portal.desktop.kde",
    NULL,
  };

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &local_error))
    return 2;

  if (opt_print_version)
    {
      /* Output version number as YAML for machine-readability,
       * inspired by `ostree --version` and `docker version` */
      g_print (
          "%s:\n"
          " Package: steam-runtime-tools\n"
          " Version: %s\n",
          argv[0], VERSION);
      return EXIT_SUCCESS;
    }

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      g_printerr ("Unable to divert stdout to stderr: %s", local_error->message);
      return EXIT_FAILURE;
    }

  builder = json_builder_new ();
  json_builder_begin_object (builder);

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
  if (connection == NULL)
    {
      g_printerr ("Unable to get the session bus: %s\n", local_error->message);
      return EXIT_FAILURE;
    }

  json_builder_set_member_name (builder, "interfaces");
  json_builder_begin_object (builder);
  for (i = 0; portal_interface_name[i] != NULL; i++)
    {
      g_autoptr(GDBusProxy) portal_proxy = NULL;
      g_autofree gchar *name_owner = NULL;
      portal_proxy = g_dbus_proxy_new_sync (connection,
                                            G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                            NULL,
                                            "org.freedesktop.portal.Desktop",
                                            "/org/freedesktop/portal/desktop",
                                            portal_interface_name[i],
                                            NULL,
                                            error);

      json_builder_set_member_name (builder, portal_interface_name[i]);
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "available");

      if (portal_proxy == NULL)
        {
          g_printerr ("Failed to contact 'org.freedesktop.portal.Desktop': %s\n",
                      local_error->message);
          json_builder_add_boolean_value (builder, FALSE);
          json_builder_end_object (builder);
          ret = EXIT_FAILURE;
          continue;
        }

      name_owner = g_dbus_proxy_get_name_owner (portal_proxy);
      if (name_owner == NULL)
        {
          g_printerr ("Failed to connect to 'org.freedesktop.portal.Desktop'\n");
          json_builder_add_boolean_value (builder, FALSE);
          json_builder_end_object (builder);
          ret = EXIT_FAILURE;
          continue;
        }

      variant_version = g_dbus_proxy_get_cached_property (portal_proxy, "version");

      if (variant_version == NULL
          || g_variant_classify (variant_version) != G_VARIANT_CLASS_UINT32)
        {
          g_printerr ("The 'version' property is not available for '%s', "
                      "either there isn't a working xdg-desktop-portal or "
                      "it is a very old version\n", portal_interface_name[i]);

          json_builder_add_boolean_value (builder, FALSE);
          ret = EXIT_FAILURE;
        }
      else
        {
          json_builder_add_boolean_value (builder, TRUE);
          json_builder_set_member_name (builder, "version");
          version = g_variant_get_uint32 (variant_version);
          json_builder_add_int_value (builder, version);
        }
      json_builder_end_object (builder);
    }
  json_builder_end_object (builder);

  /* If we are in a Flatpak container we are not allowed to contact the
   * portals implementations. So we just skip this test */
  if (!g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      json_builder_set_member_name (builder, "backends");
      json_builder_begin_object (builder);
      for (i = 0; portal_impl_name[i] != NULL; i++)
        {
          g_autoptr(GDBusProxy) portal_impl_proxy = NULL;
          g_autofree gchar *impl_name_owner = NULL;
          portal_impl_proxy = g_dbus_proxy_new_sync (connection,
                                                     (G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                      G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS),
                                                     NULL,
                                                     portal_impl_name[i],
                                                     "/org/freedesktop/portal/desktop",
                                                     "org.freedesktop.DBus.Peer",
                                                     NULL,
                                                     error);

          json_builder_set_member_name (builder, portal_impl_name[i]);
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "available");

          if (portal_impl_proxy == NULL)
            {
              g_debug ("Failed to contact '%s': %s\n",
                       portal_impl_name[i], local_error->message);
              g_clear_error (error);
              json_builder_add_boolean_value (builder, FALSE);
              json_builder_end_object (builder);
              continue;
            }

          impl_name_owner = g_dbus_proxy_get_name_owner (portal_impl_proxy);
          if (impl_name_owner == NULL)
            {
              g_debug ("Failed to connect to '%s'\n", portal_impl_name[i]);
              json_builder_add_boolean_value (builder, FALSE);
            }
          else
            {
              impl_available = TRUE;
              json_builder_add_boolean_value (builder, TRUE);
            }

          json_builder_end_object (builder);
        }
      json_builder_end_object (builder);

      /* We just need a single portal implementation to be available */
      if (!impl_available)
        {
          g_printerr ("There isn't a working portal implementation\n");
          ret = EXIT_FAILURE;
        }
    }

  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_pretty (generator, TRUE);
  json_generator_set_root (generator, root);
  json = json_generator_to_data (generator, NULL);
  if (fputs (json, original_stdout) < 0)
    g_warning ("Unable to write output: %s", g_strerror (errno));

  if (fputs ("\n", original_stdout) < 0)
    g_warning ("Unable to write final newline: %s", g_strerror (errno));

  return ret;
}
