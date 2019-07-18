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

#include "steam-runtime-tools/library.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/enums.h"

/**
 * SECTION:library
 * @title: Shared libraries
 * @short_description: Information about shared libraries
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtLibrary is an opaque object representing a shared library.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

struct _SrtLibrary
{
  /*< private >*/
  GObject parent;
  gchar *soname;
  GStrv missing_symbols;
  GQuark multiarch_tuple;
  SrtLibraryIssues issues;
};

struct _SrtLibraryClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_ISSUES,
  PROP_MISSING_SYMBOLS,
  PROP_MULTIARCH_TUPLE,
  PROP_SONAME,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtLibrary, srt_library, G_TYPE_OBJECT)

static void
srt_library_init (SrtLibrary *self)
{
}

static void
srt_library_get_property (GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  SrtLibrary *self = SRT_LIBRARY (object);

  switch (prop_id)
    {
      case PROP_ISSUES:
        g_value_set_flags (value, self->issues);
        break;

      case PROP_MISSING_SYMBOLS:
        g_value_set_boxed (value, self->missing_symbols);
        break;

      case PROP_MULTIARCH_TUPLE:
        g_value_set_static_string (value, g_quark_to_string (self->multiarch_tuple));
        break;

      case PROP_SONAME:
        g_value_set_string (value, self->soname);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_library_set_property (GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  SrtLibrary *self = SRT_LIBRARY (object);

  switch (prop_id)
    {
      case PROP_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
        break;

      case PROP_MISSING_SYMBOLS:
        /* Construct-only */
        g_return_if_fail (self->missing_symbols == NULL);
        self->missing_symbols = g_value_dup_boxed (value);

        /* Guarantee non-NULL */
        if (self->missing_symbols == NULL)
          self->missing_symbols = g_new0 (gchar *, 1);

        break;

      case PROP_MULTIARCH_TUPLE:
        /* Construct-only */
        g_return_if_fail (self->multiarch_tuple == 0);
        /* Intern the string since we only expect to deal with two values */
        self->multiarch_tuple = g_quark_from_string (g_value_get_string (value));
        break;

      case PROP_SONAME:
        /* Construct-only */
        g_return_if_fail (self->soname == NULL);
        self->soname = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_library_finalize (GObject *object)
{
  SrtLibrary *self = SRT_LIBRARY (object);

  g_free (self->soname);
  g_strfreev (self->missing_symbols);

  G_OBJECT_CLASS (srt_library_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_library_class_init (SrtLibraryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_library_get_property;
  object_class->set_property = srt_library_set_property;
  object_class->finalize = srt_library_finalize;

  properties[PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with this library",
                        SRT_TYPE_LIBRARY_ISSUES, SRT_LIBRARY_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  properties[PROP_MISSING_SYMBOLS] =
    g_param_spec_boxed ("missing-symbols", "Missing symbols",
                        "Symbols that were expected to be in this "
                        "library, but were found to be missing",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  properties[PROP_MULTIARCH_TUPLE] =
    g_param_spec_string ("multiarch-tuple", "Multiarch tuple",
                         "Debian-style multiarch tuple representing the "
                         "ABI of this library, usually " SRT_ABI_I386 " "
                         "or " SRT_ABI_X86_64,
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  properties[PROP_SONAME] =
    g_param_spec_string ("soname", "SONAME",
                         "The name of this library, for example libz.so.1",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * srt_library_get_soname:
 * @self: a library
 *
 * Return the SONAME (machine-readable runtime name) of @self.
 *
 * Returns: A string like `libz.so.1`, which is valid as long as @self
 *  is not destroyed.
 */
const char *
srt_library_get_soname (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return self->soname;
}

/**
 * srt_library_get_multiarch_tuple:
 * @self: a library
 *
 * Return the multiarch tuple representing the ABI of @self.
 *
 * Returns: A Debian-style multiarch tuple, usually %SRT_ABI_I386
 *  or %SRT_ABI_X86_64
 */
const char *
srt_library_get_multiarch_tuple (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return g_quark_to_string (self->multiarch_tuple);
}

/**
 * srt_library_get_issues:
 * @self: a library
 *
 * Return the problems found when loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found
 */
SrtLibraryIssues
srt_library_get_issues (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), SRT_LIBRARY_ISSUES_CANNOT_LOAD);
  return self->issues;
}

/**
 * srt_library_get_missing_symbols:
 * @self: a library
 *
 * Return the symbols that were expected to be provided by @self but
 * were not found.
 *
 * Returns: (array zero-terminated=1) (element-type utf8): The symbols
 *  that were missing from @self, as a %NULL-terminated array. The
 *  pointer remains valid until @self is destroyed.
 */
const char * const *
srt_library_get_missing_symbols (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return (const char * const *) self->missing_symbols;
}
