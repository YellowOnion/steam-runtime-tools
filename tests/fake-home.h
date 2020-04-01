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

#pragma once

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

typedef struct
{
  gboolean create_pinning_libs;
  gboolean create_i386_folders;
  gboolean create_amd64_folders;
  gboolean create_root_symlink;
  gboolean create_steam_symlink;
  gboolean create_steamrt_files;
  gboolean add_environments;
  gboolean has_debian_bug_916303;
  gboolean testing_beta_client;
  gboolean create_steam_mime_apps;

  gchar *home;
  gchar *steam_install;
  gchar *steam_data;
  gchar *runtime;
  gchar *pinned_32;
  gchar *pinned_64;
  gchar *i386_lib_i386;
  gchar *i386_lib;
  gchar *i386_usr_lib_i386;
  gchar *i386_usr_lib;
  gchar *i386_usr_bin;
  gchar *amd64_lib_64;
  gchar *amd64_lib;
  gchar *amd64_usr_lib_64;
  gchar *amd64_usr_lib;
  gchar *amd64_bin;
  gchar *amd64_usr_bin;
  gchar *sysroot;
  GStrv env;
} FakeHome;

FakeHome * fake_home_new (const gchar *home);
gboolean fake_home_create_structure (FakeHome *fake_home);
void fake_home_clean_up (FakeHome *f);
void fake_home_apply_to_system_info (FakeHome *fake_home,
                                     SrtSystemInfo *info);
