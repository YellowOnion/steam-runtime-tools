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
#include <getopt.h>
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

enum
{
  OPTION_HELP = 1,
  OPTION_DEB_SYMBOLS,
  OPTION_HIDDEN_DEPENDENCY,
  OPTION_VERSION,
};

struct option long_options[] =
{
    { "hidden-dependency", required_argument, NULL, OPTION_HIDDEN_DEPENDENCY },
    { "deb-symbols", no_argument, NULL, OPTION_DEB_SYMBOLS },
    { "help", no_argument, NULL, OPTION_HELP },
    { "version", no_argument, NULL, OPTION_VERSION },
    { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS] SONAME [SYMBOLS_FILENAME]\n",
           program_invocation_short_name);
  exit (code);
}

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
  size_t len = 0;
  ssize_t chars;
  bool first;
  bool deb_symbols = false;
  size_t dependency_counter = 0;
  int opt;

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_HIDDEN_DEPENDENCY:
            dependency_counter++;
            break;

          case OPTION_DEB_SYMBOLS:
            deb_symbols = true;
            break;

          case OPTION_HELP:
            usage (0);
            break;

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                argv[0], VERSION);
            return 0;

          case '?':
          default:
            usage (1);
            break;  /* not reached */
        }
    }

  if (argc < optind + 1 || argc > optind + 2)
    {
      usage (1);
    }

  const char *hidden_deps[dependency_counter + 1];

  if (dependency_counter > 0)
    {
      /* Reset getopt */
      optind = 1;

      size_t i = 0;
      while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
        {
          switch (opt)
            {
              case OPTION_HIDDEN_DEPENDENCY:
                hidden_deps[i] = optarg;
                i++;
                break;

              default:
                break;
            }
        }
    }

  soname = argv[optind];
  printf ("{\n");
  printf ("  \"");
  print_json_string_content (soname);
  printf ("\": {");

  for (size_t i = 0; i < dependency_counter; i++)
    {
      /* We don't call "dlclose" on global hidden dependencies, otherwise ubsan
       * will report an indirect memory leak */
      if (dlopen (hidden_deps[i], RTLD_NOW|RTLD_GLOBAL) == NULL)
        {
          fprintf (stderr, "Unable to find the dependency library: %s\n", dlerror ());
          clean_exit (handle);
          return 1;
        }
    }

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

  if (argc >= optind + 2)
    {
      size_t soname_len = strlen (soname);
      bool found_our_soname = false;
      bool in_our_soname = false;

      if (strcmp(argv[optind + 1], "-") == 0)
        fp = stdin;
      else
        fp = fopen(argv[optind + 1], "r");

      if (fp == NULL)
        {
          int saved_errno = errno;

          fprintf (stderr, "Error reading \"%s\": %s\n",
                   argv[optind + 1], strerror (saved_errno));
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
              char *pointer_into_line;

              if (deb_symbols)
                {
                  if (line[0] == '#' || line[0] == '*' || line[0] == '|')
                    {
                      /* comment or metadata lines, ignore:
                       * "# comment"
                       * "* Field: Value"
                       * "| alternative-dependency" */
                      continue;
                    }
                  else if (line[0] == ' ')
                    {
                      /* this line represents a symbol:
                       * " symbol@Base 1.2-3~" */
                      if (!in_our_soname)
                        {
                          /* this is a symbol from a different library,
                           * ignore */
                          continue;
                        }
                    }
                  else
                    {
                      /* This line introduces a new SONAME, which might
                       * be the one we are interested in:
                       * "libz.so.1 zlib1g #MINVER#" */
                      if (strncmp (soname, line, soname_len) == 0
                          && (line[soname_len] == ' ' || line[soname_len] == '\t'))
                        {
                          found_our_soname = true;
                          in_our_soname = true;
                        }
                      else
                        {
                          in_our_soname = false;
                        }

                      /* This is not a symbol */
                      continue;
                    }

                  pointer_into_line = &line[1];
                }
              else
                {
                  pointer_into_line = line;
                }

              symbol = strsep (&pointer_into_line, "@");
              version = strsep (&pointer_into_line, deb_symbols ? "@ \t" : "@");
              if (symbol == NULL)
                {
                  fprintf (stderr, "Probably the symbol@version pair is mispelled.");
                  free (line);
                  free (missing_symbols);
                  free (misversioned_symbols);
                  clean_exit (handle);
                  return 1;
                }

              if (version == NULL || strcmp (version, BASE) == 0)
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
            }
        }
      free (line);
      fclose (fp);

      if (deb_symbols && !found_our_soname)
        {
          fprintf (stderr, "Warning: \"%s\" does not describe ABI of \"%s\"\n",
                   argv[optind + 1], soname);
        }

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
      printf ("\n    ]");

      free (missing_symbols);
      free (misversioned_symbols);
    }
  dep_map = the_library;

  /* Some loaded libraries may be before our handle.
   * To list them all we move the pointer at the beginning. */
  while (dep_map != NULL && dep_map->l_prev != NULL)
    dep_map = dep_map->l_prev;

  printf (",\n    \"dependencies\": [");
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
