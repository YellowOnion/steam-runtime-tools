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

#include "test-utils.h"

#include <glib-object.h>
#include "steam-runtime-tools/glib-compat.h"
#include <steam-runtime-tools/steam-runtime-tools.h>

static gchar *fake_home_parent = NULL;

/**
 * _srt_global_setup_private_xdg_dirs:
 *
 * Setup a fake home directory and, following the [XDG standard]
 * (https://specifications.freedesktop.org/mime-apps-spec/mime-apps-spec-1.0.html)
 * mask every XDG used in the MIME lookup to avoid altering the tests if there
 * were already other user defined MIME lists.
 *
 * Call this function one time before launching the tests because changing the
 * environment variables is not thread safe.
 *
 * Returns: (type filename) (transfer full): Absolute path to the newly created
 *  fake home directory.
 */
gchar *
_srt_global_setup_private_xdg_dirs (void)
{
  GError *error = NULL;
  gchar *fake_home_path = NULL;
  gchar *xdg_data_home = NULL;

  g_return_val_if_fail (fake_home_parent == NULL, NULL);

  /* Create a directory that we control, and then put the fake home
   * directory inside it, so we can delete and recreate the fake home
   * directory without being vulnerable to symlink attacks. */
  fake_home_parent = g_dir_make_tmp ("fake-home-XXXXXX", &error);
  g_assert_no_error (error);
  fake_home_path = g_build_filename (fake_home_parent, "home", NULL);

  xdg_data_home = g_build_filename (fake_home_path, ".local", "share", NULL);

  g_setenv ("XDG_CONFIG_HOME", xdg_data_home, TRUE);
  g_setenv ("XDG_CONFIG_DIRS", xdg_data_home, TRUE);
  g_setenv ("XDG_DATA_HOME", xdg_data_home, TRUE);
  g_setenv ("XDG_DATA_DIRS", xdg_data_home, TRUE);

  g_free (xdg_data_home);

  return fake_home_path;
}

/**
 * _srt_global_teardown_private_xdg_dirs:
 *
 * Teardown the previously created temporary directory.
 *
 * Returns: %TRUE if no errors occurred removing the temporary directory.
 */
gboolean
_srt_global_teardown_private_xdg_dirs (void)
{
  gboolean result;
  g_return_val_if_fail (fake_home_parent != NULL, FALSE);

  result = _srt_rm_rf (fake_home_parent);
  g_free (fake_home_parent);
  fake_home_parent = NULL;

  return result;
}