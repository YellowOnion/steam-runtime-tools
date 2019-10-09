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

#include "steam-runtime-tools/system-info.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/glib-compat.h"
#include "steam-runtime-tools/graphics.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/locale-internal.h"
#include "steam-runtime-tools/os-internal.h"
#include "steam-runtime-tools/runtime-internal.h"
#include "steam-runtime-tools/steam-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * SECTION:system-info
 * @title: System information
 * @short_description: Cached information about the system
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtSystemInfo is an opaque object representing information about
 * the system. Information is retrieved "lazily"; when it has been
 * retrieved, it is cached until the #SrtSystemInfo is destroyed.
 *
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * The #SrtSystemInfo object is not thread-aware. It should be considered
 * to be "owned" by the thread that created it. Only the thread that
 * "owns" the #SrtSystemInfo may call its methods.
 * Other threads may create their own parallel #SrtSystemInfo object and
 * use that instead, if desired.
 *
 * Ownership can be transferred to other threads by an operation that
 * implies a memory barrier, such as g_atomic_pointer_set() or
 * g_object_ref(), but after this is done the previous owner must not
 * continue to call methods.
 */

typedef enum
{
  TRI_NO = FALSE,
  TRI_YES = TRUE,
  TRI_MAYBE = -1
} Tristate;

struct _SrtSystemInfo
{
  /*< private >*/
  GObject parent;
  /* "" if we have tried and failed to auto-detect */
  gchar *expectations;
  /* Fake environment variables, or %NULL to use the real environment */
  gchar **env;
  /* Path to find helper executables, or %NULL to use $SRT_HELPERS_PATH
   * or the installed helpers */
  gchar *helpers_path;
  /* Multiarch tuple to use for helper executables in cases where it
   * shouldn't matter, or %NULL to use the compiled-in default */
  GQuark primary_multiarch_tuple;
  struct
  {
    /* GQuark => MaybeLocale */
    GHashTable *cached_locales;
    SrtLocaleIssues issues;
    gboolean have_issues;
  } locales;
  struct
  {
    /* install_path != NULL or issues != NONE indicates we have already
     * checked the Steam installation */
    gchar *install_path;
    gchar *data_path;
    gchar *bin32;
    SrtSteamIssues issues;
  } steam;
  struct
  {
    /* path != NULL or issues != NONE indicates we have already checked
     * the Steam Runtime */
    gchar *path;
    gchar *expected_version;
    gchar *version;
    SrtRuntimeIssues issues;
  } runtime;
  struct
  {
    GList *egl;
    GList *vulkan;
    gboolean have_egl;
    gboolean have_vulkan;
  } icds;
  SrtOsRelease os_release;
  SrtTestFlags test_flags;
  Tristate can_write_uinput;
  /* (element-type Abi) */
  GPtrArray *abis;
};

struct _SrtSystemInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_EXPECTATIONS,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtSystemInfo, srt_system_info, G_TYPE_OBJECT)

typedef struct
{
  SrtLocale *locale;
  GError *error;
} MaybeLocale;

static MaybeLocale *
maybe_locale_new_positive (SrtLocale *locale)
{
  MaybeLocale *self;

  g_return_val_if_fail (SRT_IS_LOCALE (locale), NULL);

  self = g_slice_new0 (MaybeLocale);
  self->locale = g_object_ref (locale);
  return self;
}

static MaybeLocale *
maybe_locale_new_negative (GError *error)
{
  MaybeLocale *self;
  
  g_return_val_if_fail (error != NULL, NULL);

  self = g_slice_new0 (MaybeLocale);
  self->error = g_error_copy (error);
  return self;
}

static void
maybe_locale_free (gpointer p)
{
  MaybeLocale *self = p;

  g_clear_object (&self->locale);
  g_clear_error (&self->error);
  g_slice_free (MaybeLocale, self);
}

typedef struct
{
  GQuark multiarch_tuple;
  Tristate can_run;
  GHashTable *cached_results;
  SrtLibraryIssues cached_combined_issues;
  gboolean libraries_cache_available;

  GHashTable *cached_graphics_results;
  SrtGraphicsIssues cached_combined_graphics_issues;
  gboolean graphics_cache_available;
} Abi;

static Abi *
ensure_abi (SrtSystemInfo *self,
            const char *multiarch_tuple)
{
  GQuark quark;
  guint i;
  Abi *abi = NULL;

  quark = g_quark_from_string (multiarch_tuple);

  for (i = 0; i < self->abis->len; i++)
    {
      abi = g_ptr_array_index (self->abis, i);

      if (abi->multiarch_tuple == quark)
        return abi;
    }

  abi = g_slice_new0 (Abi);
  abi->multiarch_tuple = quark;
  abi->can_run = TRI_MAYBE;
  abi->cached_results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  abi->cached_combined_issues = SRT_LIBRARY_ISSUES_NONE;
  abi->libraries_cache_available = FALSE;
  abi->cached_graphics_results = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
  abi->cached_combined_graphics_issues = SRT_GRAPHICS_ISSUES_NONE;

  /* transfer ownership to self->abis */
  g_ptr_array_add (self->abis, abi);
  return abi;
}

static void
abi_free (gpointer self)
{
  Abi *abi = self;
  if (abi->cached_results != NULL)
    g_hash_table_unref (abi->cached_results);

  if (abi->cached_graphics_results != NULL)
    g_hash_table_unref (abi->cached_graphics_results);

  g_slice_free (Abi, self);
}

static void
srt_system_info_init (SrtSystemInfo *self)
{
  self->can_write_uinput = TRI_MAYBE;

  /* Assume that in practice we will usually add two ABIs: amd64 and i386 */
  self->abis = g_ptr_array_new_full (2, abi_free);

  _srt_os_release_init (&self->os_release);
}

static void
srt_system_info_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  switch (prop_id)
    {
      case PROP_EXPECTATIONS:
        g_value_set_string (value, self->expectations);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_system_info_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  switch (prop_id)
    {
      case PROP_EXPECTATIONS:
        /* Construct-only */
        g_return_if_fail (self->expectations == NULL);
        self->expectations = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/*
 * Forget any cached information about locales.
 */
static void
forget_locales (SrtSystemInfo *self)
{
  g_clear_pointer (&self->locales.cached_locales, g_hash_table_unref);
  self->locales.issues = SRT_LOCALE_ISSUES_NONE;
  self->locales.have_issues = FALSE;
}

/*
 * Forget any cached information about the Steam Runtime.
 */
static void
forget_runtime (SrtSystemInfo *self)
{
  g_clear_pointer (&self->runtime.path, g_free);
  g_clear_pointer (&self->runtime.version, g_free);
  g_clear_pointer (&self->runtime.expected_version, g_free);
  self->runtime.issues = SRT_RUNTIME_ISSUES_NONE;
}

/*
 * Forget any cached information about the OS.
 */
static void
forget_os (SrtSystemInfo *self)
{
  _srt_os_release_clear (&self->os_release);
  forget_runtime (self);
}

/*
 * Forget any cached information about the Steam installation.
 */
static void
forget_steam (SrtSystemInfo *self)
{
  forget_runtime (self);
  self->steam.issues = SRT_STEAM_ISSUES_NONE;
  g_clear_pointer (&self->steam.install_path, g_free);
  g_clear_pointer (&self->steam.data_path, g_free);
  g_clear_pointer (&self->steam.bin32, g_free);
}

/*
 * Forget any cached information about ICDs.
 */
static void
forget_icds (SrtSystemInfo *self)
{
  self->icds.have_egl = FALSE;
  g_list_free_full (self->icds.egl, g_object_unref);
  self->icds.egl = NULL;
  self->icds.have_vulkan = FALSE;
  g_list_free_full (self->icds.vulkan, g_object_unref);
  self->icds.vulkan = NULL;
}

static void
srt_system_info_finalize (GObject *object)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  forget_icds (self);
  forget_locales (self);
  forget_os (self);
  forget_runtime (self);
  forget_steam (self);

  g_clear_pointer (&self->abis, g_ptr_array_unref);
  g_free (self->expectations);
  g_free (self->helpers_path);
  g_strfreev (self->env);

  G_OBJECT_CLASS (srt_system_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_system_info_class_init (SrtSystemInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_system_info_get_property;
  object_class->set_property = srt_system_info_set_property;
  object_class->finalize = srt_system_info_finalize;

  properties[PROP_EXPECTATIONS] =
    g_param_spec_string ("expectations", "Expectations",
                         "Path to a directory containing information "
                         "about the properties we expect the system "
                         "to have, or NULL if unknown",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * srt_system_info_new:
 * @expectations: (nullable) (type filename): Path to a directory
 *  containing details of the state that the system is expected to have
 *
 * Return a new #SrtSystemInfo.
 *
 * The @expectations directory should contain a subdirectory for each
 * supported CPU architecture, named for the multiarch tuple as printed
 * by `gcc -print-multiarch` in the Steam Runtime (in practice this means
 * %SRT_ABI_I386 or %SRT_ABI_X86_64).
 *
 * The per-architecture directories may contain files whose names end with
 * `.symbols`. Those files are interpreted as describing libraries that
 * the runtime environment should support, in
 * [deb-symbols(5)](https://manpages.debian.org/deb-symbols.5) format.
 *
 * Returns: (transfer full): A new #SrtSystemInfo. Free with g_object_unref()
 */
SrtSystemInfo *
srt_system_info_new (const char *expectations)
{
  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail ((expectations == NULL ||
                         g_file_test (expectations, G_FILE_TEST_IS_DIR)),
                        NULL);
  return g_object_new (SRT_TYPE_SYSTEM_INFO,
                       "expectations", expectations,
                       NULL);
}

/**
 * srt_system_info_can_run:
 * @self: A #SrtSystemInfo object
 * @multiarch_tuple: A multiarch tuple defining an ABI, as printed
 *  by `gcc -print-multiarch` in the Steam Runtime
 *
 * Check whether an executable for the given ABI can be run.
 *
 * For this check (and all similar checks) to work as intended, the
 * contents of the `libsteam-runtime-tools-0-helpers:i386` package must
 * be available in the same directory hierarchy as the
 * `libsteam-runtime-tools-0` shared library, something like this:
 *
 * |[
 * any directory/
 *      lib/
 *          x86_64-linux-gnu/
 *              libsteam-runtime-tools-0.so.0
 *      libexec/
 *          steam-runtime-tools-0/
 *              i386-linux-gnu-*
 *              x86_64-linux-gnu-*
 * ]|
 *
 * Returns: %TRUE if executables belonging to @multiarch_tuple can be run
 */
gboolean
srt_system_info_can_run (SrtSystemInfo *self,
                         const char *multiarch_tuple)
{
  Abi *abi = NULL;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);
  g_return_val_if_fail (multiarch_tuple != NULL, FALSE);

  abi = ensure_abi (self, multiarch_tuple);

  if (abi->can_run == TRI_MAYBE)
    {
      if (_srt_architecture_can_run (self->helpers_path, multiarch_tuple))
        abi->can_run = TRI_YES;
      else
        abi->can_run = TRI_NO;
    }

  return (abi->can_run == TRI_YES);
}

/**
 * srt_system_info_can_write_to_uinput:
 * @self: a #SrtSystemInfo object
 *
 * Return %TRUE if the current user can write to `/dev/uinput`.
 * This is required for the Steam client to be able to emulate gamepads,
 * keyboards, mice and other input devices based on input from the
 * Steam Controller or a remote streaming client.
 *
 * Returns: %TRUE if `/dev/uinput` can be opened for writing
 */
gboolean
srt_system_info_can_write_to_uinput (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), FALSE);

  if (self->can_write_uinput == TRI_MAYBE)
    {
      int fd = open ("/dev/uinput", O_WRONLY | O_NONBLOCK);

      if (fd >= 0)
        {
          g_debug ("Successfully opened /dev/uinput for writing");
          self->can_write_uinput = TRI_YES;
          close (fd);
        }
      else
        {
          g_debug ("Failed to open /dev/uinput for writing: %s",
                   g_strerror (errno));
          self->can_write_uinput = TRI_NO;
        }
    }

  return (self->can_write_uinput == TRI_YES);
}

static gint
library_compare (SrtLibrary *a, SrtLibrary *b)
{
  return g_strcmp0 (srt_library_get_soname (a), srt_library_get_soname (b));
}

static gint
graphics_compare (SrtGraphics *a, SrtGraphics *b)
{
  int aKey = _srt_graphics_hash_key (srt_graphics_get_window_system (a),
                                     srt_graphics_get_rendering_interface (a));
  int bKey = _srt_graphics_hash_key (srt_graphics_get_window_system (b),
                                     srt_graphics_get_rendering_interface (b));
  return (aKey < bKey) ? -1 : (aKey > bKey);
}

static gchar **
get_environ (SrtSystemInfo *self)
{
  if (self->env != NULL)
    return self->env;
  else
    return environ;
}

static gboolean
ensure_expectations (SrtSystemInfo *self)
{
  g_return_val_if_fail (_srt_check_not_setuid (), FALSE);

  if (self->expectations == NULL)
    {
      const char *runtime;
      const char *sysroot = "/";
      gchar *def;

      runtime = g_environ_getenv (get_environ (self), "STEAM_RUNTIME");

      if (runtime != NULL && runtime[0] == '/')
        sysroot = runtime;

      def = g_build_filename (sysroot, "usr", "lib", "steamrt",
                              "expectations", NULL);

      if (g_file_test (def, G_FILE_TEST_IS_DIR))
        self->expectations = g_steal_pointer (&def);
      else
        self->expectations = g_strdup ("");

      g_free (def);
    }

  return self->expectations[0] != '\0';
}

/**
 * srt_system_info_check_libraries:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @libraries_out: (out) (optional) (element-type SrtLibrary) (transfer full):
 *  Used to return a #GList object where every element of said list is an
 *  #SrtLibrary object, representing every `SONAME` found from the expectations
 *  folder. Free with `g_list_free_full(libraries, g_object_unref)`.
 *
 * Check if the running system has all the expected libraries, and related symbols,
 * as listed in the `deb-symbols(5)` files `*.symbols` in the @multiarch
 * subdirectory of #SrtSystemInfo:expectations.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found.
 */
SrtLibraryIssues
srt_system_info_check_libraries (SrtSystemInfo *self,
                                 const gchar *multiarch_tuple,
                                 GList **libraries_out)
{
  Abi *abi = NULL;
  gchar *dir_path = NULL;
  const gchar *filename = NULL;
  gchar *symbols_file = NULL;
  size_t len = 0;
  ssize_t chars;
  GDir *dir = NULL;
  FILE *fp = NULL;
  GError *error = NULL;
  SrtLibraryIssues ret = SRT_LIBRARY_ISSUES_INTERNAL_ERROR;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (libraries_out == NULL || *libraries_out == NULL,
                        SRT_LIBRARY_ISSUES_INTERNAL_ERROR);

  if (!ensure_expectations (self))
    {
      /* We don't know which libraries to check. */
      return SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS;
    }

  abi = ensure_abi (self, multiarch_tuple);

  /* If we cached already the result, we return it */
  if (abi->libraries_cache_available)
    {
      if (libraries_out != NULL)
        {
          *libraries_out = g_list_sort (g_hash_table_get_values (abi->cached_results),
                                        (GCompareFunc) library_compare);
          g_list_foreach (*libraries_out, (GFunc) G_CALLBACK (g_object_ref), NULL);
        }

      return abi->cached_combined_issues;
    }

  dir_path = g_build_filename (self->expectations, multiarch_tuple, NULL);
  dir = g_dir_open (dir_path, 0, &error);
  if (error)
    {
      g_debug ("An error occurred while opening the symbols directory: %s", error->message);
      g_clear_error (&error);
      ret = SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS;
      goto out;
    }

  while ((filename = g_dir_read_name (dir)))
    {
      char *line = NULL;

      if (!g_str_has_suffix (filename, ".symbols"))
        continue;

      symbols_file = g_build_filename (dir_path, filename, NULL);
      fp = fopen(symbols_file, "r");

      if (fp == NULL)
        {
          int saved_errno = errno;
          g_debug ("Error reading \"%s\": %s\n", symbols_file, strerror (saved_errno));
          goto out;
        }

      while ((chars = getline(&line, &len, fp)) != -1)
        {
          char *pointer_into_line = line;
          if (line[chars - 1] == '\n')
            line[chars - 1] = '\0';

          if (line[0] == '\0')
            continue;

          if (line[0] != '#' && line[0] != '*' && line[0] != '|' && line[0] != ' ')
            {
              /* This line introduces a new SONAME. We extract it and call
                * `_srt_check_library_presence` with the symbols file where we
                * found it, as an argument. */
              SrtLibrary *library = NULL;
              char *soname = g_strdup (strsep (&pointer_into_line, " \t"));
              abi->cached_combined_issues |= _srt_check_library_presence (self->helpers_path,
                                                                          soname,
                                                                          multiarch_tuple,
                                                                          symbols_file,
                                                                          SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
                                                                          &library);
              g_hash_table_insert (abi->cached_results, soname, library);
            }
        }
      free (line);
      g_clear_pointer (&symbols_file, g_free);
      g_clear_pointer (&fp, fclose);
    }

  abi->libraries_cache_available = TRUE;
  if (libraries_out != NULL)
    {
      *libraries_out = g_list_sort (g_hash_table_get_values (abi->cached_results),
                                    (GCompareFunc) library_compare);
      g_list_foreach (*libraries_out, (GFunc) G_CALLBACK (g_object_ref), NULL);
    }

  ret = abi->cached_combined_issues;

  out:
    g_clear_pointer (&symbols_file, g_free);
    g_clear_pointer (&dir_path, g_free);

    if (fp != NULL)
      g_clear_pointer (&fp, fclose);

    if (dir != NULL)
      g_dir_close (dir);

    return ret;

}

/**
 * srt_system_info_check_library:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @soname: (type filename): The `SONAME` of a shared library, for example `libjpeg.so.62`.
 * @more_details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtLibrary object representing the shared library provided by @soname.
 *  Free with `g_object_unref()`.
 *
 * Check if @soname is available in the running system and whether it conforms
 * to the `deb-symbols(5)` files `*.symbols` in the @multiarch
 * subdirectory of #SrtSystemInfo:expectations.
 *
 * Returns: A bitfield containing problems, or %SRT_LIBRARY_ISSUES_NONE
 *  if no problems were found.
 */
SrtLibraryIssues
srt_system_info_check_library (SrtSystemInfo *self,
                               const gchar *multiarch_tuple,
                               const gchar *soname,
                               SrtLibrary **more_details_out)
{
  Abi *abi = NULL;
  SrtLibrary *library = NULL;
  const gchar *filename = NULL;
  gchar *symbols_file = NULL;
  gchar *dir_path = NULL;
  gchar *line = NULL;
  size_t len = 0;
  ssize_t chars;
  FILE *fp = NULL;
  SrtLibraryIssues issues;
  GDir *dir = NULL;
  GError *error = NULL;
  SrtLibraryIssues ret = SRT_LIBRARY_ISSUES_INTERNAL_ERROR;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (soname != NULL, SRT_LIBRARY_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (more_details_out == NULL || *more_details_out == NULL,
                        SRT_LIBRARY_ISSUES_INTERNAL_ERROR);

  abi = ensure_abi (self, multiarch_tuple);

  /* If we have the result already in cache, we return it */
  library = g_hash_table_lookup (abi->cached_results, soname);
  if (library != NULL)
    {
      if (more_details_out != NULL)
        *more_details_out = g_object_ref (library);
      return srt_library_get_issues (library);
    }

  if (ensure_expectations (self))
    {
      dir_path = g_build_filename (self->expectations, multiarch_tuple, NULL);
      dir = g_dir_open (dir_path, 0, &error);

      if (error)
        {
          g_debug ("An error occurred while opening the symbols directory: %s", error->message);
          g_clear_error (&error);
        }
    }

  while (dir != NULL && (filename = g_dir_read_name (dir)))
    {
      if (!g_str_has_suffix (filename, ".symbols"))
        continue;

      symbols_file = g_build_filename (dir_path, filename, NULL);
      fp = fopen(symbols_file, "r");

      if (fp == NULL)
        {
          int saved_errno = errno;
          g_debug ("Error reading \"%s\": %s\n", symbols_file, strerror (saved_errno));
          goto out;
        }

      while ((chars = getline(&line, &len, fp)) != -1)
        {
          char *pointer_into_line = line;
          if (line[chars - 1] == '\n')
            line[chars - 1] = '\0';

          if (line[0] == '\0')
            continue;

          if (line[0] != '#' && line[0] != '*' && line[0] != '|' && line[0] != ' ')
            {
              /* This line introduces a new SONAME, which might
                * be the one we are interested in. */
              char *soname_found = g_strdup (strsep (&pointer_into_line, " \t"));
              if (g_strcmp0 (soname_found, soname) == 0)
                {
                  issues = _srt_check_library_presence (self->helpers_path,
                                                        soname_found,
                                                        multiarch_tuple,
                                                        symbols_file,
                                                        SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
                                                        &library);
                  g_hash_table_insert (abi->cached_results, soname_found, library);
                  abi->cached_combined_issues |= issues;
                  if (more_details_out != NULL)
                    *more_details_out = g_object_ref (library);
                  free (line);
                  ret = issues;
                  goto out;
                }
              free (soname_found);
            }
        }
      g_clear_pointer (&symbols_file, g_free);
      g_clear_pointer (&line, g_free);
      g_clear_pointer (&fp, fclose);
    }

  /* The SONAME's symbols file is not available.
   * We do instead a simple absence/presence check. */
  issues = _srt_check_library_presence (self->helpers_path,
                                        soname,
                                        multiarch_tuple,
                                        NULL,
                                        SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
                                        &library);
  g_hash_table_insert (abi->cached_results, g_strdup (soname), library);
  abi->cached_combined_issues |= issues;
  if (more_details_out != NULL)
    *more_details_out = g_object_ref (library);

  ret = issues;

  out:
    g_clear_pointer (&symbols_file, g_free);
    g_clear_pointer (&dir_path, g_free);

    if (fp != NULL)
      g_clear_pointer (&fp, fclose);

    if (dir != NULL)
      g_dir_close (dir);

    return ret;
}

/*
 * Forget whether we can load libraries.
 */
static void
forget_libraries (SrtSystemInfo *self)
{
  gsize i;

  for (i = 0; i < self->abis->len; i++)
    {
      Abi *abi = g_ptr_array_index (self->abis, i);

      g_hash_table_remove_all (abi->cached_results);
      abi->cached_combined_issues = SRT_LIBRARY_ISSUES_NONE;
      abi->libraries_cache_available = FALSE;
    }
}

/*
 * Forget cached graphics results.
 */
static void
forget_graphics_results (SrtSystemInfo *self)
{
  gsize i;

  for (i = 0; i < self->abis->len; i++)
    {
      Abi *abi = g_ptr_array_index (self->abis, i);

      g_hash_table_remove_all (abi->cached_graphics_results);
      abi->cached_combined_graphics_issues = SRT_GRAPHICS_ISSUES_NONE;
      abi->graphics_cache_available = FALSE;
    }
}

/**
 * srt_system_info_check_graphics:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
 * @window_system: The window system to check.
 * @rendering_interface: The graphics renderng interface to check.
 * @details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtGraphics object representing the items tested and results.
 *  Free with `g_object_unref()`.
 *
 * Returns: A bitfield containing problems, or %SRT_GRAPHICS_ISSUES_NONE
 *  if no problems were found.
 */
SrtGraphicsIssues
srt_system_info_check_graphics (SrtSystemInfo *self,
        const char *multiarch_tuple,
        SrtWindowSystem window_system,
        SrtRenderingInterface rendering_interface,
        SrtGraphics **details_out)
{
  Abi *abi = NULL;
  SrtGraphicsIssues issues;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (details_out == NULL || *details_out == NULL,
                        SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (window_system >= 0, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (window_system < SRT_N_WINDOW_SYSTEMS, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (rendering_interface >= 0, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (rendering_interface < SRT_N_RENDERING_INTERFACES, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);

  abi = ensure_abi (self, multiarch_tuple);

  /* If we have the result already in cache, we return it */
  int hash_key = _srt_graphics_hash_key (window_system, rendering_interface);
  SrtGraphics *graphics = g_hash_table_lookup (abi->cached_graphics_results, GINT_TO_POINTER(hash_key));
  if (graphics != NULL)
    {
      if (details_out != NULL)
        *details_out = g_object_ref (graphics);
      return srt_graphics_get_issues (graphics);
    }

  graphics = NULL;
  issues = _srt_check_graphics (self->helpers_path,
                                self->test_flags,
                                multiarch_tuple,
                                window_system,
                                rendering_interface,
                                &graphics);
  g_hash_table_insert (abi->cached_graphics_results, GINT_TO_POINTER(hash_key), graphics);
  abi->cached_combined_graphics_issues |= issues;
  if (details_out != NULL)
    *details_out = g_object_ref (graphics);

  return issues;
}

/**
 * srt_system_info_check_all_graphics:
 * @self: The #SrtSystemInfo object to use.
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386, representing an ABI.
*
 * Check whether various combinations of rendering interface and windowing
 * system are available. The specific combinations of rendering interface and
 * windowing system that are returned are not guaranteed, but will include at
 * least %SRT_RENDERER_GL on %SRT_WINDOW_SYSTEM_GLX. Additional combinations
 * will be added in future versions of this library.
 *
 * Returns: (transfer full) (type SrtGraphics): A list of #SrtGraphics objects
 * representing the items tested and results.
 * Free with 'glist_free_full(list, g_object_unref)`.
 */
GList * srt_system_info_check_all_graphics (SrtSystemInfo *self,
    const char *multiarch_tuple)
{
  Abi *abi = NULL;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  abi = ensure_abi (self, multiarch_tuple);
  GList *list = NULL;

  /* If we cached already the result, we return it */
  if (abi->graphics_cache_available)
    {
      list = g_list_sort (g_hash_table_get_values (abi->cached_graphics_results),
                                    (GCompareFunc) graphics_compare);
      g_list_foreach (list, (GFunc) G_CALLBACK (g_object_ref), NULL);

      return list;
    }

  // Try each of glx and gles
  // Try each window system

  abi->cached_combined_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_GLX,
                                    SRT_RENDERING_INTERFACE_GL,
                                    NULL);

  abi->cached_combined_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_EGL_X11,
                                    SRT_RENDERING_INTERFACE_GL,
                                    NULL);

  abi->cached_combined_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_EGL_X11,
                                    SRT_RENDERING_INTERFACE_GLESV2,
                                    NULL);

  abi->cached_combined_issues |=
    srt_system_info_check_graphics (self,
                                    multiarch_tuple,
                                    SRT_WINDOW_SYSTEM_X11,
                                    SRT_RENDERING_INTERFACE_VULKAN,
                                    NULL);

  abi->graphics_cache_available = TRUE;

  list = g_list_sort (g_hash_table_get_values (abi->cached_graphics_results),
                      (GCompareFunc) graphics_compare);
  g_list_foreach (list, (GFunc) G_CALLBACK (g_object_ref), NULL);

  return list;
}

/**
 * srt_system_info_set_environ:
 * @self: The #SrtSystemInfo
 * @env: (nullable) (array zero-terminated=1) (element-type filename) (transfer none): An
 *  array of environment variables
 *
 * Use @env instead of the real environment variable block `environ`
 * when locating the Steam Runtime.
 *
 * If @env is %NULL, go back to using the real environment variables.
 */
void
srt_system_info_set_environ (SrtSystemInfo *self,
                             gchar * const *env)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));

  forget_libraries (self);
  forget_graphics_results (self);
  forget_locales (self);
  forget_os (self);
  g_strfreev (self->env);
  self->env = g_strdupv ((gchar **) env);

  /* Forget what we know about Steam because it is bounded to the environment. */
  forget_steam (self);
}

static void
ensure_steam_cached (SrtSystemInfo *self)
{
  if (self->steam.issues == SRT_STEAM_ISSUES_NONE &&
      self->steam.install_path == NULL &&
      self->steam.data_path == NULL)
    self->steam.issues = _srt_steam_check (self->env,
                                           &self->steam.install_path,
                                           &self->steam.data_path,
                                           &self->steam.bin32);
}

/**
 * srt_system_info_get_steam_issues:
 * @self: The #SrtSystemInfo object
 *
 * Detect and return any problems encountered with the Steam installation.
 *
 * Returns: Any problems detected with the Steam installation,
 *  or %SRT_RUNTIME_ISSUES_NONE if no problems were detected
 */
SrtSteamIssues
srt_system_info_get_steam_issues (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self),
                        SRT_STEAM_ISSUES_INTERNAL_ERROR);

  ensure_steam_cached (self);
  return self->steam.issues;
}

/**
 * srt_system_info_dup_steam_installation_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam installation in use (the
 * directory containing `steam.sh` and `ubuntu12_32/` among other
 * files and directories).
 *
 * This directory is analogous to `C:\Program Files\Steam` in a
 * typical Windows installation of Steam, and is typically of the form
 * `/home/me/.local/share/Steam`. It is also known as the "Steam root",
 * and is canonically accessed via the symbolic link `~/.steam/root`
 * (known as the "Steam root link").
 *
 * Under normal circumstances, this is the same directory as
 * srt_system_info_dup_steam_data_path(). However, it is possible to
 * construct situations where they are different, for example when a
 * Steam developer tests a new client build in its own installation
 * directory in conjunction with an existing data directory from the
 * production client, or when Steam was first installed using a Debian
 * package that suffered from
 * [#916303](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916303)
 * (which resulted in `~/.steam/steam` being a plain directory, not a
 * symbolic link).
 *
 * If the Steam installation could not be found, flags will
 * be set in the result of srt_system_info_get_steam_issues() to indicate
 * why: at least %SRT_STEAM_ISSUES_CANNOT_FIND, and possibly others.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam installation, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_steam_installation_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (self->steam.install_path);
}

/**
 * srt_system_info_dup_steam_data_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam data directory in use (the
 * directory containing `appcache/`, `userdata/` and the default
 * `steamapps/` or `SteamApps/` installation path for games, among other
 * files and directories).
 *
 * This directory is analogous to `C:\Program Files\Steam` in a
 * typical Windows installation of Steam, and is typically of the form
 * `/home/me/.local/share/Steam`. It is canonically accessed via the
 * symbolic link `~/.steam/steam` (known as the "Steam data link").
 *
 * Under normal circumstances, this is the same directory as
 * srt_system_info_dup_steam_installation_path(). However, it is possible
 * to construct situations where they are different, for example when a
 * Steam developer tests a new client build in its own installation
 * directory in conjunction with an existing data directory from the
 * production client, or when Steam was first installed using a Debian
 * package that suffered from
 * [#916303](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916303)
 * (which resulted in `~/.steam/steam` being a plain directory, not a
 * symbolic link).
 *
 * If the Steam data could not be found, flags will
 * be set in the result of srt_system_info_get_steam_issues() to indicate
 * why: at least %SRT_STEAM_ISSUES_CANNOT_FIND_DATA, and possibly others.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam installation, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_steam_data_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_steam_cached (self);
  return g_strdup (self->steam.data_path);
}

static void
ensure_os_cached (SrtSystemInfo *self)
{
  if (!self->os_release.populated)
    _srt_os_release_populate (&self->os_release, self->env);
}

/**
 * srt_system_info_dup_os_build_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a machine-readable identifier for the system image used as the
 * origin for a distribution, for example `0.20190925.0`. If called
 * from inside a Steam Runtime container, return the Steam Runtime build
 * ID, which currently looks like `0.20190925.0`.
 *
 * In operating systems that do not use image-based installation, such
 * as Debian, this will be %NULL.
 *
 * This is the `BUILD_ID` from os-release(5).
 *
 * Returns: (transfer full) (type utf8): The build ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_build_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.build_id);
}

/**
 * srt_system_info_dup_os_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable operating system identifier,
 * for example `debian` or `arch`. If called from inside a Steam Runtime
 * container, return `steamrt`.
 *
 * This is the `ID` in os-release(5). If os-release(5) is not available,
 * future versions of this library might derive a similar ID from
 * lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The OS ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.id);
}

static const char WHITESPACE[] = " \t\n\r";

/**
 * srt_system_info_dup_os_id_like:
 * @self: The #SrtSystemInfo object
 * @include_self: If %TRUE, include srt_system_info_dup_os_id() in the
 *  returned array (if known)
 *
 * Return an array of lower-case machine-readable operating system
 * identifiers similar to srt_system_info_dup_os_id() describing OSs
 * that this one resembles or is derived from.
 *
 * For example, the Steam Runtime 1 'scout' is derived from Ubuntu,
 * which is itself derived from Debian, so srt_system_info_dup_os_id_like()
 * would return `{ "debian", "ubuntu", NULL }` if @include_self is false,
 * `{ "steamrt", "debian", "ubuntu", NULL }` otherwise.
 *
 * This is the `ID_LIKE` field from os-release(5), possibly combined
 * with the `ID` field.
 *
 * Returns: (array zero-terminated=1) (transfer full) (element-type utf8) (nullable): An
 *  array of OS IDs, or %NULL if nothing is known.
 *  Free with g_strfreev().
 */
gchar **
srt_system_info_dup_os_id_like (SrtSystemInfo *self,
                                gboolean include_self)
{
  GPtrArray *builder;
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);

  builder = g_ptr_array_new_with_free_func (g_free);

  if (self->os_release.id != NULL && include_self)
    g_ptr_array_add (builder, g_strdup (self->os_release.id));

  if (self->os_release.id_like != NULL)
    {
      GStrv split;
      gsize i;

      split = g_strsplit_set (self->os_release.id_like, WHITESPACE, -1);

      for (i = 0; split != NULL && split[i] != NULL; i++)
        g_ptr_array_add (builder, g_steal_pointer (&split[i]));

      /* We already transferred ownership of the contents */
      g_free (split);
    }

  if (builder->len > 0)
    {
      g_ptr_array_add (builder, NULL);
      return (gchar **) g_ptr_array_free (builder, FALSE);
    }
  else
    {
      g_ptr_array_free (builder, TRUE);
      return NULL;
    }
}

/**
 * srt_system_info_dup_os_name:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system without
 * its version, for example `Debian GNU/Linux` or `Arch Linux`.
 *
 * This is the `NAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * name from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The name, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_name (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.name);
}

/**
 * srt_system_info_dup_os_pretty_name:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system,
 * including its version if any, for example `Debian GNU/Linux 10 (buster)`
 * or `Arch Linux`.
 *
 * If the OS uses rolling releases, this will probably be the same as
 * or similar to srt_system_info_dup_os_name().
 *
 * This is the `PRETTY_NAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * name from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The name, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_pretty_name (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.pretty_name);
}

/**
 * srt_system_info_dup_os_variant:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system variant,
 * for example `Workstation Edition`, `Server Edition` or
 * `Raspberry Pi Edition`. In operating systems that do not have
 * formal variants this will usually be %NULL.
 *
 * This is the `VARIANT` in os-release(5).
 *
 * Returns: (transfer full) (type utf8): The name, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_variant (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.variant);
}

/**
 * srt_system_info_dup_os_variant_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable identifier for the operating system
 * variant in a form suitable for use in filenames, for example
 * `workstation`, `server` or `rpi`. In operating systems that do not
 * have formal variants this will usually be %NULL.
 *
 * This is the `VARIANT_ID` in os-release(5).
 *
 * Returns: (transfer full) (type utf8): The variant ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_variant_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.variant_id);
}

/**
 * srt_system_info_dup_os_version_codename:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable identifier for the operating
 * system version codename, for example `buster` for Debian 10 "buster".
 * In operating systems that do not use codenames in machine-readable
 * contexts, this will usually be %NULL.
 *
 * This is the `VERSION_CODENAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * codename from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The codename, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_version_codename (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.version_codename);
}

/**
 * srt_system_info_dup_os_version_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a machine-readable identifier for the operating system version,
 * for example `10` for Debian 10 "buster". In operating systems that
 * only have rolling releases, such as Arch Linux, or in OS branches
 * that behave like rolling releases, such as Debian unstable, this
 * will usually be %NULL.
 *
 * This is the `VERSION_ID` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * identifier from lsb_release(1).
 *
 * Returns: (transfer full) (type utf8): The ID, or %NULL if not known.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_os_version_id (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_os_cached (self);
  return g_strdup (self->os_release.version_id);
}

/**
 * srt_system_info_set_expected_runtime_version:
 * @self: The #SrtSystemInfo object
 * @version: (nullable): The expected version number, such as `0.20190711.3`,
 *  or %NULL if there is no particular expectation
 *
 * Set the expected version number of the Steam Runtime. Invalidate any
 * cached information about the Steam Runtime if it differs from the
 * previous expectation.
 */
void
srt_system_info_set_expected_runtime_version (SrtSystemInfo *self,
                                              const char *version)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));

  if (g_strcmp0 (version, self->runtime.expected_version) != 0)
    {
      forget_runtime (self);
      g_clear_pointer (&self->runtime.expected_version, g_free);
      self->runtime.expected_version = g_strdup (version);
    }
}

/**
 * srt_system_info_dup_expected_runtime_version:
 * @self: The #SrtSystemInfo object
 *
 * Returns: (transfer full) (type utf8): The expected version number of
 *  the Steam Runtime, or %NULL if no particular version is expected.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_expected_runtime_version (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  return g_strdup (self->runtime.expected_version);
}

static void
ensure_runtime_cached (SrtSystemInfo *self)
{
  ensure_os_cached (self);
  ensure_steam_cached (self);

  if (self->runtime.issues == SRT_RUNTIME_ISSUES_NONE &&
      self->runtime.path == NULL)
    {
      if (g_strcmp0 (self->os_release.id, "steamrt") == 0)
        {
          self->runtime.path = g_strdup ("/");
          self->runtime.version = g_strdup (self->os_release.build_id);

          if (self->runtime.expected_version != NULL
              && g_strcmp0 (self->runtime.expected_version, self->runtime.version) != 0)
            {
              self->runtime.issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;
            }

          if (self->runtime.version == NULL)
            {
              self->runtime.issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
            }
          else
            {
              const char *p;

              for (p = self->runtime.version; *p != '\0'; p++)
                {
                  if (!g_ascii_isdigit (*p) && *p != '.')
                    self->runtime.issues |= SRT_RUNTIME_ISSUES_UNOFFICIAL;
                }
            }
        }
      else
        {
          self->runtime.issues = _srt_runtime_check (self->steam.bin32,
                                                     self->runtime.expected_version,
                                                     self->env,
                                                     &self->runtime.version,
                                                     &self->runtime.path);
        }
    }
}

/**
 * srt_system_info_get_runtime_issues:
 * @self: The #SrtSystemInfo object
 *
 * Detect and return any problems encountered with the Steam Runtime.
 *
 * Returns: Any problems detected with the Steam Runtime,
 *  or %SRT_RUNTIME_ISSUES_NONE if no problems were detected
 */
SrtRuntimeIssues
srt_system_info_get_runtime_issues (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self),
                        SRT_RUNTIME_ISSUES_INTERNAL_ERROR);

  ensure_runtime_cached (self);
  return self->runtime.issues;
}

/**
 * srt_system_info_dup_runtime_path:
 * @self: The #SrtSystemInfo object
 *
 * Return the absolute path to the Steam Runtime in use.
 *
 * For the `LD_LIBRARY_PATH`-based Steam Runtime, this is the directory
 * containing `run.sh`, `version.txt` and similar files.
 *
 * If running in a Steam Runtime container or chroot, this function
 * returns `/` to indicate that the entire container is the Steam Runtime.
 *
 * This will typically be below
 * srt_system_info_dup_steam_installation_path(), unless overridden.
 *
 * If the Steam Runtime has been disabled or could not be found, at
 * least one flag will be set in the result of
 * srt_system_info_get_runtime_issues() to indicate why.
 *
 * Returns: (transfer full) (type filename) (nullable): The absolute path
 *  to the Steam Runtime, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_runtime_path (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_runtime_cached (self);
  return g_strdup (self->runtime.path);
}

/**
 * srt_system_info_dup_runtime_version:
 * @self: The #SrtSystemInfo object
 *
 * Return the version number of the Steam Runtime
 * in use, for example `0.20190711.3`, or %NULL if it could not be
 * determined. This could either be the `LD_LIBRARY_PATH`-based Steam
 * Runtime, or a Steam Runtime container or chroot.
 *
 * If the Steam Runtime has been disabled or could not be found, or its
 * version number could not be read, then at least one flag will be set
 * in the result of srt_system_info_get_runtime_issues() to indicate why.
 *
 * Returns: (transfer full) (type utf8) (nullable): The version number of
 *  the Steam Runtime, or %NULL if it could not be determined.
 *  Free with g_free().
 */
gchar *
srt_system_info_dup_runtime_version (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  ensure_runtime_cached (self);
  return g_strdup (self->runtime.version);
}

/**
 * srt_system_info_set_helpers_path:
 * @self: The #SrtSystemInfo
 * @path: (nullable) (type filename) (transfer none): An absolute path
 *
 * Look for helper executables used to inspect the system state in @path,
 * instead of the normal installed location.
 *
 * If @path is %NULL, go back to using the installed location.
 */
void
srt_system_info_set_helpers_path (SrtSystemInfo *self,
                                  const gchar *path)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));

  forget_libraries (self);
  forget_graphics_results (self);
  forget_locales (self);
  free (self->helpers_path);
  self->helpers_path = g_strdup (path);
}

/**
 * srt_system_info_get_primary_multiarch_tuple:
 * @self: The #SrtSystemInfo
 *
 * Return the multiarch tuple set by
 * srt_system_info_set_primary_multiarch_tuple() if any,
 * or the multiarch tuple corresponding to the steam-runtime-tools
 * library itself.
 *
 * Returns: (type filename) (transfer none): a Debian-style multiarch
 *  tuple such as %SRT_ABI_I386 or %SRT_ABI_X86_64
 */
const char *
srt_system_info_get_primary_multiarch_tuple (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (self->primary_multiarch_tuple != 0)
    return g_quark_to_string (self->primary_multiarch_tuple);

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    /* This won't *work* but at least it's non-empty... */
    return "UNKNOWN";

  return _SRT_MULTIARCH;
}

/**
 * srt_system_info_set_primary_multiarch_tuple:
 * @self: The #SrtSystemInfo
 * @tuple: (nullable) (type filename) (transfer none): A Debian-style
 *  multiarch tuple
 *
 * Use helper executables prefixed with the given string in situations
 * where the architecture does not matter, such as checking locales.
 * This is mostly useful as a way to substitute a mock implementation
 * during regression tests.
 *
 * If @path is %NULL, go back to using the compiled-in default.
 */
void
srt_system_info_set_primary_multiarch_tuple (SrtSystemInfo *self,
                                             const gchar *tuple)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));

  forget_locales (self);
  self->primary_multiarch_tuple = g_quark_from_string (tuple);
}

/**
 * srt_system_info_get_locale_issues:
 * @self: The #SrtSystemInfo
 *
 * Check that the locale specified by environment variables, and some
 * other commonly-assumed locales, are available and suitable.
 *
 * Returns: A summary of issues found, or %SRT_LOCALE_ISSUES_NONE
 *  if no problems are detected
 */
SrtLocaleIssues
srt_system_info_get_locale_issues (SrtSystemInfo *self)
{
  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self),
                        SRT_LOCALE_ISSUES_INTERNAL_ERROR);

  if (!self->locales.have_issues)
    {
      SrtLocale *locale = NULL;

      self->locales.issues = SRT_LOCALE_ISSUES_NONE;

      locale = srt_system_info_check_locale (self, "", NULL);

      if (locale == NULL)
        self->locales.issues |= SRT_LOCALE_ISSUES_DEFAULT_MISSING;
      else if (!srt_locale_is_utf8 (locale))
        self->locales.issues |= SRT_LOCALE_ISSUES_DEFAULT_NOT_UTF8;

      g_clear_object (&locale);

      locale = srt_system_info_check_locale (self, "C.UTF-8", NULL);

      if (locale == NULL || !srt_locale_is_utf8 (locale))
        self->locales.issues |= SRT_LOCALE_ISSUES_C_UTF8_MISSING;

      g_clear_object (&locale);

      locale = srt_system_info_check_locale (self, "en_US.UTF-8", NULL);

      if (locale == NULL || !srt_locale_is_utf8 (locale))
        self->locales.issues |= SRT_LOCALE_ISSUES_EN_US_UTF8_MISSING;

      g_clear_object (&locale);

      self->locales.have_issues = TRUE;

      /* We currently only look for I18NDIR data in /usr/share/i18n (the
       * glibc default path), so these checks only look there too.
       *
       * If we discover that some distros use a different default, then
       * we should enhance this check to iterate through a search path.
       *
       * Please keep this in sync with pressure-vessel-locale-gen. */

      if (!g_file_test ("/usr/share/i18n/SUPPORTED", G_FILE_TEST_IS_REGULAR))
        self->locales.issues |= SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING;

      if (!g_file_test ("/usr/share/i18n/locales/en_US", G_FILE_TEST_IS_REGULAR))
        self->locales.issues |= SRT_LOCALE_ISSUES_I18N_LOCALES_EN_US_MISSING;
    }

  return self->locales.issues;
}

/**
 * srt_system_info_check_locale:
 * @self: The #SrtSystemInfo
 * @requested_name: The locale to request, for example `en_US.UTF-8`.
 *  This may be the empty string or %NULL to request the empty string
 *  as a locale, which uses environment variables like `$LC_ALL`.
 * @error: Used to return an error on failure
 *
 * Check whether the given locale can be set successfully.
 *
 * Returns: (transfer full) (nullable): A #SrtLocale object, or %NULL
 *  if the requested locale could not be set.
 *  Free with g_object_unref() if non-%NULL.
 */
SrtLocale *
srt_system_info_check_locale (SrtSystemInfo *self,
                              const char *requested_name,
                              GError **error)
{
  GQuark quark = 0;
  gpointer value = NULL;
  MaybeLocale *maybe;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (requested_name == NULL)
    quark = g_quark_from_string ("");
  else
    quark = g_quark_from_string (requested_name);

  if (self->locales.cached_locales == NULL)
    self->locales.cached_locales = g_hash_table_new_full (NULL, NULL, NULL,
                                                          maybe_locale_free);

  if (g_hash_table_lookup_extended (self->locales.cached_locales,
                                    GUINT_TO_POINTER (quark),
                                    NULL,
                                    &value))
    {
      maybe = value;
    }
  else
    {
      GError *local_error = NULL;
      SrtLocale *locale = NULL;

      locale = _srt_check_locale (self->helpers_path,
                                  srt_system_info_get_primary_multiarch_tuple (self),
                                  g_quark_to_string (quark),
                                  &local_error);

      if (locale != NULL)
        {
          maybe = maybe_locale_new_positive (locale);
          g_object_unref (locale);
        }
      else
        {
          maybe = maybe_locale_new_negative (local_error);
        }

      g_hash_table_replace (self->locales.cached_locales,
                            GUINT_TO_POINTER (quark),
                            maybe);
      g_clear_error (&local_error);
    }

  if (maybe->locale != NULL)
    {
      g_assert (SRT_IS_LOCALE (maybe->locale));
      g_assert (maybe->error == NULL);
      return g_object_ref (maybe->locale);
    }
  else
    {
      g_assert (maybe->error != NULL);
      g_set_error_literal (error,
                           maybe->error->domain,
                           maybe->error->code,
                           maybe->error->message);
      return NULL;
    }
}

/**
 * srt_system_info_set_test_flags:
 * @self: The #SrtSystemInfo
 * @flags: Flags altering behaviour, for use in automated tests
 *
 * Alter the behaviour of the #SrtSystemInfo to make automated tests
 * quicker or give better test coverage.
 *
 * This function should not be called in production code.
 */
void
srt_system_info_set_test_flags (SrtSystemInfo *self,
                                SrtTestFlags flags)
{
  g_return_if_fail (SRT_IS_SYSTEM_INFO (self));
  self->test_flags = flags;
}

/**
 * srt_system_info_list_egl_icds:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuples: (nullable) (array zero-terminated=1) (element-type utf8):
 *  A list of multiarch tuples like %SRT_ABI_I386, representing ABIs, or %NULL
 *  to not look in any ABI-specific locations. This is currently only used if
 *  running in a Flatpak environment.
 *
 * List the available EGL ICDs, using the same search paths as GLVND.
 *
 * This function is not architecture-specific and may return a mixture
 * of ICDs for more than one architecture or ABI, because the way the
 * GLVND EGL loader works is to read a single search path for metadata
 * describing ICDs, then filter out the ones that are for the wrong
 * architecture at load time.
 *
 * Some of the entries in the result might describe a bare SONAME in the
 * standard library search path, which might exist for any or all
 * architectures simultaneously (this is the most common approach for EGL).
 * Other entries might describe the relative or absolute path to a
 * specific library, which will only be usable for the architecture for
 * which it was compiled.
 *
 * @multiarch_tuples is used if running in a Flatpak environment, to
 * match the search paths used by the freedesktop.org runtime's patched
 * GLVND.
 *
 * Returns: (transfer full) (element-type SrtEglIcd): A list of
 *  opaque #SrtEglIcd objects. Free with
 *  `g_list_free_full(icds, srt_egl_icd_unref)`.
 */
GList *
srt_system_info_list_egl_icds (SrtSystemInfo *self,
                               const char * const *multiarch_tuples)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->icds.have_egl)
    {
      g_assert (self->icds.egl == NULL);
      self->icds.egl = _srt_load_egl_icds (self->env, multiarch_tuples);
      self->icds.have_egl = TRUE;
    }

  for (iter = self->icds.egl; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}

/**
 * srt_system_info_list_vulkan_icds:
 * @self: The #SrtSystemInfo object
 * @multiarch_tuples: (nullable) (array zero-terminated=1) (element-type utf8):
 *  A list of multiarch tuples like %SRT_ABI_I386, representing ABIs, or %NULL
 *  to not look in any ABI-specific locations. This is currently only used if
 *  running in a Flatpak environment.
 *
 * List the available Vulkan ICDs, using the same search paths as the
 * reference vulkan-loader.
 *
 * This function is not architecture-specific and may return a mixture
 * of ICDs for more than one architecture or ABI, because the way the
 * reference vulkan-loader works is to read a single search path for
 * metadata describing ICDs, then filter out the ones that are for the
 * wrong architecture at load time.
 *
 * Some of the entries in the result might describe a bare SONAME in the
 * standard library search path, which might exist for any or all
 * architectures simultaneously (for example, this approach is used for
 * the NVIDIA binary driver on Debian systems). Other entries might
 * describe the relative or absolute path to a specific library, which
 * will only be usable for the architecture for which it was compiled
 * (for example, this approach is used in Mesa).
 *
 * @multiarch_tuples is used if running in a Flatpak environment, to
 * match the search paths used by the freedesktop.org runtime's patched
 * vulkan-loader.
 *
 * Returns: (transfer full) (element-type SrtVulkanIcd): A list of
 *  opaque #SrtVulkanIcd objects. Free with
 *  `g_list_free_full(icds, srt_vulkan_icd_unref)`.
 */
GList *
srt_system_info_list_vulkan_icds (SrtSystemInfo *self,
                                  const char * const *multiarch_tuples)
{
  GList *ret = NULL;
  const GList *iter;

  g_return_val_if_fail (SRT_IS_SYSTEM_INFO (self), NULL);

  if (!self->icds.have_vulkan)
    {
      g_assert (self->icds.vulkan == NULL);
      self->icds.vulkan = _srt_load_vulkan_icds (self->env, multiarch_tuples);
      self->icds.have_vulkan = TRUE;
    }

  for (iter = self->icds.vulkan; iter != NULL; iter = iter->next)
    ret = g_list_prepend (ret, g_object_ref (iter->data));

  return g_list_reverse (ret);
}
