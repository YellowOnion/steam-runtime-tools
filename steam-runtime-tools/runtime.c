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

#include "steam-runtime-tools/runtime.h"
#include "steam-runtime-tools/runtime-internal.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-compat.h"

/**
* SECTION:runtime
* @title: LD_LIBRARY_PATH-based Steam Runtime
* @short_description: Information about the Steam Runtime
* @include: steam-runtime-tools/steam-runtime-tools.h
*
* #SrtRuntimeIssues represents problems encountered with the Steam
* Runtime.
*/

static void
should_be_executable (SrtRuntimeIssues *issues,
                      const char *path,
                      const char *filename)
{
  gchar *full = g_build_filename (path, filename, NULL);

  if (!g_file_test (full, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_debug ("%s is not executable", full);
      *issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }

  g_free (full);
}

static void
should_be_dir (SrtRuntimeIssues *issues,
               const char *path,
               const char *filename)
{
  gchar *full = g_build_filename (path, filename, NULL);

  if (!g_file_test (full, G_FILE_TEST_IS_DIR))
    {
      g_debug ("%s is not a regular file", full);
      *issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }

  g_free (full);
}

static void
should_be_stattable (SrtRuntimeIssues *issues,
                     const char *path,
                     const char *filename,
                     GStatBuf *buf)
{
  gchar *full = g_build_filename (path, filename, NULL);

  if (g_stat (full, buf) != 0)
    {
      g_debug ("stat %s: %s", full, g_strerror (errno));
      *issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
    }

  g_free (full);
}

static void
might_be_stattable (const char *path,
                    const char *filename,
                    GStatBuf *buf)
{
  GStatBuf zeroed_stat = {};
  gchar *full = g_build_filename (path, filename, NULL);

  if (g_stat (full, buf) != 0)
    {
      g_debug ("stat %s: %s", full, g_strerror (errno));
      *buf = zeroed_stat;
    }

  g_free (full);
}

static gboolean
same_stat (GStatBuf *left,
           GStatBuf *right)
{
  return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

/*
 * _srt_runtime_check:
 * @bin32: (nullable): The absolute path to `ubuntu12_32`
 * @expected_version: (nullable): The expected version number of the
 *  Steam Runtime
 * @custom_environ: (nullable): The list of environment variables to use. If NULL
 *  g_get_environ will be used instead.
 * @version_out: (optional) (type utf8) (out): The actual version number
 * @path_out: (optional) (type filename) (out): The absolute path of the
 *  Steam Runtime
 */
SrtRuntimeIssues
_srt_runtime_check (const char *bin32,
                    const char *expected_version,
                    const GStrv custom_environ,
                    gchar **version_out,
                    gchar **path_out)
{
  SrtRuntimeIssues issues = SRT_RUNTIME_ISSUES_NONE;
  GStatBuf zeroed_stat = {};
  GStatBuf expected_stat, actual_stat;
  GStatBuf pinned_libs_32;
  GStatBuf pinned_libs_64;
  GStatBuf lib_i386_linux_gnu;
  GStatBuf lib_x86_64_linux_gnu;
  GStatBuf usr_lib_i386_linux_gnu;
  GStatBuf usr_lib_x86_64_linux_gnu;
  GStatBuf amd64_bin;
  GStatBuf i386_bin;
  const gchar *env = NULL;
  gchar *contents = NULL;
  gchar *expected_path = NULL;
  gchar *path = NULL;
  gchar *version = NULL;
  gchar *version_txt = NULL;
  gsize len = 0;
  GError *error = NULL;
  GStrv my_environ = NULL;

  g_return_val_if_fail (version_out == NULL || *version_out == NULL,
                        SRT_RUNTIME_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (path_out == NULL || *path_out == NULL,
                        SRT_RUNTIME_ISSUES_INTERNAL_ERROR);

  if (custom_environ == NULL)
    my_environ = g_get_environ ();
  else
    my_environ = g_strdupv (custom_environ);

  env = g_environ_getenv (my_environ, "STEAM_RUNTIME");

  if (bin32 != NULL)
    expected_path = g_build_filename (bin32, "steam-runtime", NULL);

  if (g_strcmp0 (env, "0") == 0)
    {
      issues |= SRT_RUNTIME_ISSUES_DISABLED;
      goto out;
    }

  if (env == NULL || env[0] != '/')
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT;
    }
  else if (g_stat (env, &actual_stat) != 0)
    {
      g_debug ("stat %s: %s", env, g_strerror (errno));
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT;
      actual_stat = zeroed_stat;
    }

  if (issues & SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT)
    {
      /* Try to recover by using the expected path */
      if (expected_path != NULL)
        {
          path = g_strdup (expected_path);

          if (g_stat (path, &actual_stat) != 0)
            actual_stat = zeroed_stat;
        }
    }
  else
    {
      g_assert (env != NULL);
      g_assert (env[0] == '/');
      path = g_strdup (env);
    }

  /* If we haven't found it yet, there is nothing else we can check */
  if (path == NULL)
    goto out;

  if (expected_path != NULL && strcmp (path, expected_path) != 0)
    {
      if (g_stat (expected_path, &expected_stat) == 0)
        {
          if (!same_stat (&expected_stat, &actual_stat))
            {
              g_debug ("%s and %s are different inodes", path, expected_path);
              issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION;
            }
        }
      else
        {
          g_debug ("stat %s: %s", expected_path, g_strerror (errno));
          /* If the expected location doesn't exist then logically
           * the actual Steam Runtime in use can't be in the expected
           * location... */
          issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION;
        }
    }

  version_txt = g_build_filename (path, "version.txt", NULL);

  if (g_file_get_contents (version_txt, &contents, &len, &error))
    {
      const char *underscore = strrchr (contents, '_');

      /* Remove trailing \n if any */
      if (len > 0 && contents[len - 1] == '\n')
        contents[--len] = '\0';

      if (len != strlen (contents) ||
          strchr (contents, '\n') != NULL ||
          underscore == NULL)
        {
          g_debug ("Corrupt runtime: contents of %s should be in the "
                   "format NAME_VERSION",
                   version_txt);
          issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
        }
      else if (!g_str_has_prefix (contents, "steam-runtime_"))
        {
          g_debug ("Unofficial Steam Runtime build %s", contents);
          issues |= SRT_RUNTIME_ISSUES_UNOFFICIAL;
        }

      if (underscore != NULL)
        {
          version = g_strdup (underscore + 1);

          if (version[0] == '\0')
            {
              g_debug ("Corrupt runtime: contents of %s is missing the expected "
                       "runtime version number",
                       version_txt);
              issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
            }

          if (expected_version != NULL &&
              strcmp (expected_version, version) != 0)
            {
              g_debug ("Expected Steam Runtime v%s, got v%s",
                       expected_version, version);
              issues |= SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION;
            }
        }

      g_clear_pointer (&contents, g_free);
    }
  else
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_RUNTIME;
      g_debug ("Unable to read %s: %s", version_txt, error->message);
      g_clear_error (&error);
    }

  should_be_dir (&issues, path, "scripts");
  should_be_executable (&issues, path, "run.sh");
  should_be_executable (&issues, path, "setup.sh");
  should_be_stattable (&issues, path, "amd64/lib/x86_64-linux-gnu",
                       &lib_x86_64_linux_gnu);
  should_be_stattable (&issues, path, "amd64/usr/lib/x86_64-linux-gnu",
                       &usr_lib_x86_64_linux_gnu);
  should_be_stattable (&issues, path, "i386/lib/i386-linux-gnu",
                       &lib_i386_linux_gnu);
  should_be_stattable (&issues, path, "i386/usr/lib/i386-linux-gnu",
                       &usr_lib_i386_linux_gnu);
  might_be_stattable (path, "pinned_libs_32", &pinned_libs_32);
  might_be_stattable (path, "pinned_libs_64", &pinned_libs_64);
  might_be_stattable (path, "amd64/usr/bin", &amd64_bin);
  might_be_stattable (path, "i386/usr/bin", &i386_bin);

  env = g_environ_getenv (my_environ, "STEAM_RUNTIME_PREFER_HOST_LIBRARIES");

  if (g_strcmp0 (env, "0") == 0)
    issues |= SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES;

  env = g_environ_getenv (my_environ, "LD_LIBRARY_PATH");

  if (env == NULL)
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH;
    }
  else
    {
      gchar **entries = g_strsplit (env, ":", 0);
      gchar **entry;
      gboolean saw_lib_i386_linux_gnu = FALSE;
      gboolean saw_lib_x86_64_linux_gnu = FALSE;
      gboolean saw_usr_lib_i386_linux_gnu = FALSE;
      gboolean saw_usr_lib_x86_64_linux_gnu = FALSE;
      gboolean saw_pinned_libs_32 = FALSE;
      gboolean saw_pinned_libs_64 = FALSE;

      for (entry = entries; entry != NULL && *entry != NULL; entry++)
        {
          /* Scripts that manipulate LD_LIBRARY_PATH have a habit of
           * adding empty entries */
          if (*entry[0] == '\0')
            continue;

          /* We compare by stat(), because the entries in the
           * LD_LIBRARY_PATH might not have been canonicalized by
           * chasing symlinks, replacing "/.." or "//", etc. */
          if (g_stat (*entry, &actual_stat) == 0)
            {
              if (same_stat (&actual_stat, &lib_i386_linux_gnu))
                saw_lib_i386_linux_gnu = TRUE;

              /* Don't use "else if": it would be legitimate for
               * usr/lib/i386-linux-gnu and lib/i386-linux-gnu
               * to be symlinks to the same place, in which case
               * seeing one counts as seeing both. */
              if (same_stat (&actual_stat, &usr_lib_i386_linux_gnu))
                saw_usr_lib_i386_linux_gnu = TRUE;

              if (same_stat (&actual_stat, &lib_x86_64_linux_gnu))
                saw_lib_x86_64_linux_gnu = TRUE;

              if (same_stat (&actual_stat, &usr_lib_x86_64_linux_gnu))
                saw_usr_lib_x86_64_linux_gnu = TRUE;

              /* The pinned libraries only count if they are before the
               * corresponding Steam Runtime directories */
              if (!saw_lib_i386_linux_gnu &&
                  !saw_usr_lib_i386_linux_gnu &&
                  pinned_libs_32.st_mode != 0 &&
                  same_stat (&actual_stat, &pinned_libs_32))
                saw_pinned_libs_32 = TRUE;

              if (!saw_lib_x86_64_linux_gnu &&
                  !saw_usr_lib_x86_64_linux_gnu &&
                  pinned_libs_64.st_mode != 0 &&
                  same_stat (&actual_stat, &pinned_libs_64))
                saw_pinned_libs_64 = TRUE;
            }
          else
            {
              g_debug ("stat LD_LIBRARY_PATH entry %s: %s",
                       *entry, g_strerror (errno));
            }
        }

      if (!saw_lib_x86_64_linux_gnu || !saw_usr_lib_x86_64_linux_gnu)
        {
          g_debug ("STEAM_RUNTIME/amd64/[usr/]lib/x86_64-linux-gnu missing "
                   "from LD_LIBRARY_PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH;
        }

      if (!saw_lib_i386_linux_gnu || !saw_usr_lib_i386_linux_gnu)
        {
          g_debug ("STEAM_RUNTIME/i386/[usr/]lib/i386-linux-gnu missing "
                   "from LD_LIBRARY_PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH;
        }

      if (!saw_pinned_libs_64 || !saw_pinned_libs_32)
        {
          g_debug ("Pinned libraries missing from LD_LIBRARY_PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES;
        }

      g_strfreev (entries);
    }

  env = g_environ_getenv (my_environ, "PATH");

  if (env == NULL)
    {
      issues |= SRT_RUNTIME_ISSUES_NOT_IN_PATH;
    }
  else
    {
      gchar **entries = g_strsplit (env, ":", 0);
      gchar **entry;
      gboolean saw_amd64_bin = FALSE;
      gboolean saw_i386_bin = FALSE;

      for (entry = entries; entry != NULL && *entry != NULL; entry++)
        {
          /* Scripts that manipulate PATH have a habit of adding empty
           * entries */
          if (*entry[0] == '\0')
            continue;

          /* We compare by stat(), because the entries in the
           * PATH might not have been canonicalized by
           * chasing symlinks, replacing "/.." or "//", etc. */
          if (g_stat (*entry, &actual_stat) == 0)
            {
              if (amd64_bin.st_mode != 0 &&
                  same_stat (&actual_stat, &amd64_bin))
                saw_amd64_bin = TRUE;

              if (i386_bin.st_mode != 0 &&
                  same_stat (&actual_stat, &i386_bin))
                saw_i386_bin = TRUE;

            }
          else
            {
              g_debug ("stat PATH entry %s: %s",
                       *entry, g_strerror (errno));
            }
        }

      if (!saw_amd64_bin && !saw_i386_bin)
        {
          g_debug ("Neither STEAM_RUNTIME/amd64/usr/bin nor STEAM_RUNTIME/i386/usr/bin "
                   "are available in PATH");
          issues |= SRT_RUNTIME_ISSUES_NOT_IN_PATH;
        }

      g_strfreev (entries);
    }

out:
  if (path_out != NULL)
    *path_out = g_steal_pointer (&path);

  if (version_out != NULL)
    *version_out = g_steal_pointer (&version);

  g_free (contents);
  g_free (expected_path);
  g_free (path);
  g_free (version);
  g_free (version_txt);
  g_strfreev (my_environ);
  g_clear_error (&error);
  return issues;
}
