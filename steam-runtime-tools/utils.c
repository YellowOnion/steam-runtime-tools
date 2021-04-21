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
#include <ftw.h>
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
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

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
#define PKGLIBEXECDIR \
  "/libexec/steam-runtime-tools-" _SRT_API_MAJOR
#define INSTALLED_TESTS_PKGLIBEXECDIR \
  "/libexec/installed-tests/steam-runtime-tools-" _SRT_API_MAJOR

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

  if (map->l_name == NULL || map->l_name[0] == '\0')
    {
      char *exe = realpath ("/proc/self/exe", NULL);

      if (exe == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Unable to locate main executable");
          goto out;
        }

      g_debug ("Found _srt_find_myself() in main executable %s", exe);
      dir = g_path_get_dirname (exe);
      free (exe);
    }
  else
    {
      g_debug ("Found _srt_find_myself() in %s", map->l_name);
      dir = g_path_get_dirname (map->l_name);
    }

  if (g_str_has_suffix (dir, RELOCATABLE_PKGLIBDIR))
    dir[strlen (dir) - strlen (RELOCATABLE_PKGLIBDIR)] = '\0';
  else if (g_str_has_suffix (dir, MULTIARCH_LIBDIR))
    dir[strlen (dir) - strlen (MULTIARCH_LIBDIR)] = '\0';
  else if (g_str_has_suffix (dir, PKGLIBEXECDIR))
    dir[strlen (dir) - strlen (PKGLIBEXECDIR)] = '\0';
  else if (g_str_has_suffix (dir, INSTALLED_TESTS_PKGLIBEXECDIR))
    dir[strlen (dir) - strlen (INSTALLED_TESTS_PKGLIBEXECDIR)] = '\0';
  else if (g_str_has_suffix (dir, "/libexec"))
    dir[strlen (dir) - strlen ("/libexec")] = '\0';
  else if (g_str_has_suffix (dir, "/lib64"))
    dir[strlen (dir) - strlen ("/lib64")] = '\0';
  else if (g_str_has_suffix (dir, "/lib"))
    dir[strlen (dir) - strlen ("/lib")] = '\0';
  else if (g_str_has_suffix (dir, "/bin"))
    dir[strlen (dir) - strlen ("/bin")] = '\0';

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

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
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
   * *-wflinfo, we fall back to searching $PATH */
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
 * _srt_filter_gameoverlayrenderer_from_envp:
 * @envp: (array zero-terminated=1) (not nullable):
 *
 * Filter every path containing `gameoverlayrenderer.so` from the environment
 * variable `LD_PRELOAD` in the provided @envp.
 *
 * Returns (array zero-terminated=1) (transfer full): A newly-allocated array of
 *  strings containing all the values from @envp, but with
 *  `gameoverlayrenderer.so` filtered out. Free with g_strfreev().
 */
gchar **
_srt_filter_gameoverlayrenderer_from_envp (gchar **envp)
{
  GStrv filtered_environ = NULL;
  const gchar *ld_preload;
  g_autofree gchar *filtered_preload = NULL;

  g_return_val_if_fail (envp != NULL, NULL);

  filtered_environ = g_strdupv (envp);
  ld_preload = g_environ_getenv (filtered_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      filtered_environ = g_environ_setenv (filtered_environ, "LD_PRELOAD",
                                           filtered_preload, TRUE);
    }

  return filtered_environ;
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

/**
 * srt_enum_from_nick:
 * @enum_type: The type of the enumeration
 * @nick: The nickname to look up
 * @value_out: (not nullable): Used to return the enumeration that has been
 *  found from the provided @nick
 * @error: Used to raise an error on failure
 *
 * Get the enumeration from a given #GEnumValue.value-nick.
 * For example:
 * `srt_enum_from_nick (SRT_TYPE_GRAPHICS_LIBRARY_VENDOR, "glvnd", (gint *) &library_vendor, NULL)`
 * will set `library_vendor` to SRT_GRAPHICS_LIBRARY_VENDOR_GLVND.
 *
 * Returns: %TRUE if no errors have been found.
 */
gboolean
srt_enum_from_nick (GType enum_type,
                    const gchar *nick,
                    gint *value_out,
                    GError **error)
{
  GEnumClass *class;
  GEnumValue *enum_value;
  gboolean result = TRUE;

  g_return_val_if_fail (G_TYPE_IS_ENUM (enum_type), FALSE);
  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (value_out != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  class = g_type_class_ref (enum_type);

  enum_value = g_enum_get_value_by_nick (class, nick);
  if (enum_value)
    {
      *value_out = enum_value->value;
    }
  else
    {
      if (error != NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "\"%s\" is not a known member of %s",
                     nick, g_type_name (enum_type));
      result = FALSE;
    }

  g_type_class_unref (class);
  return result;
}

/**
 * srt_add_flag_from_nick:
 * @flags_type: The type of the flag
 * @nick: The nickname to look up
 * @value_out: (not nullable) (inout): The flag, from the provided @nick,
 *  will be added to @value_out
 * @error: Used to raise an error on failure
 *
 * Get the flag from a given #GEnumValue.value-nick.
 * For example:
 * `srt_add_flag_from_nick (SRT_TYPE_STEAM_ISSUES, "cannot-find", &issues, error)`
 * will add SRT_STEAM_ISSUES_CANNOT_FIND to `issues`.
 *
 * Returns: %TRUE if no errors have been found.
 */
gboolean
srt_add_flag_from_nick (GType flags_type,
                        const gchar *nick,
                        guint *value_out,
                        GError **error)
{
  GFlagsClass *class;
  GFlagsValue *flags_value;
  gboolean result = TRUE;

  g_return_val_if_fail (G_TYPE_IS_FLAGS (flags_type), FALSE);
  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (value_out != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  class = g_type_class_ref (flags_type);

  flags_value = g_flags_get_value_by_nick (class, nick);
  if (flags_value)
    {
      *value_out |= flags_value->value;
    }
  else
    {
      if (error != NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "\"%s\" is not a known member of %s",
                     nick, g_type_name (flags_type));
      result = FALSE;
    }

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

/*
 * _srt_indirect_strcmp0:
 * @left: A non-%NULL pointer to a (possibly %NULL) `const char *`
 * @right: A non-%NULL pointer to a (possibly %NULL) `const char *`
 *
 * A #GCompareFunc to sort pointers to strings in lexicographic
 * (g_strcmp0()) order.
 *
 * Returns: An integer < 0 if left < right, > 0 if left > right,
 *  or 0 if left == right or if they are not comparable
 */
int
_srt_indirect_strcmp0 (gconstpointer left,
                       gconstpointer right)
{
  const gchar * const *l = left;
  const gchar * const *r = right;

  g_return_val_if_fail (l != NULL, 0);
  g_return_val_if_fail (r != NULL, 0);
  return g_strcmp0 (*l, *r);
}

static gint
ftw_remove (const gchar *path,
            const struct stat *sb,
            gint typeflags,
            struct FTW *ftwbuf)
{
  if (remove (path) < 0)
    {
      g_debug ("Unable to remove %s: %s", path, g_strerror (errno));
      return -1;
    }

  return 0;
}

/**
 * _srt_rm_rf:
 * @directory: (type filename): The directory to remove.
 *
 * Recursively delete @directory within the same file system and
 * without following symbolic links.
 *
 * Returns: %TRUE if the removal was successful
 */
gboolean
_srt_rm_rf (const char *directory)
{
  g_return_val_if_fail (directory != NULL, FALSE);

  if (nftw (directory, ftw_remove, 10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0)
    return FALSE;

  return TRUE;
}

/*
 * _srt_divert_stdout_to_stderr:
 * @error: Used to raise an error on failure
 *
 * Duplicate file descriptors so that functions that would write to
 * `stdout` instead write to a copy of the original `stderr`. Return
 * a file handle that can be used to print structured output to the
 * original `stdout`.
 *
 * Returns: (transfer full): A libc file handle for the original `stdout`,
 *  or %NULL on error. Free with `fclose()`.
 */
FILE *
_srt_divert_stdout_to_stderr (GError **error)
{
  g_autoptr(FILE) original_stdout = NULL;
  glnx_autofd int original_stdout_fd = -1;
  int flags;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Duplicate the original stdout so that we still have a way to write
   * machine-readable output. */
  original_stdout_fd = dup (STDOUT_FILENO);

  if (original_stdout_fd < 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to duplicate fd %d",
                                         STDOUT_FILENO);

  flags = fcntl (original_stdout_fd, F_GETFD, 0);

  if (flags < 0)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to get flags of new fd");

  fcntl (original_stdout_fd, F_SETFD, flags|FD_CLOEXEC);

  /* If something like g_debug writes to stdout, make it come out of
   * our original stderr. */
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to make fd %d a copy of fd %d",
                                         STDOUT_FILENO, STDERR_FILENO);

  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    return glnx_null_throw_errno_prefix (error,
                                         "Unable to create a stdio wrapper for fd %d",
                                         original_stdout_fd);
  else
    original_stdout_fd = -1;    /* ownership taken, do not close */

  return g_steal_pointer (&original_stdout);
}

/*
 * _srt_file_get_contents_in_sysroot:
 * @sysroot_fd: A directory fd opened on the sysroot or `/`
 * @path: Absolute or root-relative path within @sysroot_fd
 * @contents: (out) (not optional): Used to return contents
 * @len: (out) (optional): Used to return length
 * @error: Used to return error on failure
 *
 * Like g_file_get_contents(), but the file is in a sysroot, and we
 * follow symlinks as though @sysroot_fd was the root directory
 * (similar to `fakechroot`).
 *
 * Returns: %TRUE if successful
 */
gboolean
_srt_file_get_contents_in_sysroot (int sysroot_fd,
                                   const char *path,
                                   gchar **contents,
                                   gsize *len,
                                   GError **error)
{
  g_autofree gchar *real_path = NULL;
  g_autofree gchar *fd_path = NULL;
  g_autofree gchar *ignored = NULL;
  glnx_autofd int fd = -1;

  g_return_val_if_fail (sysroot_fd >= 0, FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (contents != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (contents == NULL)
    contents = &ignored;

  fd = _srt_resolve_in_sysroot (sysroot_fd,
                                path,
                                SRT_RESOLVE_FLAGS_READABLE,
                                &real_path,
                                error);

  if (fd < 0)
    return FALSE;

  fd_path = g_strdup_printf ("/proc/self/fd/%d", fd);

  if (!g_file_get_contents (fd_path, contents, len, error))
    {
      g_prefix_error (error, "Unable to read %s: ", real_path);
      return FALSE;
    }

  return TRUE;
}

/*
 * _srt_file_test_in_sysroot:
 * @sysroot: (type filename): A path used as the root
 * @sysroot_fd: A file descriptor opened on @sysroot, or negative to
 *  reopen it
 * @filename: (type filename): A path below the root directory, either
 *  absolute or relative (to the root)
 * @test: The test to perform on the resolved file in @sysroot.
 *  G_FILE_TEST_IS_SYMLINK is not a valid GFileTest value because the
 *  path is resolved following symlinks too.
 *
 * Returns: %TRUE if the @filename resolved in @sysroot passes the @test.
 */
gboolean
_srt_file_test_in_sysroot (const char *sysroot,
                           int sysroot_fd,
                           const char *filename,
                           GFileTest test)
{
  glnx_autofd int file_fd = -1;
  glnx_autofd int local_sysroot_fd = -1;
  struct stat stat_buf;
  g_autofree gchar *file_realpath_in_sysroot = NULL;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail (sysroot != NULL, FALSE);
  g_return_val_if_fail (filename != NULL, FALSE);
  /* We reject G_FILE_TEST_IS_SYMLINK because the provided filename is resolved
   * in sysroot, following the eventual symlinks too. So it is not possible for
   * the resolved filename to be a symlink */
  g_return_val_if_fail ((test & (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE
                                 | G_FILE_TEST_IS_REGULAR
                                 | G_FILE_TEST_IS_DIR)) == test, FALSE);

  if (sysroot_fd < 0)
    {
      if (!glnx_opendirat (-1, sysroot, FALSE, &local_sysroot_fd, &error))
        {
          g_debug ("An error occurred trying to open sysroot \"%s\": %s",
                   sysroot, error->message);
          return FALSE;
        }

      sysroot_fd = local_sysroot_fd;
    }

  file_fd = _srt_resolve_in_sysroot (sysroot_fd,
                                     filename, SRT_RESOLVE_FLAGS_NONE,
                                     &file_realpath_in_sysroot, &error);

  if (file_fd < 0)
    {
      g_debug ("An error occurred trying to resolve \"%s\" in sysroot \"%s\": %s",
               filename, sysroot, error->message);
      return FALSE;
    }

  if (fstat (file_fd, &stat_buf) != 0)
    {
      g_debug ("fstat %s/%s: %s",
               sysroot, file_realpath_in_sysroot, g_strerror (errno));
      return FALSE;
    }

  if (test & G_FILE_TEST_EXISTS)
    return TRUE;

  if ((test & G_FILE_TEST_IS_EXECUTABLE) && (stat_buf.st_mode & 0111))
    return TRUE;

  if ((test & G_FILE_TEST_IS_REGULAR) && S_ISREG (stat_buf.st_mode))
    return TRUE;

  if ((test & G_FILE_TEST_IS_DIR) && S_ISDIR (stat_buf.st_mode))
    return TRUE;

  return FALSE;
}

/*
 * Return a pointer to the environment block, without copying.
 * In the unlikely event that `environ == NULL`, return a pointer to
 * an empty #GStrv.
 */
const char * const *
_srt_peek_environ_nonnull (void)
{
  static const char * const no_strings[] = { NULL };

  g_return_val_if_fail (_srt_check_not_setuid (), no_strings);

  if (environ != NULL)
    return (const char * const *) environ;
  else
    return no_strings;
}

/*
 * Globally disable GIO modules.
 *
 * This function modifies the environment, therefore:
 *
 * - it must be called from main() before starting any threads
 * - you must save a copy of the original environment first if you intend
 *   for subprocesses to receive the original, unmodified environment
 *
 * To be effective, it must also be called before any use of GIO APIs.
 */
void
_srt_setenv_disable_gio_modules (void)
{
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  g_setenv ("GIO_MODULE_DIR", "/nonexistent", TRUE);
}

/*
 * Return TRUE if @str points to one or more decimal digits, followed
 * by nul termination. This is the same as Python bytes.isdigit().
 */
gboolean
_srt_str_is_integer (const char *str)
{
  const char *p;

  g_return_val_if_fail (str != NULL, FALSE);

  if (*str == '\0')
    return FALSE;

  for (p = str;
       *p != '\0';
       p++)
    {
      if (!g_ascii_isdigit (*p))
        break;
    }

  return (*p == '\0');
}

/*
 * _srt_fstatat_is_same_file:
 * @afd: a file descriptor, `AT_FDCWD` or -1
 * @a: a path
 * @bfd: a file descriptor, `AT_FDCWD` or -1
 * @b: a path
 *
 * Returns: %TRUE if a (relative to afd) and b (relative to bfd)
 *  are names for the same inode.
 */
gboolean
_srt_fstatat_is_same_file (int afd,
                           const char *a,
                           int bfd,
                           const char *b)
{
  struct stat a_buffer, b_buffer;

  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (b != NULL, FALSE);

  afd = glnx_dirfd_canonicalize (afd);
  bfd = glnx_dirfd_canonicalize (bfd);

  if (afd == bfd && strcmp (a, b) == 0)
    return TRUE;

  return (fstatat (afd, a, &a_buffer, AT_EMPTY_PATH) == 0
          && fstatat (bfd, b, &b_buffer, AT_EMPTY_PATH) == 0
          && _srt_is_same_stat (&a_buffer, &b_buffer));
}

/*
 * _srt_steam_command_via_pipe:
 * @arguments: (not nullable) (element-type utf8) (transfer none): An array
 *  of command-line arguments that need to be passed to the Steam pipe
 * @n_arguments: Number of elements of @arguments to use, or negative if
 *  @arguments is %NULL-terminated
 * @error: Used to raise an error on failure
 *
 * If @n_arguments is positive it's the caller's responsibility to ensure that
 * @arguments has at least @n_arguments elements.
 * If @n_arguments is negative, @arguments must be %NULL-terminated.
 *
 * Returns: %TRUE if the provided @arguments were successfully passed to the
 *  running Steam client instance
 */
gboolean
_srt_steam_command_via_pipe (const char * const *arguments,
                             gssize n_arguments,
                             GError **error)
{
  glnx_autofd int fd = -1;
  int ofd_flags;
  g_autofree gchar *steampipe = NULL;
  g_autoptr(GString) args_string = g_string_new ("");
  gsize length;
  gsize i;

  g_return_val_if_fail (arguments != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (n_arguments >= 0)
    length = n_arguments;
  else
    length = g_strv_length ((gchar **) arguments);

  steampipe = g_build_filename (g_get_home_dir (), ".steam", "steam.pipe", NULL);

  fd = open (steampipe, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);

  if (fd < 0 && (errno == ENOENT || errno == ENXIO))
    return glnx_throw_errno_prefix (error, "Steam is not running");
  else if (fd < 0)
    return glnx_throw_errno_prefix (error, "An error occurred trying to open the Steam pipe");

  ofd_flags = fcntl (fd, F_GETFL, 0);

  /* Remove O_NONBLOCK to block if we write more than the pipe-buffer space */
  if (fcntl (fd, F_SETFL, ofd_flags & ~O_NONBLOCK) != 0)
    return glnx_throw_errno_prefix (error, "Unable to set flags on the steam pipe fd");

  /* We hardcode the canonical steam installation path, instead of actually
   * searching where Steam has been installed, because apparently this
   * information is not used for anything in particular and Steam just
   * discards it */
  g_string_append (args_string, "'~/.steam/root/ubuntu12_32/steam'");

  for (i = 0; i < length; i++)
    {
      g_autofree gchar *quoted = NULL;

      quoted = g_shell_quote (arguments[i]);

      g_string_append (args_string, " ");
      g_string_append (args_string, quoted);
    }

  g_string_append (args_string, "\n");

  if (glnx_loop_write (fd, args_string->str, args_string->len) < 0)
    return glnx_throw_errno_prefix (error,
                                    "An error occurred trying to write to the Steam pipe");

  return TRUE;
}
