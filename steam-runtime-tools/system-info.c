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
  gchar *expectations;
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
  GQuark multiarch_tuple;
  Tristate can_run;
  GHashTable *cached_results;
  SrtLibraryIssues cached_combined_issues;
  gboolean libraries_cache_available;
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

  g_slice_free (Abi, self);
}

static void
srt_system_info_init (SrtSystemInfo *self)
{
  self->can_write_uinput = TRI_MAYBE;

  /* Assume that in practice we will usually add two ABIs: amd64 and i386 */
  self->abis = g_ptr_array_new_full (2, abi_free);
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

static void
srt_system_info_finalize (GObject *object)
{
  SrtSystemInfo *self = SRT_SYSTEM_INFO (object);

  g_clear_pointer (&self->abis, g_ptr_array_unref);
  g_free (self->expectations);

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
      if (_srt_architecture_can_run (multiarch_tuple))
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
                * `srt_check_library_presence` with the symbols file where we
                * found it, as an argument. */
              SrtLibrary *library = NULL;
              char *soname = g_strdup (strsep (&pointer_into_line, " \t"));
              abi->cached_combined_issues |= srt_check_library_presence (soname,
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
  GDir *dir;
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


  dir_path = g_build_filename (self->expectations, multiarch_tuple, NULL);
  dir = g_dir_open (dir_path, 0, &error);
  if (error)
    {
      g_debug ("An error occurred while opening the symbols directory: %s", error->message);
      g_clear_error (&error);
      goto out;
    }
  while ((filename = g_dir_read_name (dir)))
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
                  issues = srt_check_library_presence (soname_found,
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
  issues = srt_check_library_presence (soname,
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
