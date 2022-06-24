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
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * SECTION:graphics-drivers-vaapi
 * @title: VA-API graphics driver enumeration
 * @short_description: Get information about the system's VA-API drivers
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtVaApiDriver is an opaque object representing the metadata describing
 * a VA-API driver.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

/**
 * SrtVaApiDriver:
 *
 * Opaque object representing a VA-API driver.
 */

struct _SrtVaApiDriver
{
  /*< private >*/
  GObject parent;
  gchar *library_path;
  SrtVaApiVersion version;
  gboolean is_extra;
};

struct _SrtVaApiDriverClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  VA_API_DRIVER_PROP_0,
  VA_API_DRIVER_PROP_LIBRARY_PATH,
  VA_API_DRIVER_PROP_VERSION,
  VA_API_DRIVER_PROP_IS_EXTRA,
  VA_API_DRIVER_PROP_RESOLVED_LIBRARY_PATH,
  N_VA_API_DRIVER_PROPERTIES
};

G_DEFINE_TYPE (SrtVaApiDriver, srt_va_api_driver, G_TYPE_OBJECT)

static void
srt_va_api_driver_init (SrtVaApiDriver *self)
{
}

static void
srt_va_api_driver_get_property (GObject *object,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  SrtVaApiDriver *self = SRT_VA_API_DRIVER (object);

  switch (prop_id)
    {
      case VA_API_DRIVER_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      case VA_API_DRIVER_PROP_VERSION:
        g_value_set_enum (value, self->version);
        break;

      case VA_API_DRIVER_PROP_IS_EXTRA:
        g_value_set_boolean (value, self->is_extra);
        break;

      case VA_API_DRIVER_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_va_api_driver_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_va_api_driver_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  SrtVaApiDriver *self = SRT_VA_API_DRIVER (object);

  switch (prop_id)
    {
      case VA_API_DRIVER_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      case VA_API_DRIVER_PROP_VERSION:
        /* Construct-only */
        g_return_if_fail (self->version == SRT_VA_API_VERSION_UNKNOWN);
        self->version = g_value_get_enum (value);
        break;

      case VA_API_DRIVER_PROP_IS_EXTRA:
        self->is_extra = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_va_api_driver_finalize (GObject *object)
{
  SrtVaApiDriver *self = SRT_VA_API_DRIVER (object);

  g_clear_pointer (&self->library_path, g_free);

  G_OBJECT_CLASS (srt_va_api_driver_parent_class)->finalize (object);
}

static GParamSpec *va_api_driver_properties[N_VA_API_DRIVER_PROPERTIES] = { NULL };

static void
srt_va_api_driver_class_init (SrtVaApiDriverClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_va_api_driver_get_property;
  object_class->set_property = srt_va_api_driver_set_property;
  object_class->finalize = srt_va_api_driver_finalize;

  va_api_driver_properties[VA_API_DRIVER_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Path to the DRI driver library in provider's namespace. "
                         "It may be absolute (e.g. /usr/lib/dri/iHD_drv_video.so) "
                         "or relative (e.g. custom/dri/iHD_drv_video.so). "
                         "If absolute, it is set as though the "
                         "sysroot, if any, was the root",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  va_api_driver_properties[VA_API_DRIVER_PROP_IS_EXTRA] =
    g_param_spec_boolean ("is-extra", "Is extra?",
                          "TRUE if the driver is located in an unusual path",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  va_api_driver_properties[VA_API_DRIVER_PROP_VERSION] =
    g_param_spec_enum ("version", "Libva version",
                       "The libva version that the DRI driver is compatible with",
                       SRT_TYPE_VA_API_VERSION, SRT_VA_API_VERSION_UNKNOWN,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  va_api_driver_properties[VA_API_DRIVER_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Absolute path to the DRI driver library in provider's "
                         "namespace. This is similar to 'library-path', but "
                         "is guaranteed to be an absolute path "
                         "(e.g. /usr/lib/dri/iHD_drv_video.so) "
                         "as though the sysroot, if any, was the root",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VA_API_DRIVER_PROPERTIES,
                                     va_api_driver_properties);
}

/*
 * srt_va_api_driver_new:
 * @library_path: (transfer none): the path to the library
 * @version: required libva version, or %SRT_VA_API_VERSION_UNKNOWN if unknown
 * @is_extra: if the DRI driver is in an unusual path
 *
 * Returns: (transfer full): a new VA-API driver
 */
SrtVaApiDriver *
srt_va_api_driver_new (const gchar *library_path,
                       SrtVaApiVersion version,
                       gboolean is_extra)
{
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VA_API_DRIVER,
                       "library-path", library_path,
                       "version", version,
                       "is-extra", is_extra,
                       NULL);
}

/*
 * _srt_va_api_driver_version:
 * @dfd: A directory file descriptor, `AT_FDCWD` or -1
 * @library_path: (type filename): Non-empty path to a library relative to @dfd
 *
 * Returns: The libva version, or %SRT_VA_API_VERSION_UNKNOWN if unsure.
 */
SrtVaApiVersion
_srt_va_api_driver_version (int dfd,
                            const gchar *library_path)
{
  g_autoptr(Elf) elf = NULL;
  glnx_autofd int fd = -1;
  Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  GElf_Shdr *shdr_mem;
  Elf_Data *data;
  gsize count;
  gsize i;
  SrtVaApiVersion version = SRT_VA_API_VERSION_UNKNOWN;
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (library_path != NULL, SRT_VA_API_VERSION_UNKNOWN);
  g_return_val_if_fail (library_path[0] != '\0', SRT_VA_API_VERSION_UNKNOWN);

  if (!_srt_open_elf (dfd, library_path, &fd, &elf, &local_error))
    {
      g_debug ("%s", local_error->message);
      return SRT_VA_API_VERSION_UNKNOWN;
    }

  while ((scn = elf_nextscn (elf, scn)) != NULL)
  {
    shdr_mem = gelf_getshdr (scn, &shdr);
    if (shdr_mem == NULL)
      {
        g_debug ("Failed to parse the library \"%s\" header: %s",
                 library_path, elf_errmsg (-1));
        return SRT_VA_API_VERSION_UNKNOWN;
      }
    if (shdr.sh_type == SHT_DYNSYM || shdr.sh_type == SHT_SYMTAB)
      break;
  }

  if (scn == NULL)
    {
      g_debug ("Failed to find the symbols table for \"%s\"", library_path);
      return SRT_VA_API_VERSION_UNKNOWN;
    }

  data = elf_getdata (scn, NULL);
  count = shdr.sh_size / shdr.sh_entsize;
  /* We are looking for a symbol named __vaDriverInit_x_*, where x is the
   * VA-API ABI major version. */
  for (i = 0; i < count; i++)
    {
      GElf_Sym sym;
      const gchar *symbol;
      SrtVaApiVersion parsed_version = SRT_VA_API_VERSION_UNKNOWN;

      gelf_getsym (data, i, &sym);
      symbol = elf_strptr (elf, shdr.sh_link, sym.st_name);

      if (g_str_has_prefix (symbol, "__vaDriverInit_0_"))
        parsed_version = SRT_VA_API_VERSION_LIBVA1;
      else if (g_str_has_prefix (symbol, "__vaDriverInit_1_"))
        parsed_version = SRT_VA_API_VERSION_LIBVA2;
      else
        continue;

      g_debug ("Evaluating the symbol name %s", symbol);

      if (version != SRT_VA_API_VERSION_UNKNOWN && parsed_version != version)
        {
          g_warning ("\"%s\" has symbols for multiple libva major versions",
                     library_path);
          return SRT_VA_API_VERSION_UNKNOWN;
        }

      version = parsed_version;
    }

  return version;
}

/**
 * srt_va_api_driver_get_library_path:
 * @self: The VA-API driver
 *
 * Return the library path for this VA-API driver.
 *
 * Returns: (type filename) (transfer none): #SrtVaApiDriver:library-path
 */
const gchar *
srt_va_api_driver_get_library_path (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), NULL);
  return self->library_path;
}

/**
 * srt_va_api_driver_is_extra:
 * @self: The VA-API driver
 *
 * Return a gboolean that indicates if the VA-API is in an unusual position.
 *
 * Returns: %TRUE if the VA-API driver is in an unusual position.
 */
gboolean
srt_va_api_driver_is_extra (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), FALSE);
  return self->is_extra;
}

/**
 * srt_va_api_driver_resolve_library_path:
 * @self: The VA-API driver
 *
 * Return the absolute library path for this VA-API driver.
 * If srt_va_api_driver_get_library_path() is already an absolute path, a copy
 * of the same value will be returned.
 *
 * Returns: (type filename) (transfer full): A copy of
 *  #SrtVaApiDriver:resolved-library-path. Free with g_free().
 */
gchar *
srt_va_api_driver_resolve_library_path (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), NULL);
  g_return_val_if_fail (self->library_path != NULL, NULL);

  return g_canonicalize_filename (self->library_path, NULL);
}

/**
 * srt_va_api_driver_get_version:
 * @self: The VA-API driver
 *
 * Returns the libva version that @self is compatible with.
 *
 * Returns: The libva version, or %SRT_VA_API_VERSION_UNKNOWN if unsure.
 */
SrtVaApiVersion
srt_va_api_driver_get_version (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), FALSE);
  return self->version;
}
