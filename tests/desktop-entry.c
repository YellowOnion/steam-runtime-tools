/*
 * Copyright © 2020 Collabora Ltd.
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

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/desktop-entry-internal.h"
#include "test-utils.h"
#include "fake-home.h"

static const char *argv0;
static gchar *fake_home_path;

typedef struct
{
  gchar *builddir;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->builddir);

  /* We expect that fake_home already cleaned this up, but just to be sure we
   * do it too */
  _srt_rm_rf (fake_home_path);
}

/*
 * Test basic functionality of the SrtDesktopEntry object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtDesktopEntry *entry;
  gchar *id = NULL;
  gchar *cmd = NULL;
  gchar *filename = NULL;
  gboolean is_default = FALSE;
  gboolean is_steam_handler = FALSE;

  entry = _srt_desktop_entry_new ("steam.desktop",
                                  "/usr/bin/steam %U",
                                  "/usr/share/applications/steam.desktop",
                                  TRUE,
                                  TRUE);
  g_assert_cmpstr (srt_desktop_entry_get_id (entry), ==, "steam.desktop");
  g_assert_cmpstr (srt_desktop_entry_get_commandline (entry), ==, "/usr/bin/steam %U");
  g_assert_cmpstr (srt_desktop_entry_get_filename (entry), ==, "/usr/share/applications/steam.desktop");
  g_assert_cmpint (srt_desktop_entry_is_default_handler (entry), ==, TRUE);
  g_assert_cmpint (srt_desktop_entry_is_steam_handler (entry), ==, TRUE);
  g_object_get (entry,
                "id", &id,
                "commandline", &cmd,
                "filename", &filename,
                "is-default-handler", &is_default,
                "is-steam-handler", &is_steam_handler,
                NULL);
  g_assert_cmpstr (id, ==, "steam.desktop");
  g_assert_cmpstr (cmd, ==, "/usr/bin/steam %U");
  g_assert_cmpstr (filename, ==, "/usr/share/applications/steam.desktop");
  g_assert_cmpint (is_default, ==, TRUE);
  g_assert_cmpint (is_steam_handler, ==, TRUE);
  g_free (id);
  g_free (cmd);
  g_free (filename);
  g_object_unref (entry);
}

static void
test_default_entry (Fixture *f,
                    gconstpointer context)
{
  SrtSystemInfo *info;
  FakeHome *fake_home;
  GList *desktop_entries = NULL;
  const gchar *filename = NULL;

  fake_home = fake_home_new (fake_home_path);
  fake_home->create_pinning_libs = FALSE;
  fake_home->create_i386_folders = FALSE;
  fake_home->create_amd64_folders = FALSE;
  fake_home->create_root_symlink = FALSE;
  fake_home->create_steam_symlink = FALSE;
  fake_home->create_steamrt_files = FALSE;
  fake_home->add_environments = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  desktop_entries = srt_system_info_list_desktop_entries (info);
  g_assert_null (desktop_entries->next);
  g_assert_cmpstr (srt_desktop_entry_get_id (desktop_entries->data), ==, "steam.desktop");
  g_assert_cmpstr (srt_desktop_entry_get_commandline (desktop_entries->data), ==, "/usr/bin/env steam %U");
  filename = srt_desktop_entry_get_filename (desktop_entries->data);
  g_assert_true (g_str_has_prefix (filename, "/"));
  g_assert_true (g_str_has_suffix (filename, "/steam.desktop"));
  g_assert_true (srt_desktop_entry_is_default_handler (desktop_entries->data));
  g_assert_true (srt_desktop_entry_is_steam_handler (desktop_entries->data));

  g_list_free_full (desktop_entries, g_object_unref);

  /* Do the check again, this time using the cache */
  desktop_entries = srt_system_info_list_desktop_entries (info);
  g_assert_null (desktop_entries->next);
  g_assert_cmpstr (srt_desktop_entry_get_id (desktop_entries->data), ==, "steam.desktop");
  g_assert_cmpstr (srt_desktop_entry_get_commandline (desktop_entries->data), ==, "/usr/bin/env steam %U");
  filename = srt_desktop_entry_get_filename (desktop_entries->data);
  g_assert_true (g_str_has_prefix (filename, "/"));
  g_assert_true (g_str_has_suffix (filename, "/steam.desktop"));
  g_assert_true (srt_desktop_entry_is_default_handler (desktop_entries->data));
  g_assert_true (srt_desktop_entry_is_steam_handler (desktop_entries->data));

  g_list_free_full (desktop_entries, g_object_unref);
  fake_home_clean_up (fake_home);
  g_clear_object (&info);
}

int
main (int argc,
      char **argv)
{
  int status;
  argv0 = argv[0];

  /* We can't use %G_TEST_OPTION_ISOLATE_DIRS because we are targeting an older glib verion.
   * As a workaround we use our function `setup_env()`.
   * We can't use an environ because in `_srt_steam_check()` we call `g_app_info_get_*` that
   * doesn't support a custom environ. */
  g_test_init (&argc, &argv, NULL);
  fake_home_path = _srt_global_setup_private_xdg_dirs ();

  g_test_add ("/desktop-entry/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/desktop-entry/default_entry", Fixture, NULL,
              setup, test_default_entry, teardown);

  status = g_test_run ();

  if (!_srt_global_teardown_private_xdg_dirs ())
    g_debug ("Unable to remove the fake home parent directory of: %s", fake_home_path);

  return status;
}
