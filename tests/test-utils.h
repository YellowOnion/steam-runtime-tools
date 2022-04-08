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

#pragma once

/*
 * Simplified implementations of some of the GLib test assertion macros,
 * for use with older GLib versions.
 */

#include <glib.h>

/* for its backports of g_test_skip(), etc. */
#include <libglnx.h>

#ifndef g_assert_true
#define g_assert_true(x) g_assert ((x))
#endif

#ifndef g_assert_false
#define g_assert_false(x) g_assert (!(x))
#endif

#ifndef g_assert_cmpint
#define g_assert_cmpint(a, op, b) g_assert ((a) op (b))
#endif

#ifndef g_assert_cmpmem
#define g_assert_cmpmem(m1, l1, m2, l2) \
    g_assert (l1 == l2 && memcmp (m1, m2, l1) == 0)
#endif

#ifndef g_assert_cmpstr
#define g_assert_cmpstr(a, op, b) g_assert (g_strcmp0 ((a), (b)) op 0)
#endif

#ifndef g_assert_no_errno
#define g_assert_no_errno(expr) \
  g_assert_cmpstr ((expr) >= 0 ? NULL : g_strerror (errno), ==, NULL)
#endif

/*
 * Other assorted test helpers.
 */

gchar *_srt_global_setup_private_xdg_dirs (void);
gboolean _srt_global_teardown_private_xdg_dirs (void);

typedef GHashTable *TestsOpenFdSet;
TestsOpenFdSet tests_check_fd_leaks_enter (void);
void tests_check_fd_leaks_leave (TestsOpenFdSet fds);

gchar *_srt_global_setup_sysroots (const char *argv0);
gboolean _srt_global_teardown_sysroots (void);
