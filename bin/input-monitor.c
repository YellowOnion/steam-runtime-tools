/*
 * Input device monitor, adapted from SDL code.
 *
 * Copyright © 1997-2020 Sam Lantinga <slouken@libsdl.org>
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: Zlib
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <stdio.h>

#include <glib-unix.h>

#include <json-glib/json-glib.h>

#define RECORD_SEPARATOR "\x1e"

static FILE *original_stdout = NULL;
static gboolean opt_one_line = FALSE;
static gboolean opt_seq = FALSE;

static void
print_json (JsonBuilder *builder)
{
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autofree gchar *text = NULL;
  JsonNode *root;

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);

  if (opt_seq)
    fputs (RECORD_SEPARATOR, original_stdout);

  if (!opt_one_line)
    json_generator_set_pretty (generator, TRUE);

  text = json_generator_to_data (generator, NULL);

  if (fputs (text, original_stdout) < 0)
    g_warning ("Unable to write output: %s", g_strerror (errno));

  if (fputs ("\n", original_stdout) < 0)
    g_warning ("Unable to write final newline: %s", g_strerror (errno));
}

static void
added (SrtInputDeviceMonitor *monitor,
       SrtInputDevice *dev,
       void *user_data)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "added");
      json_builder_begin_object (builder);
        {
          json_builder_set_member_name (builder, "dev_node");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_dev_node (dev));
          json_builder_set_member_name (builder, "subsystem");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_subsystem (dev));
          json_builder_set_member_name (builder, "sys_path");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_sys_path (dev));
          /* TODO: Print more details here */
        }
      json_builder_end_object (builder);
    }
  json_builder_end_object (builder);

  print_json (builder);
}

static void
removed (SrtInputDeviceMonitor *monitor,
         SrtInputDevice *dev,
         void *user_data)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "removed");
      json_builder_begin_object (builder);
        {
          /* Only print enough details to identify the object */
          json_builder_set_member_name (builder, "dev_node");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_dev_node (dev));
          json_builder_set_member_name (builder, "sys_path");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_sys_path (dev));
        }
      json_builder_end_object (builder);
    }
  json_builder_end_object (builder);

  print_json (builder);
}

static void
all_for_now (SrtInputDeviceMonitor *source,
             void *user_data)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  SrtInputDeviceMonitor **monitor_p = user_data;

  if (srt_input_device_monitor_get_flags (source) & SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE)
    {
      monitor = g_steal_pointer (monitor_p);
      g_assert (monitor == NULL || monitor == source);
      g_clear_object (&monitor);
    }

  if (opt_seq)
    fputs (RECORD_SEPARATOR, original_stdout);

  fputs ("{\"all-for-now\": true}\n", original_stdout);
}

static gboolean
interrupt_cb (void *user_data)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  SrtInputDeviceMonitor **monitor_p = user_data;

  monitor = g_steal_pointer (monitor_p);
  g_clear_object (&monitor);

  return G_SOURCE_CONTINUE;
}

static SrtInputDeviceMonitorFlags opt_mode = SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE;
static SrtInputDeviceMonitorFlags opt_flags = SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE;
static gboolean opt_evdev = FALSE;
static gboolean opt_hidraw = FALSE;
static gboolean opt_print_version = FALSE;

static gboolean
opt_once_cb (const gchar *option_name,
             const gchar *value,
             gpointer data,
             GError **error)
{
  opt_flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE;
  return TRUE;
}

static gboolean
opt_direct_cb (const gchar *option_name,
               const gchar *value,
               gpointer data,
               GError **error)
{
  opt_mode = SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT;
  return TRUE;
}

static gboolean
opt_udev_cb (const gchar *option_name,
             const gchar *value,
             gpointer data,
             GError **error)
{
  opt_mode = SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV;
  return TRUE;
}

static const GOptionEntry option_entries[] =
{
  { "direct", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_direct_cb,
    "Find devices using /dev and /sys", NULL },
  { "evdev", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_evdev,
    "List evdev event devices", NULL },
  { "hidraw", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_hidraw,
    "List raw HID devices", NULL },
  { "once", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_once_cb,
    "Print devices that are initially discovered, then exit", NULL },
  { "one-line", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_one_line,
    "Print one device per line [default: pretty-print as concatenated JSON]",
    NULL },
  { "seq", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_seq,
    "Output application/json-seq [default: pretty-print as concatenated JSON]",
    NULL },
  { "udev", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_udev_cb,
    "Find devices using udev", NULL },
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { NULL }
};

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  guint int_handler;

  _srt_setenv_disable_gio_modules ();

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, error))
    return FALSE;

  if (opt_print_version)
    {
      /* Output version number as YAML for machine-readability,
       * inspired by `ostree --version` and `docker version` */
      g_print (
          "%s:\n"
          " Package: steam-runtime-tools\n"
          " Version: %s\n",
          g_get_prgname (), VERSION);
      return TRUE;
    }

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    return FALSE;

  int_handler = g_unix_signal_add (SIGINT, interrupt_cb, &monitor);
  monitor = srt_input_device_monitor_new (opt_mode | opt_flags);

  if (opt_evdev)
    srt_input_device_monitor_request_evdev (monitor);

  if (opt_hidraw)
    srt_input_device_monitor_request_raw_hid (monitor);

  if (opt_evdev + opt_hidraw == 0)
    {
      /* Subscribe to everything by default */
      srt_input_device_monitor_request_evdev (monitor);
      srt_input_device_monitor_request_raw_hid (monitor);
    }

  g_signal_connect (monitor, "added", G_CALLBACK (added), NULL);
  g_signal_connect (monitor, "removed", G_CALLBACK (removed), NULL);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now), &monitor);

  if (!srt_input_device_monitor_start (monitor, error))
    return FALSE;

  while (monitor != NULL)
    g_main_context_iteration (NULL, TRUE);

  if (fclose (original_stdout) != 0)
    g_warning ("Unable to close stdout: %s", g_strerror (errno));

  g_source_remove (int_handler);
  return TRUE;
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;

  if (run (argc, argv, &error))
    return 0;

  if (error == NULL)
    g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Assertion failure");

  g_printerr ("%s: %s\n", g_get_prgname (), error->message);

  if (error->domain == G_OPTION_ERROR)
    return 2;

  return 1;
}
