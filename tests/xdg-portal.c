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

/* Include this before steam-runtime-tools.h so that its backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "steam-runtime-tools/glib-backports-internal.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/xdg-portal-internal.h"
#include "test-utils.h"

static const char *argv0;
static gchar *global_sysroots;

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
}

typedef struct
{
  const gchar *name;
  gboolean is_available;
} XdgPortalBackendTest;

typedef struct
{
  const gchar *name;
  gboolean is_available;
  guint32 version;
} XdgPortalInterfaceTest;

typedef struct
{
  const gchar *description;
  const gchar *multiarch_tuple;
  const gchar *messages;
  const gchar *sysroot;
  SrtTestFlags test_flags;
  SrtXdgPortalIssues issues;
  XdgPortalInterfaceTest xdg_portal_interfaces_info[3];
  XdgPortalBackendTest xdg_portal_backends_info[3];
} XdgPortalTest;

static const XdgPortalTest xdg_portal_test[] =
{
  {
    .description = "Missing OpenURI portal",
    .multiarch_tuple = "mock-bad",
    .messages = "The 'version' property is not available for 'org.freedesktop.portal.OpenURI', "
                "either there isn't a working xdg-desktop-portal or it is a very old version\n",
    .issues = SRT_XDG_PORTAL_ISSUES_MISSING_INTERFACE,
    .xdg_portal_interfaces_info =
    {
      {
        .name = "org.freedesktop.portal.OpenURI",
        .is_available = FALSE,
        .version = 0,
      },
      {
        .name = "org.freedesktop.portal.Email",
        .is_available = TRUE,
        .version = 3,
      },
    },
    .xdg_portal_backends_info =
    {
      {
        .name = "org.freedesktop.impl.portal.desktop.gtk",
        .is_available = TRUE,
      },
      {
        .name = "org.freedesktop.impl.portal.desktop.kde",
        .is_available = FALSE,
      },
    },
  },

  {
    .description = "Good system check",
    .multiarch_tuple = "mock-good",
    .issues = SRT_XDG_PORTAL_ISSUES_NONE,
    .xdg_portal_interfaces_info =
    {
      {
        .name = "org.freedesktop.portal.OpenURI",
        .is_available = TRUE,
        .version = 2,
      },
      {
        .name = "org.freedesktop.portal.Email",
        .is_available = TRUE,
        .version = 3,
      },
    },
    .xdg_portal_backends_info =
    {
      {
        .name = "org.freedesktop.impl.portal.desktop.gtk",
        .is_available = TRUE,
      },
      {
        .name = "org.freedesktop.impl.portal.desktop.kde",
        .is_available = FALSE,
      },
    },
  },

  {
    .description = "Good Flatpak environment",
    .multiarch_tuple = "mock-good-flatpak",
    .sysroot = "flatpak-example",
    .issues = SRT_XDG_PORTAL_ISSUES_NONE,
    .xdg_portal_interfaces_info =
    {
      {
        .name = "org.freedesktop.portal.OpenURI",
        .is_available = TRUE,
        .version = 3,
      },
      {
        .name = "org.freedesktop.portal.Email",
        .is_available = TRUE,
        .version = 3,
      },
    },
  },

  {
    .description = "XDG portal check timeout",
    .multiarch_tuple = "mock-hanging",
    .test_flags = SRT_TEST_FLAGS_TIME_OUT_SOONER,
    .issues = SRT_XDG_PORTAL_ISSUES_TIMEOUT,
  },
};

static void
test_check_xdg_portal (Fixture *f,
                       gconstpointer context)
{
  for (gsize i = 0; i < G_N_ELEMENTS (xdg_portal_test); i++)
    {
      const XdgPortalTest *t = &xdg_portal_test[i];
      g_autoptr(SrtSystemInfo) info = NULL;
      g_autoptr(SrtObjectList) portal_interfaces = NULL;
      GList *portal_backends = NULL;
      SrtXdgPortalIssues issues;
      g_autofree gchar *messages = NULL;
      g_autofree gchar *sysroot = NULL;
      GList *iter;
      gsize j;

      g_test_message ("%s", t->description);

      info = srt_system_info_new (NULL);
      srt_system_info_set_helpers_path (info, f->builddir);
      srt_system_info_set_primary_multiarch_tuple (info, t->multiarch_tuple);
      srt_system_info_set_test_flags (info, t->test_flags);
      if (t->sysroot != NULL)
        {
          sysroot = g_build_filename (global_sysroots, t->sysroot, NULL);
          srt_system_info_set_sysroot (info, sysroot);
        }

      portal_interfaces = srt_system_info_list_xdg_portal_interfaces (info);
      for (j = 0, iter = portal_interfaces; iter != NULL; iter = iter->next, j++)
        {
          g_assert_cmpstr (t->xdg_portal_interfaces_info[j].name, ==,
                           srt_xdg_portal_interface_get_name (iter->data));
          g_assert_cmpint (t->xdg_portal_interfaces_info[j].is_available, ==,
                           srt_xdg_portal_interface_is_available (iter->data));
          g_assert_cmpint (t->xdg_portal_interfaces_info[j].version, ==,
                           srt_xdg_portal_interface_get_version (iter->data));
        }
      g_assert_cmpstr (t->xdg_portal_interfaces_info[j].name, ==, NULL);

      portal_backends = srt_system_info_list_xdg_portal_backends (info);
      for (j = 0, iter = portal_backends; iter != NULL; iter = iter->next, j++)
        {
          g_assert_cmpstr (t->xdg_portal_backends_info[j].name, ==,
                           srt_xdg_portal_backend_get_name (iter->data));
          g_assert_cmpint (t->xdg_portal_backends_info[j].is_available, ==,
                           srt_xdg_portal_backend_is_available (iter->data));
        }
      g_assert_cmpstr (t->xdg_portal_backends_info[j].name, ==, NULL);

      issues = srt_system_info_get_xdg_portal_issues (info, &messages);

      g_assert_cmpint (issues, ==, t->issues);
      g_assert_cmpstr (messages, ==, t->messages);
    }
}

int
main (int argc,
      char **argv)
{
  int ret;

  argv0 = argv[0];
  global_sysroots = _srt_global_setup_sysroots (argv0);

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/xdg-portal/test_check_xdg_portal", Fixture, NULL, setup,
              test_check_xdg_portal, teardown);

  ret = g_test_run ();
  _srt_global_teardown_sysroots ();
  g_clear_pointer (&global_sysroots, g_free);
  return ret;
}
