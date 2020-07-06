// Copyright © 2017 Collabora Ltd

// This file is part of libcapsule.

// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.

// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "tests/test-helpers.h"
#include "utils/library-cmp.h"
#include "utils/utils.h"

static const char *argv0;

#define assert_with_errno(expr) \
  do { \
    errno = 0; \
    \
    if (!(expr)) \
      g_error ("Assertion failed: %s: %s", #expr, g_strerror (errno)); \
  } while (0)

static void
touch (const char *path)
{
  FILE *fh;

  assert_with_errno((fh = fopen (path, "w")) != NULL);
  fclose (fh);
}

typedef struct
{
  gchar *srcdir;
  gchar *builddir;
  gboolean uninstalled;
} Fixture;

static void
setup (Fixture *f,
       gconstpointer data)
{
  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  f->uninstalled = (g_getenv ("CAPSULE_TESTS_UNINSTALLED") != NULL);
}

static struct
{
  const char * const parts[3];
  const char *expected;
} filename_tests[] =
{
    { { "/host", "/usr/lib", "libc.so.6" }, "/host/usr/lib/libc.so.6" },
    { { "/usr/lib", "/libc.so.6", NULL }, "/usr/lib/libc.so.6" },
    { { "", "/usr/lib", "libc.so.6" }, "/usr/lib/libc.so.6" },
    { { "/", "usr/lib", "libc.so.6" }, "/usr/lib/libc.so.6" },
    { { "///host///", "///usr/lib///", "///libc.so.6" },
      "/host/usr/lib/libc.so.6" },
    { { NULL, "xxxxxxxxxxxxxxxx", NULL }, "" },
    { { "", NULL }, "" },
    { { "", "/etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "", "etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "/", "/etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "/", "etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "foo", "/bar", NULL }, "foo/bar" },
    { { "foo", "bar", NULL }, "foo/bar" },
};

static void
test_build_filename (Fixture *f,
                     gconstpointer data)
{
  unsigned i;

  for (i = 0; i < G_N_ELEMENTS (filename_tests); i++)
    {
      const char * const *parts = filename_tests[i].parts;
      const char *expected = filename_tests[i].expected;
      const size_t allocated = strlen (expected) + 5;
      gchar *buf = g_malloc (allocated);
      char *str = NULL;
      size_t used;
      size_t len = allocated;

      if (parts[1] == NULL)
        str = build_filename_alloc (parts[0], NULL);
      else if (parts[2] == NULL)
        str = build_filename_alloc (parts[0], parts[1], NULL);
      else
        str = build_filename_alloc (parts[0], parts[1], parts[2], NULL);

      g_assert_cmpstr (str, ==, expected);

      do
        {
          size_t j;

          memset (buf, '\xaa', allocated);

          if (parts[1] == NULL)
            {
              used = build_filename (buf, len, parts[0], NULL);
            }
          else if (parts[2] == NULL)
            {
              used = build_filename (buf, len, parts[0], parts[1], NULL);
            }
          else
            {
              used = build_filename (buf, len, parts[0], parts[1], parts[2],
                                     NULL);
            }

          g_test_message ("\"%s\", \"%s\", \"%s\" -> %zu \"%s\"",
                          parts[0], parts[1], parts[2], used,
                          len == 0 ? NULL : buf);

          g_assert_cmpuint (used, ==, strlen (expected));

          if (len == 0)
            {
              /* Stupid corner case: we can't write anything into the buffer */
            }
          else if (used >= len)
            {
              gchar *truncated = g_strndup (expected, len - 1);

              g_assert_cmpstr (buf, ==, truncated);
              g_free (truncated);
            }
          else
            {
              g_assert_cmpstr (buf, ==, expected);
            }

          /* The rest of the buffer is untouched (we didn't overflow) */
          for (j = len; j < allocated; j++)
            {
              g_assert_cmpint (buf[j], ==, '\xaa');
            }
        }
      while (--len > 0);

      g_free (buf);
      free (str);
    }
}

typedef struct
{
  const char *soname;
  const char *in_container;
  char cmp;
  const char *in_provider;
} CmpByNameTest;

static const CmpByNameTest cmp_by_name_tests[] =
{
  { "libdbus-1.so.3", "libdbus-1.so.3.1", '<', "libdbus-1.so.3.2" },
  { "libdbus-1.so.3", "libdbus-1.so.3.1.2", '>', "libdbus-1.so.3.1.1" },
  { "libdbus-1.so.3", "libdbus-1.so.3.1", '=', "libdbus-1.so.3.1" },
  { "libc.so.6", "libc-2.19.so", '<', "libc-2.22.so" },
  { "libgcc_s.so.1", "libgcc_s-20200703.so.1", '>', "libgcc_s-20120401.so.1" },
  { "libgcc_s.so.1", "libgcc_s-20200703.so.1", '=', NULL },
  { "libgcc_s.so.1", NULL, '=', "libgcc_s-20200703.so.1" },
  { "libgcc_s.so.1", NULL, '=', NULL },
};

static void
test_library_cmp_by_name (Fixture *f,
                          gconstpointer data)
{
  GError *error = NULL;
  gchar *tmpdir = g_dir_make_tmp ("libcapsule.XXXXXX", &error);
  gchar *container;
  gchar *provider;
  gsize i;

  g_assert_no_error (error);
  g_assert_nonnull (tmpdir);

  container = g_build_filename (tmpdir, "c", NULL);
  provider = g_build_filename (tmpdir, "p", NULL);

  assert_with_errno (g_mkdir (container, 0755) == 0);
  assert_with_errno (g_mkdir (provider, 0755) == 0);

  for (i = 0; i < G_N_ELEMENTS (cmp_by_name_tests); i++)
    {
      const CmpByNameTest *test = &cmp_by_name_tests[i];
      gchar *container_file = NULL;
      gchar *container_lib = g_build_filename (container, test->soname, NULL);
      gchar *provider_file = NULL;
      gchar *provider_lib = g_build_filename (provider, test->soname, NULL);
      int result;

      assert_with_errno (g_unlink (container_lib) == 0 || errno == ENOENT);
      assert_with_errno (g_unlink (provider_lib) == 0 || errno == ENOENT);

      if (test->in_container == NULL)
        {
          touch (container_lib);
        }
      else
        {
          container_file = g_build_filename (container, test->in_container, NULL);
          assert_with_errno (g_unlink (container_file) == 0 || errno == ENOENT);
          touch (container_file);
          assert_with_errno (symlink (test->in_container, container_lib) == 0);
        }

      if (test->in_provider == NULL)
        {
          touch (provider_lib);
        }
      else
        {
          provider_file = g_build_filename (provider, test->in_provider, NULL);
          assert_with_errno (g_unlink (provider_file) == 0 || errno == ENOENT);
          touch (provider_file);
          assert_with_errno (symlink (test->in_provider, provider_lib) == 0);
        }

      result = library_cmp_by_name (test->soname,
                                    container_lib, container,
                                    provider_lib, provider);

      switch (test->cmp)
        {
          case '<':
            if (result >= 0)
              g_error ("Expected %s (%s) < %s (%s), got %d",
                       container_lib,
                       container_file ? container_file : "regular file",
                       provider_lib,
                       provider_file ? provider_file : "regular file",
                       result);
            break;

          case '>':
            if (result <= 0)
              g_error ("Expected %s (%s) > %s (%s), got %d",
                       container_lib,
                       container_file ? container_file : "regular file",
                       provider_lib,
                       provider_file ? provider_file : "regular file",
                       result);
            break;

          case '=':
            if (result != 0)
              g_error ("Expected %s (%s) == %s (%s), got %d",
                       container_lib,
                       container_file ? container_file : "regular file",
                       provider_lib,
                       provider_file ? provider_file : "regular file",
                       result);
            break;

          default:
            g_assert_not_reached ();
        }

      g_free (container_file);
      g_free (container_lib);
      g_free (provider_file);
      g_free (provider_lib);
    }

  assert_with_errno (rm_rf (tmpdir));
  g_free (container);
  g_free (provider);
  g_free (tmpdir);
}

typedef struct
{
  const char *soname;
  char cmp;   // it compares such that "version1 cmp version2" is true
} CmpBySymbolsTest;

#ifdef ENABLE_SHARED
static const CmpBySymbolsTest cmp_by_symbols_tests[] =
{
  /* This adds one symbol and removes one symbol, so we can't tell which
   * was meant to be newer */
  { "libunversionedabibreak.so.1", '=' },
  { "libversionedabibreak.so.1", '=' },

  /* The only difference here is the tail of the filename, which this
   * comparator doesn't look at */
  { "libunversionednumber.so.1", '=' },
  { "libversionednumber.so.1", '=' },

  /* This is the situation this comparator handles */
  { "libunversionedsymbols.so.1", '<' },
  { "libversionedsymbols.so.1", '<' },
  { "libversionedupgrade.so.1", '<' },
  { "libversionedlikeglibc.so.1", '<' },

  /* We can't currently tell which one is newer because the private symbols
   * confuse us */
  { "libversionedlikedbus.so.1", '=' },

  { NULL }
};

static const CmpBySymbolsTest cmp_by_versions_tests[] =
{
  /* All of these have no symbol-versioning, so we can't tell a
   * difference with this comparator */
  { "libunversionedabibreak.so.1", '=' },
  { "libunversionednumber.so.1", '=' },
  { "libunversionedsymbols.so.1", '=' },

  /* This adds one verdef and removes one verdef, so we can't tell which
   * was meant to be newer */
  { "libversionedabibreak.so.1", '=' },

  /* The only difference here is the tail of the filename, which this
   * comparator doesn't look at */
  { "libversionednumber.so.1", '=' },

  /* This is simple "version ~= SONAME" symbol-versioning, like in libtiff
   * and libpng, so this comparator can't tell any difference */
  { "libversionedsymbols.so.1", '=' },

  /* This one has version-specific verdefs like libmount, libgcab, OpenSSL,
   * telepathy-glib etc., so we can tell it's an upgrade */
  { "libversionedupgrade.so.1", '<' },

  /* This one has the same symbol listed in more than one verdef,
   * like glibc - we can tell this is an upgrade */
  { "libversionedlikeglibc.so.1", '<' },

  /* We can't currently tell which one is newer because the private verdefs
   * confuse us */
  { "libversionedlikedbus.so.1", '=' },

  { NULL }
};

/* We use the same code to test cmp_by_symbols and cmp_by_versions,
 * just with a different table. */
static void
test_library_cmp_by_symbols (Fixture *f,
                             gconstpointer data)
{
  const CmpBySymbolsTest *tests = data;
  gsize i;

  for (i = 0; tests[i].soname != NULL; i++)
    {
      const CmpBySymbolsTest *test = &tests[i];
      gchar *v1;
      gchar *v1_lib;
      gchar *v2;
      gchar *v2_lib;
      int result;
      const char *how;

      v1 = g_build_filename (f->builddir, "tests", "version1", NULL);
      v1_lib = g_build_filename (v1, f->uninstalled ? ".libs" : ".",
                                 test->soname, NULL);
      v2 = g_build_filename (f->builddir, "tests", "version2", NULL);
      v2_lib = g_build_filename (v2, f->uninstalled ? ".libs" : ".",
                                 test->soname, NULL);

      if (tests == cmp_by_versions_tests)
        {
          how = "by symbol-versions";
          result = library_cmp_by_versions (test->soname,
                                            v1_lib, v1,
                                            v2_lib, v2);
        }
      else
        {
          how = "by symbols";
          result = library_cmp_by_symbols (test->soname,
                                           v1_lib, v1,
                                           v2_lib, v2);
        }

      switch (test->cmp)
        {
          case '<':
            if (result >= 0)
              g_error ("Expected %s < %s %s, got %d",
                       v1_lib, v2_lib, how, result);
            break;

          case '>':
            if (result <= 0)
              g_error ("Expected %s > %s %s, got %d",
                       v1_lib, v2_lib, how, result);
            break;

          case '=':
            if (result != 0)
              g_error ("Expected %s == %s %s, got %d",
                       v1_lib, v2_lib, how, result);
            break;

          default:
            g_assert_not_reached ();
        }

      /* We get the reverse result when we do it the other way round. */
      if (tests == cmp_by_versions_tests)
        result = library_cmp_by_versions (test->soname,
                                          v2_lib, v2,
                                          v1_lib, v1);
      else
        result = library_cmp_by_symbols (test->soname,
                                         v2_lib, v2,
                                         v1_lib, v1);

      switch (test->cmp)
        {
          case '>':   // v1 > v2, i.e. v2 < v1
            if (result >= 0)
              g_error ("Expected %s < %s %s, got %d",
                       v2_lib, v1_lib, how, result);
            break;

          case '<':   // v1 < v2, i.e. v2 > v1
            if (result <= 0)
              g_error ("Expected %s > %s %s, got %d",
                       v2_lib, v1_lib, how, result);
            break;

          case '=':
            if (result != 0)
              g_error ("Expected %s == %s %s, got %d",
                       v2_lib, v1_lib, how, result);
            break;

          default:
            g_assert_not_reached ();
        }


      g_free (v1);
      g_free (v1_lib);
      g_free (v2);
      g_free (v2_lib);
    }
}
#endif

static void
test_ptr_list (Fixture *f,
               gconstpointer data)
{
  size_t n;
  ptr_list *list;
  void **array;

  list = ptr_list_alloc (0);
  ptr_list_push_ptr (list, (char *) "hello");
  ptr_list_add_ptr (list, (char *) "world", g_str_equal);
  ptr_list_add_ptr (list, (char *) "hello", g_str_equal);    // duplicate, not added
  ptr_list_add_ptr (list, (char *) "world", g_str_equal);    // duplicate, not added
  ptr_list_push_ptr (list, (char *) "hello");
  ptr_list_push_ptr (list, NULL);
  ptr_list_push_addr (list, 23);

  g_assert_cmpstr (ptr_list_nth_ptr (list, 0), ==, "hello");
  g_assert_cmpstr (ptr_list_nth_ptr (list, 1), ==, "world");
  g_assert_cmpstr (ptr_list_nth_ptr (list, 2), ==, "hello");
  g_assert_cmpstr (ptr_list_nth_ptr (list, 3), ==, NULL);
  g_assert_cmpuint (GPOINTER_TO_SIZE (ptr_list_nth_ptr (list, 4)), ==, 23);
  g_assert_cmpstr (ptr_list_nth_ptr (list, 5), ==, NULL);
  g_assert_cmpstr (ptr_list_nth_ptr (list, 47), ==, NULL);
  g_assert_true (ptr_list_contains (list, 23));
  g_assert_true (ptr_list_contains (list, (ElfW(Addr)) ptr_list_nth_ptr (list, 1)));
  g_assert_false (ptr_list_contains (list, 1));

  array = ptr_list_free_to_array (list, &n);
  g_assert_cmpint (n, ==, 5);
  g_assert_cmpstr (array[0], ==, "hello");
  g_assert_cmpstr (array[1], ==, "world");
  g_assert_cmpstr (array[2], ==, "hello");
  g_assert_cmpstr (array[3], ==, NULL);
  g_assert_cmpuint (GPOINTER_TO_SIZE (array[4]), ==, 23);
  g_assert_cmpstr (array[5], ==, NULL);
  free (array);

  list = ptr_list_alloc (0);
  ptr_list_free (list);
}

static void
teardown (Fixture *f,
          gconstpointer data)
{
  g_free (f->srcdir);
  g_free (f->builddir);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  set_debug_flags (g_getenv("CAPSULE_DEBUG"));

  g_test_add ("/build-filename", Fixture, NULL, setup,
              test_build_filename, teardown);
  g_test_add ("/library-cmp/name", Fixture, NULL,
              setup, test_library_cmp_by_name, teardown);
#ifdef ENABLE_SHARED
  g_test_add ("/library-cmp/symbols", Fixture, cmp_by_symbols_tests,
              setup, test_library_cmp_by_symbols, teardown);
  g_test_add ("/library-cmp/versions", Fixture, cmp_by_versions_tests,
              setup, test_library_cmp_by_symbols, teardown);
#endif
  g_test_add ("/ptr-list", Fixture, NULL, setup, test_ptr_list, teardown);

  return g_test_run ();
}
