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

#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_SYS_AUXV_H
#include <sys/auxv.h>
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "steam-runtime-tools/glib-compat.h"

#ifdef HAVE_GETAUXVAL
#define getauxval_AT_SECURE() getauxval (AT_SECURE)
#else
/*
 * This implementation assumes that auxv entries are pointer-sized on
 * all architectures.
 *
 * Note that this implementation doesn't special-case AT_HWCAP and
 * AT_HWCAP like glibc does, so it is only suitable for other types
 * (but in practice we only need AT_SECURE here).
 */
static long
getauxval_AT_SECURE (void)
{
  uintptr_t buf[2] = { 0 /* type */, 0 /* value */ };
  FILE *auxv;
  gboolean found = FALSE;

  if ((auxv = fopen("/proc/self/auxv", "r")) == NULL)
    return 0;

  while ((fread (buf, sizeof (buf), 1, auxv)) == 1)
    {
      if (buf[0] == AT_SECURE)
        {
          found = TRUE;
          break;
        }
      else
        {
          buf[0] = buf[1] = 0;
        }
    }

  fclose(auxv);

  if (!found)
    errno = ENOENT;

  return (long) buf[1];
}
#endif

/* Return TRUE if setuid, setgid, setcap or otherwise running with
 * elevated privileges. "setuid" in the name is shorthand for this. */
static gboolean
check_for_setuid_once (void)
{
  errno = 0;

  /* If the kernel says we are running with elevated privileges,
   * believe it */
  if (getauxval_AT_SECURE ())
    return TRUE;

  /* If the kernel specifically told us we are not running with
   * elevated privileges, believe it (as opposed to the kernel not
   * having told us either way, which sets errno to ENOENT) */
  if (errno == 0)
    return FALSE;

  /* Otherwise resort to comparing (e)uid and (e)gid */
  if (geteuid () != getuid ())
    return TRUE;

  if (getegid () != getgid ())
    return TRUE;

  return FALSE;
}

static int is_setuid = -1;

/*
 * _srt_check_not_setuid:
 *
 * Check that the process containing this library is not setuid, setgid,
 * setcap or otherwise running with elevated privileges. The word
 * "setuid" in the function name is not completely accurate, but is used
 * as a shorthand term since it is the most common way for a process
 * to be more privileged than its parent.
 *
 * This library trusts environment variables and other aspects of the
 * execution environment, and is not designed to be used with elevated
 * privileges, so this should normally be done as a precondition check:
 *
 * |[<!-- language="C" -->
 * g_return_if_fail (_srt_check_not_setuid ());
 * // or in functions that return a value
 * g_return_val_if_fail (_srt_check_not_setuid (), SOME_ERROR_CONSTANT);
 * ]|
 *
 * Returns: %TRUE under normal circumstances
 */
G_GNUC_INTERNAL gboolean
_srt_check_not_setuid (void)
{
  if (is_setuid >= 0)
    return !is_setuid;

  is_setuid = check_for_setuid_once ();
  return !is_setuid;
}

static gchar *global_helpers_path = NULL;

static const char *
_srt_get_helpers_path (GError **error)
{
  const char *path;
  Dl_info ignored;
  struct link_map *map = NULL;
  gchar *dir;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  path = global_helpers_path;

  if (path != NULL)
    goto out;

  path = g_getenv ("SRT_HELPERS_PATH");

  if (path != NULL)
    goto out;

  if (dladdr1 (_srt_get_helpers_path, &ignored, (void **) &map,
               RTLD_DL_LINKMAP) == 0 ||
      map == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to locate shared library containing "
                   "_srt_get_helpers_path()");
      goto out;
    }

  g_debug ("Found _srt_get_helpers_path() in %s", map->l_name);
  dir = g_path_get_dirname (map->l_name);

  if (g_str_has_suffix (dir, "/lib/" _SRT_MULTIARCH))
    dir[strlen (dir) - strlen ("/lib/" _SRT_MULTIARCH)] = '\0';
  else if (g_str_has_suffix (dir, "/lib64"))
    dir[strlen (dir) - strlen ("/lib64")] = '\0';
  else if (g_str_has_suffix (dir, "/lib"))
    dir[strlen (dir) - strlen ("/lib")] = '\0';

  /* If the library was found in /lib/MULTIARCH, /lib64 or /lib on a
   * merged-/usr system, assume --prefix=/usr (/libexec doesn't
   * normally exist) */
  if (dir[0] == '\0')
    {
      g_free (dir);
      dir = g_strdup ("/usr");
    }

  /* deliberate one-per-process leak */
  global_helpers_path = g_build_filename (
      dir, "libexec", "steam-runtime-tools-" _SRT_API_MAJOR,
      NULL);
  path = global_helpers_path;

  g_free (dir);

out:
  return path;
}

/*
 * _srt_get_helper:
 * @helpers_path: (nullable): Directory to search for helper executables,
 *  or %NULL for default behaviour
 * @multiarch: (nullable): A multiarch tuple like %SRT_ABI_I386 to prefix
 *  to the executable name, or %NULL
 * @base: (not nullable): Base name of the executable
 * @flags: Flags affecting how we set up the helper
 * @error: Used to raise an error if %NULL is returned
 *
 * Find a helper executable. We return an array of arguments so that the
 * helper can be wrapped by an "adverb" like `env`, `timeout` or a
 * specific `ld.so` implementation if required.
 *
 * Returns: (nullable) (element-type filename) (transfer container): The
 *  initial `argv` for the helper, with g_free() set as the free-function, and
 *  no %NULL terminator. Free with g_ptr_array_unref() or g_ptr_array_free().
 */
G_GNUC_INTERNAL GPtrArray *
_srt_get_helper (const char *helpers_path,
                 const char *multiarch,
                 const char *base,
                 SrtHelperFlags flags,
                 GError **error)
{
  GPtrArray *argv = NULL;
  gchar *path;
  gchar *prefixed;

  g_return_val_if_fail (base != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  argv = g_ptr_array_new_with_free_func (g_free);

  if (flags & SRT_HELPER_FLAGS_TIME_OUT)
    {
      g_ptr_array_add (argv, g_strdup ("timeout"));
      g_ptr_array_add (argv, g_strdup ("--signal=TERM"));

      if (flags & SRT_HELPER_FLAGS_TIME_OUT_SOONER)
        {
          /* Speed up the failing case in automated testing */
          g_ptr_array_add (argv, g_strdup ("--kill-after=1"));
          g_ptr_array_add (argv, g_strdup ("1"));
        }
      else
        {
          /* Kill the helper (if still running) 3 seconds after the TERM
           * signal */
          g_ptr_array_add (argv, g_strdup ("--kill-after=3"));
          /* Send TERM signal after 10 seconds */
          g_ptr_array_add (argv, g_strdup ("10"));
        }
    }

  if ((flags & SRT_HELPER_FLAGS_SEARCH_PATH) != 0
      && helpers_path == NULL)
    {
      /* For helpers that are not part of steam-runtime-tools, such as
       * *-wflinfo and *-vulkaninfo, we currently only search PATH,
       * unless helpers_path has been overridden */
      if (multiarch == NULL)
        prefixed = g_strdup (base);
      else
        prefixed = g_strdup_printf ("%s-%s", multiarch, base);

      g_ptr_array_add (argv, g_steal_pointer (&prefixed));
      return argv;
    }

  if (helpers_path == NULL)
    {
      helpers_path = _srt_get_helpers_path (error);

      if (helpers_path == NULL)
        {
          g_ptr_array_unref (argv);
          return NULL;
        }
    }

  path = g_strdup_printf ("%s/%s%s%s",
                          helpers_path,
                          multiarch == NULL ? "" : multiarch,
                          multiarch == NULL ? "" : "-",
                          base);

  g_debug ("Looking for %s", path);

  if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_ptr_array_add (argv, g_steal_pointer (&path));
      return argv;
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "%s not found", path);
      g_free (path);
      g_ptr_array_unref (argv);
      return NULL;
    }
}

/**
 * _srt_filter_gameoverlayrenderer:
 * @input: The environment variable value that needs to be filtered.
 *  Usually retrieved with g_environ_getenv ()
 *
 * Filter the @input paths list from every path containing `gameoverlayrenderer.so`
 *
 * Returns: A newly-allocated string containing all the paths from @input
 *  except for the ones with `gameoverlayrenderer.so`.
 *  Free with g_free ().
 */
gchar *
_srt_filter_gameoverlayrenderer (const gchar *input)
{
  gchar **entries;
  gchar **entry;
  gchar *ret = NULL;
  GPtrArray *filtered;

  g_return_val_if_fail (input != NULL, NULL);

  entries = g_strsplit (input, ":", 0);
  filtered = g_ptr_array_new ();

  for (entry = entries; entry != NULL && *entry != NULL; entry++)
    {
      if (!g_str_has_suffix (*entry, "/gameoverlayrenderer.so"))
        g_ptr_array_add (filtered, *entry);
    }

  g_ptr_array_add (filtered, NULL);
  ret = g_strjoinv (":", (gchar **) filtered->pdata);

  g_ptr_array_free (filtered, TRUE);
  g_strfreev (entries);

  return ret;
}

#if !GLIB_CHECK_VERSION(2, 36, 0)
static void _srt_constructor (void) __attribute__((__constructor__));
static void
_srt_constructor (void)
{
  g_type_init ();
  g_return_if_fail (_srt_check_not_setuid ());
}
#endif
