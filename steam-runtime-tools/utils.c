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
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
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

#define MULTIARCH_LIBDIR \
  "/lib/" _SRT_MULTIARCH
#define RELOCATABLE_PKGLIBDIR \
  MULTIARCH_LIBDIR "/steam-runtime-tools-" _SRT_API_MAJOR

/**
 * _srt_process_timeout_wait_status:
 *
 * @wait_status: The wait_status from g_spawn_sync to process
 * @exit_status: (not optional): The exit_status to populate
 * @terminating_signal: (not optional): The terminating signal if any, 0 otherwise
 *
 * Check given wait_status and populate given exit_status and terminating_signal
 *
 * Returns: True if timeout, false otherwise
 */
G_GNUC_INTERNAL gboolean
_srt_process_timeout_wait_status (int wait_status, int *exit_status, int *terminating_signal)
{
  gboolean timed_out = FALSE;

  g_return_val_if_fail (exit_status != NULL, FALSE);
  g_return_val_if_fail (terminating_signal != NULL, FALSE);

  *exit_status = -1;
  *terminating_signal = 0;

  if (WIFEXITED (wait_status))
    {
      *exit_status = WEXITSTATUS (wait_status);

      if (*exit_status > 128 && *exit_status <= 128 + SIGRTMAX)
        {
          g_debug ("-> killed by signal %d", (*exit_status - 128));
          *terminating_signal = (*exit_status - 128);
        }
      else if (*exit_status == 124)
        {
          g_debug ("-> timed out");
          timed_out = TRUE;
        }
    }
  else if (WIFSIGNALED (wait_status))
    {
      g_debug ("-> timeout killed by signal %d", WTERMSIG (wait_status));
      *terminating_signal = WTERMSIG (wait_status);
    }
  else
    {
      g_critical ("Somehow got a wait_status that was neither exited nor signaled");
      g_return_val_if_reached (FALSE);
    }

  return timed_out;
}

G_GNUC_INTERNAL const char *
_srt_find_myself (const char **helpers_path_out,
                  GError **error)
{
  static gchar *saved_prefix = NULL;
  static gchar *saved_helpers_path = NULL;
  Dl_info ignored;
  struct link_map *map = NULL;
  gchar *dir = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (helpers_path_out == NULL || *helpers_path_out == NULL,
                        NULL);

  if (saved_prefix != NULL && saved_helpers_path != NULL)
    goto out;

  if (dladdr1 (_srt_find_myself, &ignored, (void **) &map,
               RTLD_DL_LINKMAP) == 0 ||
      map == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to locate shared library containing "
                   "_srt_find_myself()");
      goto out;
    }

  g_debug ("Found _srt_find_myself() in %s", map->l_name);
  dir = g_path_get_dirname (map->l_name);

  if (g_str_has_suffix (dir, RELOCATABLE_PKGLIBDIR))
    dir[strlen (dir) - strlen (RELOCATABLE_PKGLIBDIR)] = '\0';
  else if (g_str_has_suffix (dir, MULTIARCH_LIBDIR))
    dir[strlen (dir) - strlen (MULTIARCH_LIBDIR)] = '\0';
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

  saved_prefix = g_steal_pointer (&dir);
  /* deliberate one-per-process leak */
  saved_helpers_path = g_build_filename (
      saved_prefix, "libexec", "steam-runtime-tools-" _SRT_API_MAJOR,
      NULL);

out:
  if (helpers_path_out != NULL)
    *helpers_path_out = saved_helpers_path;

  g_free (dir);
  return saved_prefix;
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

  if (helpers_path == NULL)
    helpers_path = g_getenv ("SRT_HELPERS_PATH");

  if (helpers_path == NULL
      && _srt_find_myself (&helpers_path, error) == NULL)
    {
      g_ptr_array_unref (argv);
      return NULL;
    }

  /* Prefer a helper from ${SRT_HELPERS_PATH} or
   * ${libexecdir}/steam-runtime-tools-${_SRT_API_MAJOR}
   * if it exists */
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

  if ((flags & SRT_HELPER_FLAGS_SEARCH_PATH) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "%s not found", path);
      g_free (path);
      g_ptr_array_unref (argv);
      return NULL;
    }

  /* For helpers that are not part of steam-runtime-tools, such as
   * *-wflinfo and *-vulkaninfo, we fall back to searching $PATH */
  g_free (path);

  if (multiarch == NULL)
    prefixed = g_strdup (base);
  else
    prefixed = g_strdup_printf ("%s-%s", multiarch, base);

  g_ptr_array_add (argv, g_steal_pointer (&prefixed));
  return argv;
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

/**
 * srt_enum_value_to_nick
 * @enum_type: The type of the enumeration.
 * @value: The enumeration value to stringify.
 *
 * Get the #GEnumValue.value-nick of a given enumeration value.
 * For example, `srt_enum_value_to_nick (SRT_TYPE_WINDOW_SYSTEM, SRT_WINDOW_SYSTEM_EGL_X11)`
 * returns `"egl-x11"`.
 *
 * Returns: (transfer none): A string representation
 *  of the given enumeration value.
 */
const char *
srt_enum_value_to_nick (GType enum_type,
                        int value)
{
  GEnumClass *class;
  GEnumValue *enum_value;
  const char *result;

  g_return_val_if_fail (G_TYPE_IS_ENUM (enum_type), NULL);

  class = g_type_class_ref (enum_type);
  enum_value = g_enum_get_value (class, value);

  if (enum_value != NULL)
    result = enum_value->value_nick;
  else
    result = NULL;

  g_type_class_unref (class);
  return result;
}

static void _srt_constructor (void) __attribute__((__constructor__));
static void
_srt_constructor (void)
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
  g_type_init ();
#endif
  g_return_if_fail (_srt_check_not_setuid ());
}

static const int signals_blocked_by_steam[] =
  {
    SIGALRM,
    SIGCHLD,
    SIGPIPE,
    SIGTRAP
  };

/*
 * _srt_child_setup_unblock_signals:
 * @ignored: Ignored, for compatibility with #GSpawnChildSetupFunc
 *
 * A child-setup function that unblocks all signals, and resets signals
 * known to be altered by the Steam client to their default dispositions.
 *
 * In particular, this can be used to work around versions of `timeout(1)`
 * that do not do configure `SIGCHLD` to make sure they receive it
 * (GNU coreutils >= 8.27, < 8.29 as seen in Ubuntu 18.04).
 *
 * This function is async-signal-safe.
 */
void
_srt_child_setup_unblock_signals (gpointer ignored)
{
  struct sigaction action = { .sa_handler = SIG_DFL };
  sigset_t new_set;
  gsize i;

  /* We ignore errors and don't even g_debug(), to avoid being
   * async-signal-unsafe */
  sigemptyset (&new_set);
  (void) pthread_sigmask (SIG_SETMASK, &new_set, NULL);

  for (i = 0; i < G_N_ELEMENTS (signals_blocked_by_steam); i++)
    (void) sigaction (signals_blocked_by_steam[i], &action, NULL);
}

/*
 * _srt_unblock_signals:
 *
 * Unblock all signals, and reset signals known to be altered by the
 * Steam client to their default dispositions.
 *
 * This function is not async-signal-safe.
 */
void
_srt_unblock_signals (void)
{
  struct sigaction old_action = { .sa_handler = SIG_DFL };
  struct sigaction new_action = { .sa_handler = SIG_DFL };
  sigset_t old_set;
  sigset_t new_set;
  gsize i;
  int sig;
  int saved_errno;

  sigemptyset (&old_set);
  sigfillset (&new_set);

  /* This returns an errno code instead of setting errno */
  saved_errno = pthread_sigmask (SIG_UNBLOCK, &new_set, &old_set);

  if (saved_errno != 0)
    {
      g_warning ("Unable to unblock signals: %s", g_strerror (saved_errno));
    }
  else
    {
      for (sig = 1; sig < 64; sig++)
        {
          /* sigismember returns -1 for non-signals, which we ignore */
          if (sigismember (&new_set, sig) == 1 &&
              sigismember (&old_set, sig) == 1)
            g_debug ("Unblocked signal %d (%s)", sig, g_strsignal (sig));
        }
    }

  for (i = 0; i < G_N_ELEMENTS (signals_blocked_by_steam); i++)
    {
      sig = signals_blocked_by_steam[i];

      if (sigaction (sig, &new_action, &old_action) != 0)
        {
          saved_errno = errno;
          g_warning ("Unable to reset handler for signal %d (%s): %s",
                     sig, g_strsignal (sig), g_strerror (saved_errno));
        }
      else if (old_action.sa_handler != SIG_DFL)
        {
          g_debug ("Reset signal %d (%s) from handler %p to SIG_DFL",
                   sig, g_strsignal (sig), old_action.sa_handler);
        }
    }
}
