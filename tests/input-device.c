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

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <linux/input.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/utils-internal.h"
#include "mock-input-device.h"
#include "test-utils.h"

typedef struct
{
  enum { MOCK, DIRECT, UDEV } type;
} Config;

static const Config defconfig =
{
  .type = MOCK,
};

static const Config direct_config =
{
  .type = DIRECT,
};

static const Config udev_config =
{
  .type = UDEV,
};

typedef struct
{
  const Config *config;
  GMainContext *monitor_context;
  GPtrArray *log;
  gboolean skipped;
} Fixture;

static void
setup (Fixture *f,
       gconstpointer context)
{
  const Config *config = context;
  GStatBuf sb;

  if (config == NULL)
    f->config = &defconfig;
  else
    f->config = config;

  if (f->config->type == DIRECT && g_stat ("/dev/input", &sb) != 0)
    {
      g_test_skip ("/dev/input not available");
      f->skipped = TRUE;
    }

  f->log = g_ptr_array_new_with_free_func (g_free);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->log, g_ptr_array_unref);
  g_clear_pointer (&f->monitor_context, g_main_context_unref);
}

static void
test_input_device_usb (Fixture *f,
                       gconstpointer context)
{
  g_autoptr(MockInputDevice) mock_device = mock_input_device_new ();
  SrtInputDevice *device = SRT_INPUT_DEVICE (mock_device);
  g_auto(GStrv) udev_properties = NULL;
  g_autofree gchar *uevent = NULL;
  g_autofree gchar *hid_uevent = NULL;
  g_autofree gchar *input_uevent = NULL;
  g_autofree gchar *usb_uevent = NULL;

  mock_device->dev_node = g_strdup ("/dev/input/event0");
  mock_device->sys_path = g_strdup ("/sys/devices/mock/usb/hid/input/input0/event0");
  mock_device->subsystem = g_strdup ("input");
  mock_device->udev_properties = g_new0 (gchar *, 2);
  mock_device->udev_properties[0] = g_strdup ("ID_INPUT_JOYSTICK=1");
  mock_device->udev_properties[1] = NULL;
  mock_device->uevent = g_strdup ("A=a\nB=b\n");

  mock_device->hid_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid");
  mock_device->hid_ancestor.uevent = g_strdup ("HID=yes\n");

  mock_device->input_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid/input");
  mock_device->input_ancestor.uevent = g_strdup ("INPUT=yes\n");

  mock_device->usb_device_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb");
  mock_device->usb_device_ancestor.uevent = g_strdup ("USB=usb_device\n");

  /* TODO: Fill in somewhat realistic details for a USB-attached
   * Steam Controller */

  g_assert_cmpstr (srt_input_device_get_dev_node (device), ==,
                   "/dev/input/event0");
  g_assert_cmpstr (srt_input_device_get_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid/input/input0/event0");
  g_assert_cmpstr (srt_input_device_get_subsystem (device), ==, "input");

  uevent = srt_input_device_dup_uevent (device);
  g_assert_cmpstr (uevent, ==, "A=a\nB=b\n");

  g_assert_cmpstr (srt_input_device_get_hid_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid");
  hid_uevent = srt_input_device_dup_hid_uevent (device);
  g_assert_cmpstr (hid_uevent, ==, "HID=yes\n");

  g_assert_cmpstr (srt_input_device_get_input_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid/input");
  input_uevent = srt_input_device_dup_input_uevent (device);
  g_assert_cmpstr (input_uevent, ==, "INPUT=yes\n");

  g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (device), ==,
                   "/sys/devices/mock/usb");
  usb_uevent = srt_input_device_dup_usb_device_uevent (device);
  g_assert_cmpstr (usb_uevent, ==, "USB=usb_device\n");

  udev_properties = srt_input_device_dup_udev_properties (device);
  g_assert_nonnull (udev_properties);
  g_assert_cmpstr (udev_properties[0], ==, "ID_INPUT_JOYSTICK=1");
  g_assert_cmpstr (udev_properties[1], ==, NULL);

  /* TODO: Check other details */
}

static gboolean
in_monitor_main_context (Fixture *f)
{
  if (f->monitor_context == NULL)
    return g_main_context_is_owner (g_main_context_default ());

  return g_main_context_is_owner (f->monitor_context);
}

static void
device_added_cb (SrtInputDeviceMonitor *monitor,
                 SrtInputDevice *device,
                 gpointer user_data)
{
  Fixture *f = user_data;
  g_autofree gchar *message = NULL;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));
  g_assert_true (SRT_IS_INPUT_DEVICE (device));

  message = g_strdup_printf ("added device: %s",
                             srt_input_device_get_dev_node (device));
  g_debug ("%s: %s", G_OBJECT_TYPE_NAME (monitor), message);

  /* For the mock device monitor, we know exactly what to expect, so
   * we can compare the expected log with what actually happened. For
   * real device monitors, we don't know what's physically present,
   * so we have to just emit debug messages. */
  if (f->config->type == MOCK)
    {
      g_autofree gchar *uevent = NULL;
      g_autofree gchar *hid_uevent = NULL;
      g_autofree gchar *input_uevent = NULL;
      g_autofree gchar *usb_uevent = NULL;
      g_auto(GStrv) udev_properties = NULL;

      uevent = srt_input_device_dup_uevent (device);
      g_assert_cmpstr (uevent, ==, "ONE=1\nTWO=2\n");

      udev_properties = srt_input_device_dup_udev_properties (device);
      g_assert_nonnull (udev_properties);
      g_assert_cmpstr (udev_properties[0], ==, "ID_INPUT_JOYSTICK=1");
      g_assert_cmpstr (udev_properties[1], ==, NULL);

      g_assert_cmpstr (srt_input_device_get_hid_sys_path (device), ==,
                       "/sys/devices/mock/usb/hid");
      hid_uevent = srt_input_device_dup_hid_uevent (device);
      g_assert_cmpstr (hid_uevent, ==, "HID=yes\n");

      g_assert_cmpstr (srt_input_device_get_input_sys_path (device), ==,
                       "/sys/devices/mock/usb/hid/input");
      input_uevent = srt_input_device_dup_input_uevent (device);
      g_assert_cmpstr (input_uevent, ==, "INPUT=yes\n");

      g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (device), ==,
                       "/sys/devices/mock/usb");
      usb_uevent = srt_input_device_dup_usb_device_uevent (device);
      g_assert_cmpstr (usb_uevent, ==, "USB=usb_device\n");

      g_ptr_array_add (f->log, g_steal_pointer (&message));
    }

  g_assert_true (in_monitor_main_context (f));
}

static void
device_removed_cb (SrtInputDeviceMonitor *monitor,
                   SrtInputDevice *device,
                   gpointer user_data)
{
  Fixture *f = user_data;
  g_autofree gchar *message = NULL;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));
  g_assert_true (SRT_IS_INPUT_DEVICE (device));

  message = g_strdup_printf ("removed device: %s",
                             srt_input_device_get_dev_node (device));
  g_debug ("%s: %s", G_OBJECT_TYPE_NAME (monitor), message);

  if (f->config->type == MOCK)
    g_ptr_array_add (f->log, g_steal_pointer (&message));

  g_assert_true (in_monitor_main_context (f));
}

static void
all_for_now_cb (SrtInputDeviceMonitor *monitor,
                gpointer user_data)
{
  Fixture *f = user_data;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));

  g_ptr_array_add (f->log, g_strdup ("all for now"));
  g_debug ("%s: %s",
           G_OBJECT_TYPE_NAME (monitor),
           (const char *) g_ptr_array_index (f->log, f->log->len - 1));

  g_assert_true (in_monitor_main_context (f));
}

static gboolean
done_cb (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;
  return G_SOURCE_REMOVE;
}

/* This is the equivalent of g_idle_add() for a non-default main context */
static guint
idle_add_in_context (GSourceFunc function,
                     gpointer data,
                     GMainContext *context)
{
  g_autoptr(GSource) idler = g_idle_source_new ();

  g_source_set_callback (idler, function, data, NULL);
  return g_source_attach (idler, context);
}

static SrtInputDeviceMonitor *
input_device_monitor_new (Fixture *f,
                          SrtInputDeviceMonitorFlags flags)
{
  switch (f->config->type)
    {
      case DIRECT:
        flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT;
        return srt_input_device_monitor_new (flags);

      case UDEV:
        flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV;
        return srt_input_device_monitor_new (flags);

      case MOCK:
      default:
        return SRT_INPUT_DEVICE_MONITOR (mock_input_device_monitor_new (flags));
    }
}

/*
 * Test the basic behaviour of an input device monitor:
 * - start
 * - do initial enumeration
 * - watch for new devices
 * - emit signals in the correct main context
 * - stop
 */
static void
test_input_device_monitor (Fixture *f,
                           gconstpointer context)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  gboolean did_default_idle = FALSE;
  gboolean did_context_idle = FALSE;
  guint i;

  if (f->skipped)
    return;

  f->monitor_context = g_main_context_new ();

  /* To check that the signals get emitted in the correct main-context,
   * temporarily set a new thread-default main-context while we create
   * the monitor. */
  g_main_context_push_thread_default (f->monitor_context);
    {
      monitor = input_device_monitor_new (f, SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE);
    }
  g_main_context_pop_thread_default (f->monitor_context);

  g_assert_nonnull (monitor);

  srt_input_device_monitor_request_evdev (monitor);
  srt_input_device_monitor_request_raw_hid (monitor);

  g_signal_connect (monitor, "added", G_CALLBACK (device_added_cb), f);
  g_signal_connect (monitor, "removed", G_CALLBACK (device_removed_cb), f);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now_cb), f);

  /* Note that the signals are emitted in the main-context that was
   * thread-default at the time we created the object, not the
   * main-context that called start(). */
  ok = srt_input_device_monitor_start (monitor, &error);
  g_debug ("start() returned");
  g_assert_no_error (error);
  g_assert_true (ok);
  g_ptr_array_add (f->log, g_strdup ("start() returned"));

  g_idle_add (done_cb, &did_default_idle);
  idle_add_in_context (done_cb, &did_context_idle, f->monitor_context);

  i = 0;

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "start() returned");
  /* There's nothing else in the log yet */
  g_assert_cmpuint (f->log->len, ==, i);

  /* Iterating the default main context does not deliver signals */
  while (!did_default_idle)
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpuint (f->log->len, ==, i);

  /* Iterating the main context that was thread-default at the time we
   * constructed the monitor *does* deliver signals */
  while (!did_context_idle)
    g_main_context_iteration (f->monitor_context, TRUE);

  /* For the mock device monitor, we can predict which devices will be added,
   * so we log them and assert about them. For real device monitors we
   * can't reliably do this. */
  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event0");
    }

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "all for now");

  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event-connected-briefly");
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "removed device: /dev/input/event-connected-briefly");
    }

  g_assert_cmpuint (f->log->len, ==, i);

  /* Explicitly stop it here. We test not explicitly stopping in the
   * other test-case */
  srt_input_device_monitor_stop (monitor);

  /* It's possible that not all the memory used is freed until we have
   * iterated the main-context one last time */
  did_context_idle = FALSE;
  idle_add_in_context (done_cb, &did_context_idle, f->monitor_context);

  while (!did_context_idle)
    g_main_context_iteration (f->monitor_context, TRUE);
}

/*
 * Test things we couldn't test in the previous test-case:
 * - the ONCE flag, which disables monitoring
 * - using our thread-default main-context throughout
 */
static void
test_input_device_monitor_once (Fixture *f,
                                gconstpointer context)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  gboolean done = FALSE;
  guint i;

  if (f->skipped)
    return;

  monitor = input_device_monitor_new (f, SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE);
  g_assert_nonnull (monitor);

  srt_input_device_monitor_request_evdev (monitor);
  srt_input_device_monitor_request_raw_hid (monitor);

  g_signal_connect (monitor, "added", G_CALLBACK (device_added_cb), f);
  g_signal_connect (monitor, "removed", G_CALLBACK (device_removed_cb), f);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now_cb), f);

  ok = srt_input_device_monitor_start (monitor, &error);
  g_debug ("start() returned");
  g_assert_no_error (error);
  g_assert_true (ok);
  g_ptr_array_add (f->log, g_strdup ("start() returned"));

  g_idle_add (done_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  i = 0;

  /* Because the same main context was the thread-default at the
   * time we created the object and at the time we called start(),
   * the first batch of signals arrive even before start() has returned. */
  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event0");
    }

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "all for now");
  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "start() returned");
  g_assert_cmpuint (f->log->len, ==, i);

  /* Don't explicitly stop it here. We test explicitly stopping in the
   * other test-case */
  g_clear_object (&monitor);

  /* It's possible that not all the memory used is freed until we have
   * iterate the main-context one last time */
  done = FALSE;
  g_idle_add (done_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/input-device/usb", Fixture, NULL,
              setup, test_input_device_usb, teardown);
  g_test_add ("/input-device/monitor/mock", Fixture, NULL,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/mock", Fixture, NULL,
              setup, test_input_device_monitor_once, teardown);
  g_test_add ("/input-device/monitor/direct", Fixture, &direct_config,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/direct", Fixture, &direct_config,
              setup, test_input_device_monitor_once, teardown);
  g_test_add ("/input-device/monitor/udev", Fixture, &udev_config,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/udev", Fixture, &udev_config,
              setup, test_input_device_monitor_once, teardown);

  return g_test_run ();
}
