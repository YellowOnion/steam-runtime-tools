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

#include "fake-home.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "test-utils.h"
#include <steam-runtime-tools/steam-runtime-tools.h>

/**
 * fake_home_new:
 *
 * Create a new #FakeHome object and a temporary folder in the file system
 *
 * Returns: the newely created #FakeHome object
 */
FakeHome *
fake_home_new (void)
{
  GError *error = NULL;
  FakeHome *fake_home = g_slice_new (FakeHome);
  fake_home->home = g_dir_make_tmp ("fake-home-XXXXXX", &error);
  g_assert_no_error (error);

  fake_home->create_pinning_libs = TRUE;
  fake_home->create_i386_folders = TRUE;
  fake_home->create_amd64_folders = TRUE;
  fake_home->create_root_symlink = TRUE;
  fake_home->create_steam_symlink = TRUE;
  fake_home->create_steamrt_files = TRUE;
  fake_home->add_environments = TRUE;
  fake_home->has_debian_bug_916303 = FALSE;
  fake_home->testing_beta_client = FALSE;

  return fake_home;
}

/**
 * fake_home_create_structure:
 * @f: a #FakeHome object used to create the folder structure
 *
 * Create folders and files like a canonical Steam Installation.
 * As the home directory, the temporary folder stored in `home` is used.
 * A custom environ is also created and stored under `env`.
 *
 * Returns: %TRUE if the creation was successful
 */
gboolean
fake_home_create_structure (FakeHome *f)
{
  gboolean ret = FALSE;
  GFile *symlink_gfile = NULL;
  gchar *dot_steam = NULL;
  gchar *scripts = NULL;
  gchar *run = NULL;
  gchar *setup = NULL;
  gchar *version = NULL;
  gchar *dot_steam_bin32 = NULL;
  gchar *dot_steam_root = NULL;
  gchar *dot_steam_steam = NULL;
  gchar *local_share = NULL;
  gchar *ld_path = NULL;
  gchar *prepended_path = NULL;
  gchar *ubuntu12_32 = NULL;
  GError *error = NULL;
  int saved_errno;

  g_return_val_if_fail (f != NULL, FALSE);

  dot_steam = g_build_filename (f->home, ".steam", NULL);

  if (f->has_debian_bug_916303)
    {
      f->steam_install = g_strdup (dot_steam);
      f->steam_data = g_build_filename (dot_steam, "steam", NULL);
    }
  else if (f->testing_beta_client)
    {
      f->steam_install = g_build_filename (f->home, "beta-client", NULL);
      f->steam_data = g_build_filename (f->home, ".local", "share",
                                        "Steam", NULL);
    }
  else
    {
      f->steam_install = g_build_filename (f->home, ".local", "share",
                                           "Steam", NULL);
      f->steam_data = g_strdup (f->steam_install);
    }

  f->runtime = g_build_filename (f->steam_install, "ubuntu12_32",
                                 "steam-runtime", NULL);

  scripts = g_build_filename (f->runtime, "scripts", NULL);

  if (g_mkdir_with_parents (dot_steam, 0755) != 0)
    goto out;
  if (g_mkdir_with_parents (f->steam_data, 0755) != 0)
    goto out;
  if (g_mkdir_with_parents (f->steam_install, 0755) != 0)
    goto out;
  if (g_mkdir_with_parents (scripts, 0755) != 0)
    goto out;

  f->pinned_32 = g_build_filename (f->runtime, "pinned_libs_32", NULL);
  f->pinned_64 = g_build_filename (f->runtime, "pinned_libs_64", NULL);
  f->i386_lib_i386 = g_build_filename (f->runtime, "i386", "lib", "i386-linux-gnu", NULL);
  f->i386_lib = g_build_filename (f->runtime, "i386", "lib", NULL);
  f->i386_usr_lib_i386 = g_build_filename (f->runtime, "i386", "usr", "lib", "i386-linux-gnu", NULL);
  f->i386_usr_lib = g_build_filename (f->runtime, "i386", "usr", "lib", NULL);
  f->i386_usr_bin = g_build_filename (f->runtime, "i386", "usr", "bin", NULL);
  f->amd64_lib_64 = g_build_filename (f->runtime, "amd64", "lib", "x86_64-linux-gnu", NULL);
  f->amd64_lib = g_build_filename (f->runtime, "amd64", "lib", NULL);
  f->amd64_usr_lib_64 = g_build_filename (f->runtime, "amd64", "usr", "lib", "x86_64-linux-gnu", NULL);
  f->amd64_usr_lib = g_build_filename (f->runtime, "amd64", "usr", "lib", NULL);
  f->amd64_bin = g_build_filename (f->runtime, "amd64", "bin", NULL);
  f->amd64_usr_bin = g_build_filename (f->runtime, "amd64", "usr", "bin", NULL);

  if (f->create_pinning_libs)
    {
      if (g_mkdir_with_parents (f->pinned_32, 0755) != 0)
        goto out;
      if (g_mkdir_with_parents (f->pinned_64, 0755) != 0)
        goto out;
    }

  if (f->create_i386_folders)
    {
      if (g_mkdir_with_parents (f->i386_lib_i386, 0755) != 0)
        goto out;
      if (g_mkdir_with_parents (f->i386_usr_lib_i386, 0755) != 0)
        goto out;
      if (g_mkdir_with_parents (f->i386_usr_bin, 0755) != 0)
        goto out;
    }

  if (f->create_amd64_folders)
    {
      if (g_mkdir_with_parents (f->amd64_lib_64, 0755) != 0)
        goto out;
      if (g_mkdir_with_parents (f->amd64_usr_lib_64, 0755) != 0)
        goto out;
      if (g_mkdir_with_parents (f->amd64_usr_bin, 0755) != 0)
        goto out;
    }

  if (f->create_steamrt_files)
    {
      run = g_build_filename (f->runtime, "run.sh", NULL);
      setup = g_build_filename (f->runtime, "setup.sh", NULL);
      version = g_build_filename (f->runtime, "version.txt", NULL);

      if (g_creat (run, 0755) < 0)
        goto out;
      if (g_creat (setup, 0755) < 0)
        goto out;
      if (g_creat (version, 0755) < 0)
        goto out;

      g_file_set_contents (version, "steam-runtime_0.20190711.3", -1, &error);
      g_assert_no_error (error);
    }

  if (f->has_debian_bug_916303)
    {
      dot_steam_steam = g_build_filename (dot_steam, "steam", NULL);
      if (g_mkdir_with_parents (dot_steam_steam, 0755) != 0)
        goto out;
    }

  if (f->create_root_symlink)
    {
      if (dot_steam_root == NULL)
        dot_steam_root = g_build_filename (dot_steam, "root", NULL);
      symlink_gfile = g_file_new_for_path (dot_steam_root);

      g_file_make_symbolic_link (symlink_gfile, f->steam_install, NULL, &error);
      g_object_unref (symlink_gfile);
      g_assert_no_error (error);

      dot_steam_bin32 = g_build_filename (dot_steam, "bin32", NULL);
      symlink_gfile = g_file_new_for_path (dot_steam_bin32);

      ubuntu12_32 = g_build_filename (f->steam_install, "ubuntu12_32", NULL);
      g_file_make_symbolic_link (symlink_gfile, ubuntu12_32, NULL, &error);
      g_object_unref (symlink_gfile);
      g_assert_no_error (error);
    }

  if (f->create_steam_symlink)
    {
      if (dot_steam_steam == NULL)
        dot_steam_steam = g_build_filename (dot_steam, "steam", NULL);
      symlink_gfile = g_file_new_for_path (dot_steam_steam);

      g_file_make_symbolic_link (symlink_gfile, f->steam_data, NULL, &error);
      g_object_unref (symlink_gfile);
      if (f->has_debian_bug_916303)
        {
          g_error_free (error);
          error = NULL;
        }
      else
        {
          g_assert_no_error (error);
        }
    }

  local_share = g_build_filename (f->home, ".local", "share", NULL);
  f->env = g_get_environ ();
  f->env = g_environ_setenv (f->env, "HOME", f->home, TRUE);
  f->env = g_environ_setenv (f->env, "XDG_DATA_HOME", local_share, TRUE);
  /* Make sure we don't find /etc/os-release or /usr/lib/os-release
   * if we happen to be in a Steam Runtime container */
  f->env = g_environ_setenv (f->env, "SRT_TEST_SYSROOT", f->home, TRUE);

  if (f->add_environments)
    {
      const gchar *path;

      f->env = g_environ_setenv (f->env, "STEAM_RUNTIME", f->runtime, TRUE);

      ld_path = g_strjoin (":",
                           f->pinned_32,
                           f->pinned_64,
                           f->i386_lib_i386,
                           f->i386_lib,
                           f->i386_usr_lib_i386,
                           f->i386_usr_lib,
                           f->amd64_lib_64,
                           f->amd64_lib,
                           f->amd64_usr_lib_64,
                           f->amd64_usr_lib,
                           NULL);
      f->env = g_environ_setenv (f->env, "LD_LIBRARY_PATH", ld_path, TRUE);

      path = g_environ_getenv (f->env, "PATH");

      prepended_path = g_strjoin (":",
                                  f->amd64_bin,
                                  f->amd64_usr_bin,
                                  path,
                                  NULL);

      f->env = g_environ_setenv (f->env, "PATH", prepended_path, TRUE);
    }

  ret = TRUE;

  out:
    saved_errno = errno;
    g_free (dot_steam);
    g_free (scripts);
    g_free (run);
    g_free (setup);
    g_free (version);
    g_free (dot_steam_bin32);
    g_free (dot_steam_root);
    g_free (dot_steam_steam);
    g_free (local_share);
    g_free (ld_path);
    g_free (prepended_path);
    g_free (ubuntu12_32);

    if (!ret)
      g_warning ("Unable to create directories: %s", g_strerror (saved_errno));

    return ret;

}

/**
 * fake_home_clean_up:
 * @f: the #FakeHome object to clean up and finalize
 *
 * Clean up a #FakeHome object recursively removing the created
 * home directory and freeing @f.
 */
void
fake_home_clean_up (FakeHome *f)
{
  if (!_srt_rm_rf (f->home))
    g_debug ("Unable to remove the fake home directory: %s", f->home);

  g_free (f->home);
  g_free (f->steam_data);
  g_free (f->steam_install);
  g_free (f->runtime);
  g_free (f->pinned_32);
  g_free (f->pinned_64);
  g_free (f->i386_lib_i386);
  g_free (f->i386_lib);
  g_free (f->i386_usr_lib_i386);
  g_free (f->i386_usr_lib);
  g_free (f->i386_usr_bin);
  g_free (f->amd64_lib_64);
  g_free (f->amd64_lib);
  g_free (f->amd64_usr_lib_64);
  g_free (f->amd64_usr_lib);
  g_free (f->amd64_bin);
  g_free (f->amd64_usr_bin);
  g_strfreev (f->env);

  g_slice_free (FakeHome, f);
}

/**
 * fake_home_apply_to_system_info:
 * @fake_home: The fake home directory
 * @info: The system info context
 *
 * Make @info look in @fake_home instead of the real root/home directories.
 */
void
fake_home_apply_to_system_info (FakeHome *fake_home,
                                SrtSystemInfo *info)
{
  g_return_if_fail (fake_home != NULL);
  g_return_if_fail (fake_home->env != NULL);
  g_return_if_fail (SRT_IS_SYSTEM_INFO (info));

  srt_system_info_set_environ (info, fake_home->env);
}
