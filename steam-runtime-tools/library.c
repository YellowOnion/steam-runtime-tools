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
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/utils.h"
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
  gchar *requested_name;
  GStrv dependencies;
  GStrv missing_symbols;
  GStrv misversioned_symbols;
  GQuark multiarch_tuple;
  SrtLibraryIssues issues;
  int exit_status;
  int terminating_signal;
  gchar *real_soname;
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
  PROP_REAL_SONAME,
  PROP_REQUESTED_NAME,
  PROP_MISVERSIONED_SYMBOLS,
  PROP_EXIT_STATUS,
  PROP_TERMINATING_SIGNAL,
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

      case PROP_REAL_SONAME:
        g_value_set_string (value, self->real_soname);
        break;

      case PROP_REQUESTED_NAME:
      case PROP_SONAME:   /* deprecated alias */
        g_value_set_string (value, self->requested_name);
        break;

      case PROP_MISVERSIONED_SYMBOLS:
        g_value_set_boxed (value, self->misversioned_symbols);
        break;

      case PROP_EXIT_STATUS:
        g_value_set_int (value, self->exit_status);
        break;

      case PROP_TERMINATING_SIGNAL:
        g_value_set_int (value, self->terminating_signal);
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

      case PROP_REQUESTED_NAME:
      case PROP_SONAME:   /* deprecated alias */
        tmp = g_value_get_string (value);

        /* We have to ignore NULL, because the deprecated soname
         * property will be explicitly set to NULL during construction,
         * even if the non-deprecated requested-name property is also set. */
        if (tmp != NULL)
          {
            /* Construct-only, mutually exclusive */
            g_return_if_fail (self->requested_name == NULL);
            self->requested_name = g_strdup (tmp);
          }
        break;

      case PROP_REAL_SONAME:
        /* Construct-only */
        g_return_if_fail (self->real_soname == NULL);
        self->real_soname = g_value_dup_string (value);
        break;

      case PROP_MISVERSIONED_SYMBOLS:
        /* Construct-only */
        g_return_if_fail (self->misversioned_symbols == NULL);
        self->misversioned_symbols = g_value_dup_boxed (value);

        /* Guarantee non-NULL */
        if (self->misversioned_symbols == NULL)
          self->misversioned_symbols = g_new0 (gchar *, 1);

        break;

      case PROP_EXIT_STATUS:
        self->exit_status = g_value_get_int (value);
        break;

      case PROP_TERMINATING_SIGNAL:
        self->terminating_signal = g_value_get_int (value);
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
  g_free (self->requested_name);
  g_free (self->real_soname);
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
  properties[PROP_REQUESTED_NAME] =
    g_param_spec_string ("requested-name", "Requested name",
                         "The name that was loaded, for example libz.so.1",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  properties[PROP_SONAME] =
    g_param_spec_string ("soname", "Requested SONAME",
                         "Deprecated alias for requested-name",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED);
  properties[PROP_REAL_SONAME] =
    g_param_spec_string ("real-soname", "Real SONAME",
                         "ELF DT_SONAME found when loading the library",
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
  properties[PROP_EXIT_STATUS] =
    g_param_spec_int ("exit-status", "Exit status", "Exit status of helper(s) executed. 0 on success, positive on unsuccessful exit(), -1 if killed by a signal or not run at all",
                      -1,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);

  properties[PROP_TERMINATING_SIGNAL] =
    g_param_spec_int ("terminating-signal", "Terminating signal", "Signal used to terminate helper process if any, 0 otherwise",
                      0,
                      G_MAXINT,
                      0,
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
 * srt_library_get_requested_name:
 * @self: a library
 *
 * Return the name that was requested to be loaded when checking @self.
 *
 * Returns: A string like `libz.so.1`, which is valid as long as @self
 *  is not destroyed.
 */
const char *
srt_library_get_requested_name (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return self->requested_name;
}

/**
 * srt_library_get_soname:
 * @self: a library
 *
 * Deprecated alias for srt_library_get_requested_name().
 * See also srt_library_get_real_soname(), which might be what you
 * thought this did.
 *
 * Returns: A string like `libz.so.1`, which is valid as long as @self
 *  is not destroyed.
 */
const char *
srt_library_get_soname (SrtLibrary *self)
{
  return srt_library_get_requested_name (self);
}

/**
 * srt_library_get_real_soname:
 * @self: a library
 *
 * Return the ELF `DT_SONAME` found by parsing the loaded library.
 * This is often the same as srt_library_get_requested_name(),
 * but can differ when compatibility aliases are involved.
 *
 * Returns: A string like `libz.so.1`, which is valid as long as @self
 *  is not destroyed, or %NULL if the SONAME could not be determined.
 */
const char *
srt_library_get_real_soname (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), NULL);
  return self->real_soname;
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
 * srt_library_get_exit_status:
 * @self: a library
 *
 * Return the exit status of helpers when testing the given library.
 *
 * Returns: 0 on success, positive on unsuccessful `exit()`, or
 *          -1 if killed by a signal or not run at all
 */
int srt_library_get_exit_status (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), -1);

  return self->exit_status;
}

/**
 * srt_library_get_terminating_signal:
 * @self: a library
 *
 * Return the terminating signal used to terminate the helper if any.
 *
 * Returns: a signal number such as `SIGTERM`, or 0 if not killed by a signal or not run at all.
 */
int srt_library_get_terminating_signal (SrtLibrary *self)
{
  g_return_val_if_fail (SRT_IS_LIBRARY (self), -1);

  return self->terminating_signal;
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
 * @requested_name: (type filename): The `SONAME` or absolute or relative
 *  path of a shared library, for example `libjpeg.so.62`
 * @multiarch: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @symbols_path: (nullable) (type filename): The filename of a file listing
 *  symbols, or %NULL if we do not know which symbols the library is meant to
 *  contain.
 * @symbols_format: The format of @symbols_path.
 * @more_details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtLibrary object representing the shared library provided
 *  by @requested_name. Free with `g_object_unref()`.
 *
 * Attempt to load @requested_name into a helper subprocess, and check whether
 * it conforms to the ABI provided in `symbols_path`.
 *
 * If @symbols_format is %SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, @symbols_path must
 * list one symbol per line, in the format `jpeg_input_complete@LIBJPEG_6.2`
 * for versioned symbols or `DGifOpen@Base` (or just `DGifOpen`) for symbols
 * not associated with a version.
 *
 * If @symbols_format is %SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS, @symbols_path
 * must be in deb-symbols(5) format. It may list symbols for more SONAMEs than
 * just @requested_name; if so, they are ignored.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found.
 */
SrtLibraryIssues
srt_check_library_presence (const char *requested_name,
                            const char *multiarch,
                            const char *symbols_path,
                            SrtLibrarySymbolsFormat symbols_format,
                            SrtLibrary **more_details_out)
{
  return _srt_check_library_presence (NULL, requested_name, multiarch,
                                      symbols_path, NULL,
                                      (gchar **) _srt_peek_environ_nonnull (),
                                      symbols_format, more_details_out);
}

SrtLibraryIssues
_srt_check_library_presence (const char *helpers_path,
                             const char *requested_name,
                             const char *multiarch,
                             const char *symbols_path,
                             const char * const *hidden_deps,
                             gchar **envp,
                             SrtLibrarySymbolsFormat symbols_format,
                             SrtLibrary **more_details_out)
{
  GPtrArray *argv = NULL;
  gchar *output = NULL;
  gchar *child_stderr = NULL;
  gchar *absolute_path = NULL;
  gchar *real_soname = NULL;
  int wait_status = -1;
  int exit_status = -1;
  int terminating_signal = 0;
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
  SrtHelperFlags flags = SRT_HELPER_FLAGS_TIME_OUT;
  GString *log_args = NULL;

  g_return_val_if_fail (requested_name != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (multiarch != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (more_details_out == NULL || *more_details_out == NULL,
                        SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (envp != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);
  g_return_val_if_fail (_srt_check_not_setuid (), SRT_LIBRARY_ISSUES_UNKNOWN);

  if (symbols_path == NULL)
    issues |= SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS;

  argv = _srt_get_helper (helpers_path, multiarch, "inspect-library",
                          flags, &error);

  if (argv == NULL)
    {
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
      /* Use the error message as though the child had printed it on stderr -
       * either way, it's a useful diagnostic */
      child_stderr = g_strdup (error->message);
      goto out;
    }

  log_args = g_string_new ("");

  for (guint i = 0; i < argv->len; ++i)
    {
      if (i > 0)
        {
          g_string_append_c (log_args, ' ');
        }
      g_string_append (log_args, g_ptr_array_index (argv, i));
    }

  g_debug ("Checking library %s integrity with %s ", requested_name,
           log_args->str);
  g_string_free (log_args, TRUE);

  switch (symbols_format)
    {
      case SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN:
        g_ptr_array_add (argv, g_strdup (requested_name));
        g_ptr_array_add (argv, g_strdup (symbols_path));
        break;

      case SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS:
        g_ptr_array_add (argv, g_strdup ("--deb-symbols"));
        g_ptr_array_add (argv, g_strdup (requested_name));
        g_ptr_array_add (argv, g_strdup (symbols_path));
        break;

      default:
        g_return_val_if_reached (SRT_LIBRARY_ISSUES_UNKNOWN);
    }

  for (gsize i = 0; hidden_deps != NULL && hidden_deps[i] != NULL; i++)
    {
      g_ptr_array_add (argv, g_strdup ("--hidden-dependency"));
      g_ptr_array_add (argv, g_strdup (hidden_deps[i]));
    }

  /* NULL terminate the array */
  g_ptr_array_add (argv, NULL);

  my_environ = g_strdupv (envp);
  ld_preload = g_environ_getenv (my_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      my_environ = g_environ_setenv (my_environ, "LD_PRELOAD", filtered_preload, TRUE);
    }

  if (!g_spawn_sync (NULL,       /* working directory */
                     (gchar **) argv->pdata,
                     my_environ, /* envp */
                     G_SPAWN_SEARCH_PATH,          /* flags */
                     _srt_child_setup_unblock_signals,
                     NULL,       /* user data */
                     &output,    /* stdout */
                     &child_stderr,
                     &wait_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
    }

  if (wait_status != 0)
    {
      g_debug ("... wait status %d", wait_status);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;

      if (_srt_process_timeout_wait_status (wait_status, &exit_status, &terminating_signal))
        {
          issues |= SRT_LIBRARY_ISSUES_TIMEOUT;
        }
      goto out;
    }
  else
    {
      exit_status = 0;
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
  if (!json_object_has_member (json, requested_name))
    {
      g_debug ("The helper output is missing the expected SONAME %s",
               requested_name);
      issues |= SRT_LIBRARY_ISSUES_CANNOT_LOAD;
      goto out;
    }
  json = json_object_get_object_member (json, requested_name);

  absolute_path = g_strdup (json_object_get_string_member (json, "path"));

  if (json_object_has_member (json, "SONAME"))
    real_soname = g_strdup (json_object_get_string_member (json, "SONAME"));

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
                                          requested_name,
                                          issues,
                                          child_stderr,
                                          (const char **) missing_symbols,
                                          (const char **) misversioned_symbols,
                                          (const char **) dependencies,
                                          real_soname,
                                          exit_status,
                                          terminating_signal);

  if (parser != NULL)
    g_object_unref (parser);

  g_strfreev (my_environ);
  g_strfreev (missing_symbols);
  g_strfreev (misversioned_symbols);
  g_strfreev (dependencies);
  g_clear_pointer (&argv, g_ptr_array_unref);
  g_free (absolute_path);
  g_free (child_stderr);
  g_free (output);
  g_free (real_soname);
  g_free (filtered_preload);
  g_clear_error (&error);
  return issues;
}

/**
 * _srt_library_get_issues_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "library-issues-summary" property
 *
 * If the provided @json_obj doesn't have a "library-issues-summary" member,
 * or it is malformed, %SRT_LIBRARY_ISSUES_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse,
 * %SRT_LIBRARY_ISSUES_UNKNOWN will be added to the returned #SrtLibraryIssues.
 *
 * Returns: The #SrtLibraryIssues that has been found
 */
SrtLibraryIssues
_srt_library_get_issues_from_report (JsonObject *json_obj)
{
  g_return_val_if_fail (json_obj != NULL, SRT_LIBRARY_ISSUES_UNKNOWN);

  return srt_get_flags_from_json_array (SRT_TYPE_LIBRARY_ISSUES,
                                        json_obj,
                                        "library-issues-summary",
                                        SRT_LIBRARY_ISSUES_UNKNOWN);
}
