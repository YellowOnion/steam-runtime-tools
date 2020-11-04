/*<private_header>*/
/*
 * Input device internals, with parts based on SDL code.
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

#pragma once

#include "steam-runtime-tools/input-device.h"

#include <linux/input.h>

#define _SRT_INPUT_DEVICE_ALWAYS_OPEN_FLAGS (O_CLOEXEC | O_NOCTTY)

/* Macros for the bitfield encoding used by the EVIOCGBIT ioctl. */
#define BITS_PER_LONG           (sizeof (unsigned long) * CHAR_BIT)
#define LONGS_FOR_BITS(x)       ((((x)-1)/BITS_PER_LONG)+1)
#define CHOOSE_BIT(x)           ((x)%BITS_PER_LONG)
#define CHOOSE_LONG(x)          ((x)/BITS_PER_LONG)

/* Note that this always returns 0 or 1, so it can be considered to return
 * a gboolean */
#define test_bit(bit, ulongs)   ((ulongs[CHOOSE_LONG(bit)] >> CHOOSE_BIT(bit)) & 1)

#define set_bit(bit, ulongs)    do { ulongs[CHOOSE_LONG(bit)] |= (1UL << CHOOSE_BIT(bit)); } while (0)

/*
 * @bit_number: A bit number, where 0 is the least significant bit of bits[0]
 * @bits: (in) (array length=n_longs): Bitfields encoded as longs, where
 *  the most significant bit of bits[0] is one place less significant than
 *  the least significant bit of bits[1]
 * @n_longs: The number of elements in @bits
 *
 * Decode the bitfield encoding used by the EVIOCGBIT ioctl.
 */
static inline gboolean
test_bit_checked (unsigned int bit_number,
                  const unsigned long *bits,
                  size_t n_longs)
{
  if (CHOOSE_LONG (bit_number) >= n_longs)
    return FALSE;

  return test_bit (bit_number, bits);
}

/* We assume a buffer large enough for all the keyboard/button codes is
 * also sufficient for all the less numerous event types. */
#define HIGHEST_EVENT_CODE (KEY_MAX)
G_STATIC_ASSERT (KEY_MAX >= EV_MAX);
G_STATIC_ASSERT (KEY_MAX >= ABS_MAX);
G_STATIC_ASSERT (KEY_MAX >= REL_MAX);
G_STATIC_ASSERT (KEY_MAX >= FF_MAX);

/* Some of the event codes we're interested in weren't in in older kernels,
 * like the one whose headers we use for SteamRT 1 'scout'. */

#ifndef ABS_RESERVED
# define ABS_RESERVED 0x2e
#endif

G_STATIC_ASSERT (ABS_RESERVED < KEY_MAX);

#ifndef REL_RESERVED
# define REL_RESERVED 0x0a
#endif

#ifndef REL_WHEEL_HI_RES
# define REL_WHEEL_HI_RES 0x0b
#endif

#ifndef REL_HWHEEL_HI_RES
# define REL_HWHEEL_HI_RES 0x0c
#endif

G_STATIC_ASSERT (REL_HWHEEL_HI_RES < KEY_MAX);

#ifndef BTN_DPAD_UP
  /* We assume that these were all defined together */
# define BTN_DPAD_UP 0x220
# define BTN_DPAD_DOWN 0x221
# define BTN_DPAD_LEFT 0x222
# define BTN_DPAD_RIGHT 0x223
#endif

#ifndef KEY_MACRO1
# define KEY_MACRO1 0x290
#endif

G_STATIC_ASSERT (KEY_MACRO1 < KEY_MAX);

#ifndef INPUT_PROP_TOPBUTTONPAD
# define INPUT_PROP_TOPBUTTONPAD 0x04
#endif
#ifndef INPUT_PROP_POINTING_STICK
# define INPUT_PROP_POINTING_STICK 0x05
#endif
#ifndef INPUT_PROP_ACCELEROMETER
# define INPUT_PROP_ACCELEROMETER 0x06
#endif

G_STATIC_ASSERT (INPUT_PROP_ACCELEROMETER < INPUT_PROP_MAX);

typedef struct
{
  unsigned long ev[LONGS_FOR_BITS (EV_MAX)];
  unsigned long keys[LONGS_FOR_BITS (KEY_MAX)];
  unsigned long abs[LONGS_FOR_BITS (ABS_MAX)];
  unsigned long rel[LONGS_FOR_BITS (REL_MAX)];
  unsigned long ff[LONGS_FOR_BITS (FF_MAX)];
  unsigned long props[LONGS_FOR_BITS (INPUT_PROP_MAX)];
} SrtEvdevCapabilities;

const unsigned long *_srt_evdev_capabilities_get_bits (const SrtEvdevCapabilities *caps,
                                                       unsigned int type,
                                                       size_t *n_longs);
gboolean _srt_evdev_capabilities_set_from_evdev (SrtEvdevCapabilities *caps,
                                                 int fd);
void _srt_evdev_capabilities_dump (const SrtEvdevCapabilities *caps);
SrtInputDeviceTypeFlags _srt_evdev_capabilities_guess_type (const SrtEvdevCapabilities *caps);

struct _SrtInputDeviceInterface
{
  GTypeInterface parent;

  SrtInputDeviceTypeFlags (*get_type_flags) (SrtInputDevice *device);
  SrtInputDeviceInterfaceFlags (*get_interface_flags) (SrtInputDevice *device);

  const char * (*get_dev_node) (SrtInputDevice *device);
  const char * (*get_sys_path) (SrtInputDevice *device);
  const char * (*get_subsystem) (SrtInputDevice *device);
  gchar ** (*dup_udev_properties) (SrtInputDevice *device);
  gchar * (*dup_uevent) (SrtInputDevice *device);
  gboolean (*get_identity) (SrtInputDevice *device,
                            unsigned int *bus_type,
                            unsigned int *vendor_id,
                            unsigned int *product_id,
                            unsigned int *version);
  const SrtEvdevCapabilities *(*peek_event_capabilities) (SrtInputDevice *device);

  const char * (*get_hid_sys_path) (SrtInputDevice *device);
  gchar * (*dup_hid_uevent) (SrtInputDevice *device);
  gboolean (*get_hid_identity) (SrtInputDevice *device,
                                unsigned int *bus_type,
                                unsigned int *vendor_id,
                                unsigned int *product_id,
                                const char **name,
                                const char **phys,
                                const char **uniq);

  const char * (*get_input_sys_path) (SrtInputDevice *device);
  gchar * (*dup_input_uevent) (SrtInputDevice *device);
  gboolean (*get_input_identity) (SrtInputDevice *device,
                                  unsigned int *bus_type,
                                  unsigned int *vendor_id,
                                  unsigned int *product_id,
                                  unsigned int *version,
                                  const char **name,
                                  const char **phys,
                                  const char **uniq);

  const char * (*get_usb_device_sys_path) (SrtInputDevice *device);
  gchar * (*dup_usb_device_uevent) (SrtInputDevice *device);
  gboolean (*get_usb_device_identity) (SrtInputDevice *device,
                                       unsigned int *vendor_id,
                                       unsigned int *product_id,
                                       unsigned int *device_version,
                                       const char **manufacturer,
                                       const char **product,
                                       const char **serial);

  int (*open_device) (SrtInputDevice *device,
                      int mode_and_flags,
                      GError **error);
};

gboolean _srt_input_device_check_open_flags (int mode_and_flags,
                                             GError **error);

struct _SrtInputDeviceMonitorInterface
{
  GTypeInterface parent;

  /* Signals */
  void (*added) (SrtInputDeviceMonitor *monitor,
                 SrtInputDevice *device);
  void (*removed) (SrtInputDeviceMonitor *monitor,
                   SrtInputDevice *device);
  void (*all_for_now) (SrtInputDeviceMonitor *monitor);

  /* Virtual methods */
  void (*request_raw_hid) (SrtInputDeviceMonitor *monitor);
  void (*request_evdev) (SrtInputDeviceMonitor *monitor);
  gboolean (*start) (SrtInputDeviceMonitor *monitor,
                     GError **error);
  void (*stop) (SrtInputDeviceMonitor *monitor);
};

void _srt_input_device_monitor_emit_added (SrtInputDeviceMonitor *monitor,
                                           SrtInputDevice *device);
void _srt_input_device_monitor_emit_removed (SrtInputDeviceMonitor *monitor,
                                             SrtInputDevice *device);
void _srt_input_device_monitor_emit_all_for_now (SrtInputDeviceMonitor *monitor);

gboolean _srt_get_identity_from_evdev (int fd,
                                       guint32 *bus_type,
                                       guint32 *vendor,
                                       guint32 *product,
                                       guint32 *version,
                                       gchar **name,
                                       gchar **phys,
                                       gchar **uniq);
gboolean _srt_get_identity_from_raw_hid (int fd,
                                         guint32 *bus_type,
                                         guint32 *vendor,
                                         guint32 *product,
                                         gchar **name,
                                         gchar **phys,
                                         gchar **uniq);
gchar *_srt_input_device_uevent_field (const char *text,
                                       const char *key);
gboolean _srt_input_device_uevent_field_equals (const char *text,
                                                const char *key,
                                                const char *want_value);
gboolean _srt_get_identity_from_hid_uevent (const char *text,
                                            guint32 *bus_type,
                                            guint32 *product_id,
                                            guint32 *vendor_id,
                                            gchar **name,
                                            gchar **phys,
                                            gchar **uniq);
