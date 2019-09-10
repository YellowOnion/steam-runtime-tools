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
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <json-glib/json-glib.h>

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
  gchar *absolute_path;
  gchar *messages;
  gchar *soname;
  GStrv dependencies;
  GStrv missing_symbols;
  GStrv misversioned_symbols;
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
  PROP_ABSOLUTE_PATH,
  PROP_DEPENDENCIES,
  PROP_ISSUES,
  PROP_MESSAGES,
  PROP_MISSING_SYMBOLS,
  PROP_MULTIARCH_TUPLE,
  PROP_SONAME,
  PROP_MISVERSIONED_SYMBOLS,
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
      case PROP_ABSOLUTE_PATH:
        g_value_set_string (value, self->absolute_path);
        break;

      case PROP_DEPENDENCIES:
        g_value_set_boxed (value, self->dependencies);
        break;

      case PROP_ISSUES:
        g_value_set_flags (value, self->issues);
        break;

      case PROP_MESSAGES:
        g_value_set_string (value, self->messages);
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

      case PROP_MISVERSIONED_SYMBOLS:
        g_value_set_boxed (value, self->misversioned_symbols);
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
  const char *tmp;

  switch (prop_id)
    {
      case PROP_ABSOLUTE_PATH:
        /* Construct-only */
        g_return_if_fail (self->absolute_path == NULL);
        self->absolute_path = g_value_dup_string (value);
        break;

      case PROP_DEPENDENCIES:
        /* Construct-only */
        g_return_if_fail (self->dependencies == NULL);
        self->dependencies = g_value_dup_boxed (value);

        /* Guarantee non-NULL */
        if (self->dependencies == NULL)
          self->dependencies = g_new0 (gchar *, 1);

        break;

      case PROP_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
        break;

      case PROP_MESSAGES:
        /* Construct-only */
        g_return_if_fail (self->messages == NULL);
        tmp = g_value_get_string (value);

        /* Normalize the empty string (expected to be common) to NULL */
        if (tmp != NULL && tmp[0] == '\0')
          tmp = NULL;

        self->messages = g_strdup (tmp);
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

      case PROP_MISVERSIONED_SYMBOLS:
        /* Construct-only */
        g_return_if_fail (self->misversioned_symbols == NULL);
        self->misversioned_symbols = g_value_dup_boxed (value);

        /* Guarantee non-NULL */
        if (self->misversioned_symbols == NULL)
          self->misversioned_symbols = g_new0 (gchar *, 1);

        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_library_finalize (GObject *object)
{
  SrtLibrary *self = SRT_LIBRARY (object);

  g_free (self->absolute_path);
  g_free (self->messages);
  g_free (self->soname);
  g_strfreev (self->dependencies);
  g_strfreev (self->missing_symbols);
  g_strfreev (self->misversioned_symbols);

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

  properties[PROP_ABSOLUTE_PATH] =
    g_param_spec_string ("absolute-path", "Absolute path",
                         "The absolute path of this library, for example "
                         "/usr/lib/libz.so.1",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  properties[PROP_DEPENDENCIES] =
    g_param_spec_boxed ("dependencies", "Dependencies",
                        "Dependencies of this library",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  properties[PROP_MESSAGES] =
    g_param_spec_string ("messages", "Messages",
                         "Diagnostic messages produced while checking this "
                         "library",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
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
  properties[PROP_MISVERSIONED_SYMBOLS] =
    g_param_spec_boxed ("misversioned-symbols", "Misversioned symbols",
                        "Symbols that were expected to be in this library, "
                        "but were available with a different version",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * srt_library_get_absolute_path:
 * @self: a library
 *
 * Return the absolute path of @self.
 *
 * Returns: A string like `/usr/lib/libz.so.1`, which is valid as long as
 *  @self is not destroyed.
 */
const char *
srt_library_get_absolute_path (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return self->absolute_path;
}

/**
 * srt_library_get_messages:
 * @self: a library object
 *
 * Return the diagnostic messages produced while checking this library,
 * if any.
 *
 * Returns: (nullable) (transfer none): A string, which must not be freed,
 *  or %NULL if there were no diagnostic messages.
 */
const char *
srt_library_get_messages (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return self->messages;
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
 * srt_library_get_dependencies:
 * @self: a library
 *
 * Return the dependencies of @self.
 *
 * Returns: (array zero-terminated=1) (element-type utf8): The dependencies
 *  of @self, as a %NULL-terminated array. The pointer remains valid
 *  until @self is destroyed.
 */
const char * const *
srt_library_get_dependencies (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return (const char * const *) self->dependencies;
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

/**
 * srt_library_get_misversioned_symbols:
 * @self: a library
 *
 * Return the symbols that were expected to be provided by self but were
 * available with a different version. Note that this list contains the symbol
 * we expected, not the symbol we found. For example, if we expected to find
 * the versioned symbol `curl_getenv@CURL_OPENSSL_3` in `libcurl.so.4` (as
 * seen in Ubuntu 12.04 and the Steam Runtime), but we actually found either
 * `curl_getenv@CURL_OPENSSL_4` (as seen in Debian 10) or an unversioned
 * curl_getenv, then this list would contain `curl_getenv@CURL_OPENSSL_3`
 *
 * Returns: (array zero-terminated=1) (element-type utf8): The symbols
 *  were available with a different version from @self, as a %NULL-terminated
 *  array. The pointer remains valid until @self is destroyed.
 */
const char * const *
srt_library_get_misversioned_symbols (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return (const char * const *) self->misversioned_symbols;
}

/**
 * srt_check_library_presence:
 * @soname: (type filename): The `SONAME` of a shared library, for example `libjpeg.so.62`
 * @multiarch: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @symbols_path: (nullable): (type filename): The filename of a file listing
 *  symbols, or %NULL if we do not know which symbols the library is meant to
 *  contain.
 * @symbols_format: The format of @symbols_path.
 * @more_details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtLibrary object representing the shared library provided by @soname.
 *  Free with `g_object_unref()`.
 *
 * Attempt to load @soname into a helper subprocess, and check whether it conforms
 * to the ABI provided in `symbols_path`.
 *
 * If @symbols_format is %SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, @symbols_path must
 * list one symbol per line, in the format `jpeg_input_complete@LIBJPEG_6.2`
 * for versioned symbols or `DGifOpen@Base` (or just `DGifOpen`) for symbols
 * not associated with a version.
 *
 * If @symbols_format is %SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS, @symbols_path
 * must be in deb-symbols(5) format. It may list symbols for more SONAMEs than
 * just @soname; if so, they are ignored.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found.
 */
SrtLibraryIssues
srt_check_library_presence (const char *soname,
                            const char *multiarch,
                            const char *symbols_path,
                            SrtLibrarySymbolsFormat symbols_format,
                            SrtLibrary **more_details_out)
{
  return _srt_check_library_presence (NULL, soname, multiarch,
                                      symbols_path, symbols_format,
                                      more_details_out);
}

SrtLibraryIssues
_srt_check_library_presence (const char *helpers_path,
                             const char *soname,
                             const char *multiarch,
                             const char *symbols_path,
                             SrtLibrarySymbolsFormat symbols_format,
                             SrtLibrary **more_details_out)
{
  gchar *helper = NULL;
  gchar *output = NULL;
  gchar *child_stderr = NULL;
  gchar *absolute_path = NULL;
  const gchar *argv[] =
    {
      "inspect-library", "--deb-symbols", soname, symbols_path, NULL
    };
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonArray *missing_array = NULL;
  JsonArray *misversioned_array = NULL;
  JsonArray *dependencies_array = NULL;
  GError *error = NULL;
  GStrv missing_symbols = NULL;
  GStrv misversioned_symbols = NULL;
  GStrv dependencies = NULL;
  SrtLibraryIssues issues = SRT_LIBRARY_ISSUES_NONE;
  GStrv my_environ = NULL;
  const gchar *ld_preload;
  gchar *filtered_preload = NULL;

  g_return_val_if_fail (soname != NULL, SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (multiarch != NULL, SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (more_details_out == NULL || *more_details_out == NULL,
                        SRT_LIBRARY_ISSUES_INTERNAL_ERROR);

  switch (symbols_format)
    {
      case SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN:
        argv[1] = soname;
        argv[2] = symbols_path;
        argv[3] = NULL;
        break;

      case SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS:
        /* do nothing, argv is already set up for this */
        break;

      default:
        g_return_val_if_reached (SRT_LIBRARY_ISSUES_CANNOT_LOAD);
    }

  if (symbols_path == NULL)
    issues |= SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS;

  if (helpers_path == NULL)
    helpers_path = _srt_get_helpers_path ();

  helper = g_strdup_printf ("%s/%s-inspect-library", helpers_path, multiarch);
  argv[0] = helper;
  g_debug ("Checking library %s integrity with %s", soname, helper);

  my_environ = g_get_environ ();
  ld_preload = g_environ_getenv (my_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      my_environ = g_environ_setenv (my_environ, "LD_PRELOAD", filtered_preload, TRUE);
    }

  if (!g_spawn_sync (NULL,       /* working directory */
                     (gchar **) argv,
                     my_environ, /* envp */
                     0,          /* flags */
                     NULL,       /* child setup */
                     NULL,       /* user data */
                     &output,    /* stdout */
                     &child_stderr,
                     &exit_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
      goto out;
    }

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();

  if (!json_parser_load_from_data (parser, output, -1, &error))
    {
      g_debug ("The helper output is not a valid JSON: %s", error->message);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
      goto out;
    }

  node = json_parser_get_root (parser);
  json = json_node_get_object (node);
  if (!json_object_has_member (json, soname))
    {
      g_debug ("The helper output is missing the expected soname %s", soname);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
      goto out;
    }
  json = json_object_get_object_member (json, soname);

  absolute_path = g_strdup (json_object_get_string_member (json, "path"));

  if (json_object_has_member (json, "missing_symbols"))
    missing_array = json_object_get_array_member (json, "missing_symbols");
  if (missing_array != NULL)
    {
      if (json_array_get_length (missing_array) > 0)
        {
          issues |= SRT_LIBRARY_ISSUES_MISSING_SYMBOLS;
          missing_symbols = g_new0 (gchar *, json_array_get_length (missing_array) + 1);
          for (guint i = 0; i < json_array_get_length (missing_array); i++)
            {
              missing_symbols[i] = g_strdup (json_array_get_string_element (missing_array, i));
            }
          missing_symbols[json_array_get_length (missing_array)] = NULL;
        }
    }

  if (json_object_has_member (json, "misversioned_symbols"))
    misversioned_array = json_object_get_array_member (json, "misversioned_symbols");
  if (misversioned_array != NULL)
    {
      if (json_array_get_length (misversioned_array) > 0)
        {
          issues |= SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS;
          misversioned_symbols = g_new0 (gchar *, json_array_get_length (misversioned_array) + 1);
          for (guint i = 0; i < json_array_get_length (misversioned_array); i++)
            {
              misversioned_symbols[i] = g_strdup (json_array_get_string_element (misversioned_array, i));
            }
          misversioned_symbols[json_array_get_length (misversioned_array)] = NULL;
        }
    }

  if (json_object_has_member (json, "dependencies"))
    dependencies_array = json_object_get_array_member (json, "dependencies");
  if (dependencies_array != NULL)
    {
      dependencies = g_new0 (gchar *, json_array_get_length (dependencies_array) + 1);
      for (guint i = 0; i < json_array_get_length (dependencies_array); i++)
        {
          dependencies[i] = g_strdup (json_array_get_string_element (dependencies_array, i));
        }
      dependencies[json_array_get_length (dependencies_array)] = NULL;
    }

out:
  if (more_details_out != NULL)
    *more_details_out = _srt_library_new (multiarch,
                                          absolute_path,
                                          soname,
                                          issues,
                                          child_stderr,
                                          (const char **) missing_symbols,
                                          (const char **) misversioned_symbols,
                                          (const char **) dependencies);

  if (parser != NULL)
    g_object_unref (parser);

  g_strfreev (my_environ);
  g_strfreev (missing_symbols);
  g_strfreev (misversioned_symbols);
  g_strfreev (dependencies);
  g_free (absolute_path);
  g_free (child_stderr);
  g_free (helper);
  g_free (output);
  g_free (filtered_preload);
  g_clear_error (&error);
  return issues;
}
