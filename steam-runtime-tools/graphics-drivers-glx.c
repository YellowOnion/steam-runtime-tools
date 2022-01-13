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
 * SECTION:graphics-drivers-glx
 * @title: GLX graphics driver enumeration
 * @short_description: Get information about the system's GLX graphics drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtGlxIcd is an opaque object representing the metadata describing
 * a GLX driver.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtGlxIcd:
 *
 * Opaque object representing a GLVND GLX ICD.
 */

struct _SrtGlxIcd
{
  /*< private >*/
  GObject parent;
  gchar *library_soname;
  gchar *library_path;
};

struct _SrtGlxIcdClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  GLX_ICD_PROP_0,
  GLX_ICD_PROP_LIBRARY_SONAME,
  GLX_ICD_PROP_LIBRARY_PATH,
  N_GLX_ICD_PROPERTIES
};

G_DEFINE_TYPE (SrtGlxIcd, srt_glx_icd, G_TYPE_OBJECT)

static void
srt_glx_icd_init (SrtGlxIcd *self)
{
}

static void
srt_glx_icd_get_property (GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  SrtGlxIcd *self = SRT_GLX_ICD (object);

  switch (prop_id)
    {
      case GLX_ICD_PROP_LIBRARY_SONAME:
        g_value_set_string (value, self->library_soname);
        break;

      case GLX_ICD_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_glx_icd_set_property (GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  SrtGlxIcd *self = SRT_GLX_ICD (object);

  switch (prop_id)
    {
      case GLX_ICD_PROP_LIBRARY_SONAME:
        g_return_if_fail (self->library_soname == NULL);
        self->library_soname = g_value_dup_string (value);
        break;

      case GLX_ICD_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_glx_icd_finalize (GObject *object)
{
  SrtGlxIcd *self = SRT_GLX_ICD (object);

  g_clear_pointer (&self->library_soname, g_free);
  g_clear_pointer (&self->library_path, g_free);

  G_OBJECT_CLASS (srt_glx_icd_parent_class)->finalize (object);
}

static GParamSpec *glx_icd_properties[N_GLX_ICD_PROPERTIES] = { NULL };

static void
srt_glx_icd_class_init (SrtGlxIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_glx_icd_get_property;
  object_class->set_property = srt_glx_icd_set_property;
  object_class->finalize = srt_glx_icd_finalize;

  glx_icd_properties[GLX_ICD_PROP_LIBRARY_SONAME] =
    g_param_spec_string ("library-soname", "Library soname",
                         "SONAME of the GLX ICD library",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  glx_icd_properties[GLX_ICD_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library absolute path",
                         "Absolute path to the GLX ICD library as though "
                         "the sysroot, if any, was the root",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_GLX_ICD_PROPERTIES,
                                     glx_icd_properties);
}

/*
 * srt_glx_icd_new:
 * @library_soname: (transfer none): the soname of the library
 * @library_path: (transfer none): the absolute path of the library
 *
 * Returns: (transfer full): a new GLVND GLX ICD
 */
SrtGlxIcd *
srt_glx_icd_new (const gchar *library_soname,
                 const gchar *library_path)
{
  g_return_val_if_fail (library_soname != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);
  g_return_val_if_fail (g_path_is_absolute (library_path), NULL);

  return g_object_new (SRT_TYPE_GLX_ICD,
                       "library-soname", library_soname,
                       "library-path", library_path,
                       NULL);
}

/**
 * srt_glx_icd_get_library_soname:
 * @self: The GLX ICD
 *
 * Return the library SONAME for this GLX ICD, for example `libGLX_mesa.so.0`.
 *
 * Returns: (type filename) (transfer none): #SrtGlxIcd:library-soname
 */
const gchar *
srt_glx_icd_get_library_soname (SrtGlxIcd *self)
{
  g_return_val_if_fail (SRT_IS_GLX_ICD (self), NULL);
  return self->library_soname;
}

/**
 * srt_glx_icd_get_library_path:
 * @self: The GLX ICD
 *
 * Return the absolute path to the library that implements this GLX soname.
 *
 * Returns: (type filename) (transfer none): #SrtGlxIcd:library-path
 */
const gchar *
srt_glx_icd_get_library_path (SrtGlxIcd *self)
{
  g_return_val_if_fail (SRT_IS_GLX_ICD (self), NULL);
  return self->library_path;
}
