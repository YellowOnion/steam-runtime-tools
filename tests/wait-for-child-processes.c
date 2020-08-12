/*
 * Copyright © 2019-2020 Collabora Ltd.
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

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "test-utils.h"
#include "utils.h"

typedef struct
{
  int unused;
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

#define SPAWN_FLAGS \
  (G_SPAWN_SEARCH_PATH \
   | G_SPAWN_LEAVE_DESCRIPTORS_OPEN \
   | G_SPAWN_DO_NOT_REAP_CHILD)

static void
test_wait_for_all (Fixture *f,
                   gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  int wstat;
  gboolean ret;
  const char * const argv[] = { "sh", "-c", "exit 42", NULL };

  ret = g_spawn_async (NULL,  /* cwd */
                       (char **) argv,
                       NULL,  /* envp */
                       SPAWN_FLAGS,
                       NULL, NULL,    /* child setup */
                       NULL,  /* pid */
                       &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  ret = pv_wait_for_child_processes (0, &wstat, &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_cmpint (wstat, ==, -1);
}

static void
test_wait_for_main (Fixture *f,
                    gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  int wstat;
  gboolean ret;
  GPid main_pid;
  const char * const argv[] = { "sh", "-c", "exit 42", NULL };

  ret = g_spawn_async (NULL,  /* cwd */
                       (char **) argv,
                       NULL,  /* envp */
                       SPAWN_FLAGS,
                       NULL, NULL,    /* child setup */
                       &main_pid,
                       &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  ret = pv_wait_for_child_processes (main_pid, &wstat, &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_true (WIFEXITED (wstat));
  g_assert_cmpint (WEXITSTATUS (wstat), ==, 42);
}

static void
test_wait_for_main_plus (Fixture *f,
                         gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  int wstat;
  gboolean ret;
  GPid main_pid;
  const char * const before_argv[] = { "sh", "-c", "exit 0", NULL };
  const char * const main_argv[] = { "sh", "-c", "sleep 1; kill -TERM $$", NULL };
  const char * const after_argv[] = { "sh", "-c", "sleep 2", NULL };

  ret = g_spawn_async (NULL,  /* cwd */
                       (char **) before_argv,
                       NULL,  /* envp */
                       SPAWN_FLAGS,
                       NULL, NULL,    /* child setup */
                       NULL,          /* PID */
                       &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  ret = g_spawn_async (NULL,  /* cwd */
                       (char **) main_argv,
                       NULL,  /* envp */
                       SPAWN_FLAGS,
                       NULL, NULL,    /* child setup */
                       &main_pid,
                       &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  ret = g_spawn_async (NULL,  /* cwd */
                       (char **) after_argv,
                       NULL,  /* envp */
                       SPAWN_FLAGS,
                       NULL, NULL,    /* child setup */
                       NULL,          /* PID */
                       &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  ret = pv_wait_for_child_processes (main_pid, &wstat, &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_true (WIFSIGNALED (wstat));
  g_assert_cmpint (WTERMSIG (wstat), ==, SIGTERM);
}

static void
test_wait_for_nothing (Fixture *f,
                       gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  int wstat;
  gboolean ret;

  ret = pv_wait_for_child_processes (0, &wstat, &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_cmpint (wstat, ==, -1);
}

static void
test_wait_for_wrong_main (Fixture *f,
                          gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  int wstat;
  gboolean ret;

  ret = pv_wait_for_child_processes (getpid (), &wstat, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ret);
  g_assert_cmpint (wstat, ==, -1);
}

int
main (int argc,
      char **argv)
{
  pv_avoid_gvfs ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/wait/for-all", Fixture, NULL,
              setup, test_wait_for_all, teardown);
  g_test_add ("/wait/for-main", Fixture, NULL,
              setup, test_wait_for_main, teardown);
  g_test_add ("/wait/for-main-plus", Fixture, NULL,
              setup, test_wait_for_main_plus, teardown);
  g_test_add ("/wait/for-nothing", Fixture, NULL,
              setup, test_wait_for_nothing, teardown);
  g_test_add ("/wait/for-wrong-main", Fixture, NULL,
              setup, test_wait_for_wrong_main, teardown);

  return g_test_run ();
}
