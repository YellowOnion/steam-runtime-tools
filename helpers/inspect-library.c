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

/*
 * This helper takes a SONAME as an argument, and optionally a filename for
 * symbols, and outputs a parsable JSON with the path, the dependencies and
 * the possible missing symbols of the requested library.
 *
 * For a usage example see `srt_check_library_presence` in
 * `steam-runtime-tools/library.c`.
 */

#include <argz.h>
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#define BASE "Base"

static void print_json_string_content (const char *s);
static void clean_exit (void *handle);
static bool has_symbol (void *handle, const char *symbol);
static bool has_versioned_symbol (void *handle,
                                  const char *symbol,
                                  const char * version);

static void oom (void) __attribute__((__noreturn__));
static void
oom (void)
{
  fprintf (stderr, "Out of memory");
  exit (EX_OSERR);
}

#define asprintf_or_die(...) \
do { \
    if (asprintf (__VA_ARGS__) < 0) \
      oom (); \
} while (0)

#define argz_add_or_die(...) \
do { \
    if (argz_add (__VA_ARGS__) != 0) \
      oom (); \
} while (0)

int
main (int argc,
      char **argv)
{
  const char *entry;
  const char *soname;
  const char *version;
  const char *symbol;
  void *handle = NULL;
  struct link_map *dep_map = NULL;
  struct link_map *the_library = NULL;
  FILE *fp;
  char *missing_symbols = NULL;
  char *misversioned_symbols = NULL;
  size_t missing_n = 0;
  size_t misversioned_n = 0;
  char *line = NULL;
  char *r = NULL;
  size_t len = 0;
  ssize_t chars;
  bool first;

  if (argc < 2 || argc > 3)
    {
      fprintf (stderr, "Expected, as argument, one SONAME and optionally a filename for symbols.\n");
      return 1;
    }

  soname = argv[1];
  printf ("{\n");
  printf ("  \"");
  print_json_string_content (soname);
  printf ("\": {");
  handle = dlopen (soname, RTLD_NOW);
  if (handle == NULL)
    {
      fprintf (stderr, "Unable to find the library: %s\n", dlerror ());
      clean_exit (handle);
      return 1;
    }
  /* Using RTLD_DI_LINKMAP insted of RTLD_DI_ORIGIN we don't need to worry
   * about allocating a big enough array for the path. */
  if (dlinfo (handle, RTLD_DI_LINKMAP, &the_library) != 0 || the_library == NULL)
    {
      fprintf (stderr, "Unable to obtain the path: %s\n", dlerror ());
      clean_exit (handle);
      return 1;
    }
  printf ("\n    \"path\": \"");
  print_json_string_content (the_library->l_name);
  printf ("\"");

  if (argc == 3)
    {
      fp = fopen(argv[2], "r");
      if (fp == NULL)
        {
          int saved_errno = errno;

          fprintf (stderr, "Error reading \"%s\": %s\n", argv[2], strerror (saved_errno));
          clean_exit (handle);
          return 1;
        }

      while ((chars = getline(&line, &len, fp)) != -1)
        {
          if (line[chars - 1] == '\n')
            line[chars - 1] = '\0';

          /* Skip any empty line */
          if (chars > 1)
            {
              r = line;
              symbol = strsep(&line, "@");
              version = strsep(&line, "@");
              if (symbol == NULL)
                {
                  fprintf (stderr, "Probably the symbol@version pair is mispelled.");
                  free (r);
                  free (missing_symbols);
                  free (misversioned_symbols);
                  clean_exit (handle);
                  return 1;
                }

              if (strcmp (version, BASE) == 0)
                {
                  if (!has_symbol (handle, symbol))
                    argz_add_or_die (&missing_symbols, &missing_n, symbol);
                }
              else
                {
                  if (!has_versioned_symbol (handle, symbol, version))
                    {
                      char * merged_string;
                      asprintf_or_die (&merged_string, "%s@%s", symbol, version);
                      if (has_symbol (handle, symbol))
                          argz_add_or_die (&misversioned_symbols, &misversioned_n, merged_string);
                      else
                          argz_add_or_die (&missing_symbols, &missing_n, merged_string);

                      free (merged_string);
                    }
                }
              free (r);
            }
        }
      free (line);
      fclose (fp);

      first = true;
      entry = 0;
      printf (",\n    \"missing_symbols\": [");
      while ((entry = argz_next (missing_symbols, missing_n, entry)))
        {
          if (first)
            first = false;
          else
            printf (",");

          printf ("\n      \"");
          print_json_string_content (entry);
          printf ("\"");
        }
      printf ("\n    ]");

      first = true;
      entry = 0;
      printf (",\n    \"misversioned_symbols\": [");
      while ((entry = argz_next (misversioned_symbols, misversioned_n, entry)))
        {
          if (first)
            first = false;
          else
            printf (",");

          printf ("\n      \"");
          print_json_string_content (entry);
          printf ("\"");
        }
      printf ("\n    ],");

      free (missing_symbols);
      free (misversioned_symbols);
    }
  dep_map = the_library;

  /* Some loaded libraries may be before our handle.
   * To list them all we move the pointer at the beginning. */
  while (dep_map != NULL && dep_map->l_prev != NULL)
    dep_map = dep_map->l_prev;

  printf ("\n    \"dependencies\": [");
  first = true;
  for (; dep_map != NULL; dep_map = dep_map->l_next)
    {
      if (dep_map == the_library || strcmp (dep_map->l_name, "") == 0)
        continue;

      if (first)
        {
          printf ("\n      \"");
          first = false;
        }
      else
        printf (",\n      \"");

      print_json_string_content (dep_map->l_name);
      printf ("\"");
    }

  printf ("\n    ]\n  }");
  clean_exit (handle);

  return 0;
}

static void
print_json_string_content (const char *s)
{
  const char *p;

  for (p = s; *p != '\0'; p++)
    {
      if (*p == '"' || *p == '\\' || *p <= 0x1F)
        printf ("\\u%04x", *p);
      else
        printf ("%c", *p);
    }
}

static void
clean_exit (void *handle)
{
    if (handle != NULL)
      dlclose (handle);

    printf ("\n}\n");
}

static bool
has_symbol (void *handle,
            const char *symbol)
{
  (void) dlerror ();  /* clear old error indicator */
  (void) dlsym (handle, symbol);
  return (dlerror () == NULL);
}

static bool
has_versioned_symbol (void *handle,
                      const char *symbol,
                      const char *version)
{
  (void) dlerror ();  /* clear old error indicator */
  (void) dlvsym (handle, symbol, version);
  return (dlerror () == NULL);
}
