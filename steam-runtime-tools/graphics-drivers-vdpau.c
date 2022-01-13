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

/**
 * SECTION:graphics-drivers-vdpau
 * @title: VDPAU graphics driver enumeration
 * @short_description: Get information about the system's VDPAU drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtVdpauDriver is an opaque object representing the metadata describing
 * a VDPAU driver.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtVdpauDriver:
 *
 * Opaque object representing a VDPAU driver.
 */

struct _SrtVdpauDriver
{
  /*< private >*/
  GObject parent;
  gchar *library_path;
  gchar *library_link;
  gboolean is_extra;
};

struct _SrtVdpauDriverClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  VDPAU_DRIVER_PROP_0,
  VDPAU_DRIVER_PROP_LIBRARY_PATH,
  VDPAU_DRIVER_PROP_LIBRARY_LINK,
  VDPAU_DRIVER_PROP_IS_EXTRA,
  VDPAU_DRIVER_PROP_RESOLVED_LIBRARY_PATH,
  N_VDPAU_DRIVER_PROPERTIES
};

G_DEFINE_TYPE (SrtVdpauDriver, srt_vdpau_driver, G_TYPE_OBJECT)

static void
srt_vdpau_driver_init (SrtVdpauDriver *self)
{
}

static void
srt_vdpau_driver_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SrtVdpauDriver *self = SRT_VDPAU_DRIVER (object);

  switch (prop_id)
    {
      case VDPAU_DRIVER_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      case VDPAU_DRIVER_PROP_LIBRARY_LINK:
        g_value_set_string (value, self->library_link);
        break;

      case VDPAU_DRIVER_PROP_IS_EXTRA:
        g_value_set_boolean (value, self->is_extra);
        break;

      case VDPAU_DRIVER_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_vdpau_driver_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vdpau_driver_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SrtVdpauDriver *self = SRT_VDPAU_DRIVER (object);

  switch (prop_id)
    {
      case VDPAU_DRIVER_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      case VDPAU_DRIVER_PROP_LIBRARY_LINK:
        g_return_if_fail (self->library_link == NULL);
        self->library_link = g_value_dup_string (value);
        break;

      case VDPAU_DRIVER_PROP_IS_EXTRA:
        self->is_extra = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vdpau_driver_finalize (GObject *object)
{
  SrtVdpauDriver *self = SRT_VDPAU_DRIVER (object);

  g_clear_pointer (&self->library_path, g_free);
  g_clear_pointer (&self->library_link, g_free);

  G_OBJECT_CLASS (srt_vdpau_driver_parent_class)->finalize (object);
}

static GParamSpec *vdpau_driver_properties[N_VDPAU_DRIVER_PROPERTIES] = { NULL };

static void
srt_vdpau_driver_class_init (SrtVdpauDriverClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_vdpau_driver_get_property;
  object_class->set_property = srt_vdpau_driver_set_property;
  object_class->finalize = srt_vdpau_driver_finalize;

  vdpau_driver_properties[VDPAU_DRIVER_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Path to the VDPAU driver library. It may be absolute "
                         "(e.g. /usr/lib/vdpau/libvdpau_radeonsi.so) or relative "
                         "(e.g. custom/vdpau/libvdpau_radeonsi.so). "
                         "If absolute, it is set as though the "
                         "sysroot, if any, was the root",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vdpau_driver_properties[VDPAU_DRIVER_PROP_LIBRARY_LINK] =
    g_param_spec_string ("library-link", "Library symlink contents",
                         "Contents of the symbolik link of the VDPAU driver library",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vdpau_driver_properties[VDPAU_DRIVER_PROP_IS_EXTRA] =
    g_param_spec_boolean ("is-extra", "Is extra?",
                          "TRUE if the driver is located in an unusual path",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  vdpau_driver_properties[VDPAU_DRIVER_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Absolute path to the VDPAU driver library. This is similar "
                         "to 'library-path', but is guaranteed to be an "
                         "absolute path (e.g. /usr/lib/vdpau/libvdpau_radeonsi.so) "
                         "as though the sysroot, if any, was the root",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VDPAU_DRIVER_PROPERTIES,
                                     vdpau_driver_properties);
}

/*
 * srt_vdpau_driver_new:
 * @library_path: (transfer none): the path to the library
 * @library_link: (transfer none) (nullable): the content of the library symlink
 * @is_extra: if the VDPAU driver is in an unusual path
 *
 * Returns: (transfer full): a new VDPAU driver
 */
SrtVdpauDriver *
srt_vdpau_driver_new (const gchar *library_path,
                      const gchar *library_link,
                      gboolean is_extra)
{
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VDPAU_DRIVER,
                       "library-path", library_path,
                       "library-link", library_link,
                       "is-extra", is_extra,
                       NULL);
}

/**
 * srt_vdpau_driver_get_library_path:
 * @self: The VDPAU driver
 *
 * Return the library path for this VDPAU driver.
 *
 * Returns: (type filename) (transfer none): #SrtVdpauDriver:library-path
 */
const gchar *
srt_vdpau_driver_get_library_path (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), NULL);
  return self->library_path;
}

/**
 * srt_vdpau_driver_get_library_link:
 * @self: The VDPAU driver
 *
 * Return the content of the symbolic link for this VDPAU driver or %NULL
 * if the library path is not a symlink.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVdpauDriver:library-link
 */
const gchar *
srt_vdpau_driver_get_library_link (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), NULL);
  return self->library_link;
}

/**
 * srt_vdpau_driver_is_extra:
 * @self: The VDPAU driver
 *
 * Return a gboolean that indicates if the VDPAU is in an unusual position.
 *
 * Returns: %TRUE if the VDPAU driver is in an unusual position.
 */
gboolean
srt_vdpau_driver_is_extra (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), FALSE);
  return self->is_extra;
}

/**
 * srt_vdpau_driver_resolve_library_path:
 * @self: The VDPAU driver
 *
 * Return the absolute library path for this VDPAU driver.
 * If srt_vdpau_driver_get_library_path() is already an absolute path, a copy
 * of the same value will be returned.
 *
 * Returns: (type filename) (transfer full): A copy of
 *  #SrtVdpauDriver:resolved-library-path. Free with g_free().
 */
gchar *
srt_vdpau_driver_resolve_library_path (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), NULL);
  g_return_val_if_fail (self->library_path != NULL, NULL);

  return g_canonicalize_filename (self->library_path, NULL);
}
