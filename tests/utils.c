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
  library_cmp_function *comparators;

  g_assert_no_error (error);
  g_assert_nonnull (tmpdir);

  comparators = library_cmp_list_from_string ("name", ",", NULL, NULL);
  g_assert_nonnull (comparators);

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

      result = library_cmp_list_iterate (comparators,
                                         test->soname,
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
  free (comparators);
}

typedef struct
{
  const char *soname;
  const char *spec;
  /* It compares such that "version1 cmp version2" is true, except for
   * the special case that "c" and "p" mean we choose the container version
   * and provider version respectively */
  char cmp;
} CmpTest;

#ifdef ENABLE_SHARED
static const CmpTest cmp_tests[] =
{
  /* BY SYMBOLS */

  /* This adds one symbol and removes one symbol, so we can't tell which
   * was meant to be newer */
  { "libunversionedabibreak.so.1", "symbols", '=' },
  { "libversionedabibreak.so.1", "symbols", '=' },

  /* The only difference here is the tail of the filename, which this
   * comparator doesn't look at */
  { "libunversionednumber.so.1", "symbols", '=' },
  { "libversionednumber.so.1", "symbols", '=' },

  /* This is the situation this comparator handles */
  { "libunversionedsymbols.so.1", "symbols", '<' },
  { "libversionedsymbols.so.1", "symbols", '<' },
  { "libversionedupgrade.so.1", "symbols", '<' },
  { "libversionedlikeglibc.so.1", "symbols", '<' },

  /* We can't currently tell which one is newer because the private symbols
   * confuse us */
  { "libversionedlikedbus.so.1", "symbols", '=' },

  /* BY VERSION DEFINITIONS */

  /* All of these have no symbol-versioning, so we can't tell a
   * difference with this comparator */
  { "libunversionedabibreak.so.1", "versions", '=' },
  { "libunversionednumber.so.1", "versions", '=' },
  { "libunversionedsymbols.so.1", "versions", '=' },

  /* This adds one verdef and removes one verdef, so we can't tell which
   * was meant to be newer */
  { "libversionedabibreak.so.1", "versions", '=' },

  /* The only difference here is the tail of the filename, which this
   * comparator doesn't look at */
  { "libversionednumber.so.1", "versions", '=' },

  /* This is simple "version ~= SONAME" symbol-versioning, like in libtiff
   * and libpng, so this comparator can't tell any difference */
  { "libversionedsymbols.so.1", "versions", '=' },

  /* This one has version-specific verdefs like libmount, libgcab, OpenSSL,
   * telepathy-glib etc., so we can tell it's an upgrade */
  { "libversionedupgrade.so.1", "versions", '<' },

  /* This one has the same symbol listed in more than one verdef,
   * like glibc - we can tell this is an upgrade */
  { "libversionedlikeglibc.so.1", "versions", '<' },

  /* We can't currently tell which one is newer because the private verdefs
   * confuse us */
  { "libversionedlikedbus.so.1", "versions", '=' },

  /* BY NAME */

  /* These have the version number in the filename. */
  { "libunversionednumber.so.1", "name", '<' },
  { "libversionedlikedbus.so.1", "name", '<' },
  { "libversionednumber.so.1", "name", '<' },

  /* These have the same filename in both versions, so we can't tell. */
  { "libunversionedabibreak.so.1", "name", '=' },
  { "libunversionedsymbols.so.1", "name", '=' },
  { "libversionedabibreak.so.1", "name", '=' },
  { "libversionedsymbols.so.1", "name", '=' },
  { "libversionedupgrade.so.1", "name", '=' },
  { "libversionedlikeglibc.so.1", "name", '=' },

  /* BY MORE THAN ONE FACTOR */

  { "libversionedlikeglibc.so.1", "name,versions", '<' },
  { "libversionedlikeglibc.so.1", "name,container", 'c' },
  { "libversionedlikeglibc.so.1", "name,container,provider", 'c' },
  { "libversionedlikeglibc.so.1", "name,provider", 'p' },
  { "libversionedlikeglibc.so.1", "name,provider,container", 'p' },
  { "libversionedlikeglibc.so.1", "versions,name", '<' },
  { "libversionedlikeglibc.so.1", "versions,container", '<' },
  { "libversionedlikeglibc.so.1", "versions,provider", '<' },
  { "libversionedlikeglibc.so.1", "name,symbols", '<' },
  { "libversionedlikeglibc.so.1", "symbols,provider", '<' },

  /* This one is a stand-in for libgcc_s.so.1 */
  { "libversionedupgrade.so.1", "name", '=' },
  { "libversionedupgrade.so.1", "versions,name,symbols", '<' },
  { "libversionedupgrade.so.1", "name,versions", '<' },

  /* These are obviously silly - just assert that they don't crash. */
  { "libversionedupgrade.so.1", "", '=' },
  { "libversionedupgrade.so.1", ",,,,,,", '=' },
  { "libversionedupgrade.so.1", "name,,,,,,name,,,,,,,,,,,,,,,,,,,", '=' },
  { "libversionedupgrade.so.1",
    "name,name,name,name,name,name,name,name,name,name,name,name,name,name",
    '=' },
};

static void
test_library_cmp (Fixture *f,
                  gconstpointer data)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (cmp_tests); i++)
    {
      const CmpTest *test = &cmp_tests[i];
      library_cmp_function *comparators;
      gchar *v1;
      gchar *v1_lib;
      gchar *v2;
      gchar *v2_lib;
      int result;

      comparators = library_cmp_list_from_string (test->spec, ",", NULL, NULL);
      g_assert_nonnull (comparators);

      v1 = g_build_filename (f->builddir, "tests", "version1", NULL);
      v1_lib = g_build_filename (v1, f->uninstalled ? ".libs" : ".",
                                 test->soname, NULL);
      v2 = g_build_filename (f->builddir, "tests", "version2", NULL);
      v2_lib = g_build_filename (v2, f->uninstalled ? ".libs" : ".",
                                 test->soname, NULL);

      result = library_cmp_list_iterate (comparators,
                                         test->soname,
                                         v1_lib, v1,
                                         v2_lib, v2);

      switch (test->cmp)
        {
          case '<':   // v1 < v2
          case 'p':   // choose provider, i.e. v2 here
            if (result >= 0)
              g_error ("Expected %s < %s by (%s), got %d",
                       v1_lib, v2_lib, test->spec, result);
            break;

          case '>':   // v1 > v2
          case 'c':   // choose container, i.e. v1 here
            if (result <= 0)
              g_error ("Expected %s > %s by (%s), got %d",
                       v1_lib, v2_lib, test->spec, result);
            break;

          case '=':
            if (result != 0)
              g_error ("Expected %s == %s by (%s), got %d",
                       v1_lib, v2_lib, test->spec, result);
            break;

          default:
            g_assert_not_reached ();
        }

      /* We get the reverse result when we do it the other way round -
       * unless the expected result is "c" or "p", in which case we
       * get the same result, because those results are determined by
       * the position of the arguments rather than by their content. */
      result = library_cmp_list_iterate (comparators,
                                         test->soname,
                                         v2_lib, v2,
                                         v1_lib, v1);

      switch (test->cmp)
        {
          case '>':   // v1 > v2, i.e. v2 < v1
          case 'p':   // choose provider, i.e. v1 here
            if (result >= 0)
              g_error ("Expected %s < %s by (%s), got %d",
                       v2_lib, v1_lib, test->spec, result);
            break;

          case '<':   // v1 < v2, i.e. v2 > v1
          case 'c':   // choose container, i.e. v2 here
            if (result <= 0)
              g_error ("Expected %s > %s by (%s), got %d",
                       v2_lib, v1_lib, test->spec, result);
            break;

          case '=':
            if (result != 0)
              g_error ("Expected %s == %s by (%s), got %d",
                       v2_lib, v1_lib, test->spec, result);
            break;

          default:
            g_assert_not_reached ();
        }


      g_free (v1);
      g_free (v1_lib);
      g_free (v2);
      g_free (v2_lib);
      free (comparators);
    }
}
#endif

char library_ini_part_1[] =
"# Configuration for capsule-capture-libs\n"
"\n"
"[Library libc.so.6]\n"
"CompareBy=versions;symbols;name;\n"
"\n"
"[Library libgcc_s.so.1]\n"
"CompareBy=versions;\n"
"\n"
"[Library libwhatsmynameagain.so.0]\n"
"CompareBy=name;\n"
"\n"
"[Library libcountmysymbols.so.0]\n"
"CompareBy=symbols;\n"
"\n"
"[Not a library]\n"
"# Ignore this\n"
"CompareBy=By guesswork; by magic; randomly\n"
"whatever=Whatever\n"
"\n"
"[Library libdbus-1.so.3]\n"
"\n"
"[Library libdbus-1.so.3]\n"
"CompareBy=provider;container;\n"
"";

char library_ini_part_2[] =
"[Library libglib-2.0.so.0]\n"
"\n"
"[Library libdbus-1.so.3]\n"
"CompareBy=versions;name;symbols;\n"
"private-symbols=_*;dbus_internal_do_not_use_*;\n"
"private-versions=LIBDBUS_PRIVATE_*;\n"
"";

static void
test_library_knowledge_good (Fixture *f,
                             gconstpointer data)
{
  library_knowledge knowledge = LIBRARY_KNOWLEDGE_INIT;
  const library_details *libc;
  const library_details *libgcc;
  const library_details *name;
  const library_details *symbols;
  const library_details *libdbus;
  const library_details *glib;
  const library_details *nope;
  int code = -1;
  char *message = NULL;
  FILE *fh;

  assert_with_errno ((fh = fmemopen ((char *) library_ini_part_1,
                                     sizeof (library_ini_part_1), "r")) != NULL);

  if (!library_knowledge_load_from_stream (&knowledge, fh, "library.ini.1",
                                           &code, &message))
    g_error ("%s", message);

  g_assert_cmpstr (message, ==, NULL);
  g_assert_cmpuint (code, ==, -1);
  assert_with_errno (fclose (fh) == 0);

  assert_with_errno ((fh = fmemopen ((char *) library_ini_part_2,
                                     sizeof (library_ini_part_2), "r")) != NULL);

  if (!library_knowledge_load_from_stream (&knowledge, fh, "library.ini.2",
                                           &code, &message))
    g_error ("%s", message);

  g_assert_cmpstr (message, ==, NULL);
  g_assert_cmpuint (code, ==, -1);
  assert_with_errno (fclose (fh) == 0);

  libc = library_knowledge_lookup (&knowledge, "libc.so.6");
  g_assert_nonnull (libc);
  g_assert_cmpstr (libc->name, ==, "libc.so.6");
  libgcc = library_knowledge_lookup (&knowledge, "libgcc_s.so.1");
  g_assert_nonnull (libgcc);
  g_assert_cmpstr (libgcc->name, ==, "libgcc_s.so.1");
  name = library_knowledge_lookup (&knowledge, "libwhatsmynameagain.so.0");
  g_assert_nonnull (name);
  g_assert_cmpstr (name->name, ==, "libwhatsmynameagain.so.0");
  symbols = library_knowledge_lookup (&knowledge, "libcountmysymbols.so.0");
  g_assert_nonnull (symbols);
  g_assert_cmpstr (symbols->name, ==, "libcountmysymbols.so.0");
  libdbus = library_knowledge_lookup (&knowledge, "libdbus-1.so.3");
  g_assert_nonnull (libdbus);
  g_assert_cmpstr (libdbus->name, ==, "libdbus-1.so.3");
  glib = library_knowledge_lookup (&knowledge, "libglib-2.0.so.0");
  g_assert_nonnull (glib);
  g_assert_cmpstr (libdbus->name, ==, "libdbus-1.so.3");
  nope = library_knowledge_lookup (&knowledge, "libdbus-glib-1.so.2");
  g_assert_null (nope);

  g_assert_null (glib->comparators);
  g_assert_nonnull (name->comparators);
  g_assert_nonnull (name->comparators[0]);
  g_assert_null (name->comparators[1]);
  g_assert_nonnull (symbols->comparators);
  g_assert_nonnull (symbols->comparators[0]);
  g_assert_null (symbols->comparators[1]);
  g_assert_nonnull (libgcc->comparators);
  g_assert_nonnull (libgcc->comparators[0]);
  g_assert_null (libgcc->comparators[1]);

  g_assert_nonnull (libc->comparators);
  g_assert_true (libc->comparators[0] == libgcc->comparators[0]);
  g_assert_true (libc->comparators[1] == symbols->comparators[0]);
  g_assert_true (libc->comparators[2] == name->comparators[0]);
  g_assert_null (libc->comparators[3]);

  g_assert_nonnull (libdbus->comparators);
  g_assert_true (libdbus->comparators[0] == libgcc->comparators[0]);
  g_assert_true (libdbus->comparators[1] == name->comparators[0]);
  g_assert_true (libdbus->comparators[2] == symbols->comparators[0]);
  g_assert_null (libdbus->comparators[3]);

  library_knowledge_clear (&knowledge);
}

static const char * const library_knowledge_bad_strings[] =
{
  "nope",
  "[nope",
  "foo=bar",
  "[nope]\n[no",
};

static void
test_library_knowledge_bad (Fixture *f,
                            gconstpointer data)
{
  gsize i;
  library_knowledge knowledge = LIBRARY_KNOWLEDGE_INIT;

  for (i = 0; i < G_N_ELEMENTS (library_knowledge_bad_strings); i++)
    {
      const char *s = library_knowledge_bad_strings[i];
      int code = -1;
      char *message = NULL;
      FILE *fh;
      gchar *description;
      bool ok;

      description = g_strescape (s, NULL);
      assert_with_errno ((fh = fmemopen ((char *) s, strlen (s), "r")) != NULL);

      ok = library_knowledge_load_from_stream (&knowledge, fh, description,
                                               &code, &message);
      g_assert_cmpstr (message, !=, NULL);
      g_assert_cmpuint (code, !=, -1);
      g_assert_false (ok);
      g_test_message ("code %d: %s", code, message);
      free (message);
      assert_with_errno (fclose (fh) == 0);
      library_knowledge_clear (&knowledge);

      assert_with_errno ((fh = fmemopen ((char *) s, strlen (s), "r")) != NULL);
      ok = library_knowledge_load_from_stream (&knowledge, fh, description,
                                               NULL, NULL);
      g_assert_false (ok);
      assert_with_errno (fclose (fh) == 0);
      library_knowledge_clear (&knowledge);

      g_free (description);
    }
}

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
#ifdef ENABLE_SHARED
  g_test_add ("/library-cmp/configurable", Fixture, NULL,
              setup, test_library_cmp, teardown);
#endif
  g_test_add ("/library-cmp/name", Fixture, NULL,
              setup, test_library_cmp_by_name, teardown);
  g_test_add ("/library-knowledge/bad", Fixture, NULL,
              setup, test_library_knowledge_bad, teardown);
  g_test_add ("/library-knowledge/good", Fixture, NULL,
              setup, test_library_knowledge_good, teardown);
  g_test_add ("/ptr-list", Fixture, NULL, setup, test_ptr_list, teardown);

  return g_test_run ();
}
