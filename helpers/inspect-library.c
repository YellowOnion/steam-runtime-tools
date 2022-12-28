/*
 * Copyright © 2019-2022 Collabora Ltd.
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
#include <fcntl.h>
#include <getopt.h>
#include <link.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <assert.h>
#include <unistd.h>

#include <libelf.h>
#include <gelf.h>

#define BASE "Base"

static void print_json_string_content (const char *s);
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

static inline void *
malloc_or_die (size_t size)
{
  void *p = malloc (size);

  if (size != 0 && p == NULL)
    oom ();

  return p;
}

static inline void *
steal_pointer (void *pp)
{
    typedef void *__attribute__((may_alias)) voidp_alias;
    voidp_alias *pointer_to_pointer = pp;
    void *ret = *pointer_to_pointer;
    *pointer_to_pointer = NULL;
    return ret;
}

static inline int
steal_fd (int *fdp)
{
  int fd = *fdp;
  *fdp = -1;
  return fd;
}

static void
clear_with_free (void *pp)
{
  free (steal_pointer (pp));
}

static void
clear_with_freev (void *pp)
{
  size_t i;
  char **str_array = steal_pointer (pp);

  for (i = 0; str_array != NULL && str_array[i] != NULL; i++)
    free (str_array[i]);

  free (str_array);
}

static void
clear_with_dlclose (void *pp)
{
  void *handle = steal_pointer (pp);

  if (handle != NULL)
    dlclose (handle);
}

static void
clear_with_fclose (void *pp)
{
  FILE *fh = steal_pointer (pp);

  if (fh != NULL)
    fclose (fh);
}

static void
close_fd (void *pp)
{
  int fd = steal_fd (pp);

  if (fd >= 0)
    close (fd);
}

static void
close_elf (void *pp)
{
  Elf *elf = steal_pointer (pp);

  if (elf != NULL)
    elf_end (elf);
}

#define autodlclose __attribute__((__cleanup__(clear_with_dlclose)))
#define autofclose __attribute__((__cleanup__(clear_with_fclose)))
#define autofreev __attribute__((__cleanup__(clear_with_freev)))
#define autofree __attribute__((__cleanup__(clear_with_free)))
#define autofd __attribute__((__cleanup__(close_fd)))
#define autoelf __attribute__((__cleanup__(close_elf)))

enum
{
  OPTION_HELP = 1,
  OPTION_DEB_SYMBOLS,
  OPTION_HIDDEN_DEPENDENCY,
  OPTION_LINE_BASED,
  OPTION_VERSION,
};

struct option long_options[] =
{
    { "hidden-dependency", required_argument, NULL, OPTION_HIDDEN_DEPENDENCY },
    { "deb-symbols", no_argument, NULL, OPTION_DEB_SYMBOLS },
    { "help", no_argument, NULL, OPTION_HELP },
    { "line-based", no_argument, NULL, OPTION_LINE_BASED },
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

static const char *
find_dyn_entry (const ElfW(Dyn) *entries,
                const ElfW(Addr) base,
                const ElfW(Sxword) tag)
{
  const ElfW(Dyn) *entry;
  ElfW(Addr) stab = 0;

  for (entry = entries; entry->d_tag != DT_NULL; entry++)
    {
      if (entry->d_tag == tag)
        stab = entry->d_un.d_ptr;
    }

  if (stab == 0)
    return NULL;
  else if (stab < base)
    return (const char *) base + stab;
  else
    return (const char *) stab;
}

static size_t
find_tag_value (const ElfW(Dyn) *dyn,
                const ElfW(Sxword) tag)
{
  for (; dyn->d_tag != DT_NULL; dyn++)
    {
      if (dyn->d_tag == tag)
        return dyn->d_un.d_val;
    }

  return (size_t) -1;
}

/*
 * Print a bytestring to stdout, escaping backslashes and control
 * characters in octal. The result can be parsed with g_strcompress().
 */
static void
print_strescape (const char *bytestring)
{
  const unsigned char *p;

  for (p = (const unsigned char *) bytestring; *p != '\0'; p++)
    {
      if (*p < ' ' || *p >= 0x7f || *p == '\\')
        printf ("\\%03o", *p);
      else
        putc (*p, stdout);
    }
}

/*
 * Print an element as either a line based or, if @name_line_based
 * is %NULL, as an entry in a JSON array.
 */
static void
print_array_entry (const char *entry,
                   const char *name_line_based,
                   bool *first)
{
  assert (entry != NULL);
  assert (first != NULL);

  if (*first)
    *first = false;
  else if (name_line_based == NULL)
    printf (",");

  if (name_line_based == NULL)
    {
      printf ("\n      \"");
      print_json_string_content (entry);
      printf ("\"");
    }
  else
    {
      fprintf (stdout, "%s=", name_line_based);
      print_strescape (entry);
      putc ('\n', stdout);
    }
}

/*
 * Print an array in stdout as either a formatted JSON entry or
 * a line based
 */
static void
print_argz (const char *name,
            const char *argz_values,
            size_t argz_n,
            bool line_based)
{
  const char *entry = 0;
  bool first = true;

  if (!line_based)
    printf (",\n    \"%s\": [", name);

  while ((entry = argz_next (argz_values, argz_n, entry)))
    print_array_entry (entry, line_based ? name : NULL, &first);

  if (!line_based)
    printf ("\n    ]");
}

static int
bsearch_strcmp_cb (const void *n, const void *ip)
{
  const char *needle = n;
  const char * const *item_p = ip;
  return strcmp (needle, *item_p);
}

static int
qsort_strcmp_cb (const void *s1, const void *s2)
{
  const char * const *a = (const char * const *) s1;
  const char * const *b = (const char * const *) s2;
  return strcmp (*a, *b);
}

/*
 * get_versions:
 * @elf: The object's elf of which we want to get the versions
 * @versions_count: (out) (not nullable): The number of versions found
 *
 * Returns: (array zero-terminated=1) (transfer full): A %NULL-terminated
 *  list of version definitions that @elf has, or %NULL on failure.
 *  If the object is unversioned, @versions_count will be set to zero and
 *  an array with with a single %NULL element will be returned.
 */
static char **
get_versions (Elf *elf,
              size_t *versions_count)
{
  Elf_Scn *scn = NULL;
  Elf_Data *data;
  GElf_Shdr shdr_mem;
  GElf_Shdr *shdr = NULL;
  bool found_verdef = false;
  uintptr_t verdef_ptr = 0;
  size_t phnum;
  size_t sh_entsize;
  size_t i;
  size_t versions_n = 0;
  GElf_Verdef def_mem;
  GElf_Verdef *def;
  size_t auxoffset;
  size_t offset = 0;
  autofree char *versions_argz = NULL;
  autofreev char **versions = NULL;
  const char *entry = 0;

  assert (elf != NULL);
  assert (versions_count != NULL);

  *versions_count = 0;

  if (elf_getphdrnum (elf, &phnum) < 0)
    {
      fprintf (stderr, "Unable to determine the number of program headers: %s\n",
               elf_errmsg (elf_errno ()));
      return NULL;
    }

  for (i = 0; i < phnum; i++)
    {
      GElf_Phdr phdr_mem;
      GElf_Phdr *phdr = gelf_getphdr (elf, i, &phdr_mem);
      if (phdr != NULL && phdr->p_type == PT_DYNAMIC)
        {
          scn = gelf_offscn (elf, phdr->p_offset);
          shdr = gelf_getshdr (scn, &shdr_mem);
          if (shdr == NULL)
            {
              fprintf (stderr, "Unable to get the section header: %s\n",
                       elf_errmsg (elf_errno ()));
              return NULL;
            }
          break;
        }
    }

  if (shdr == NULL)
    {
      fprintf (stderr, "Unable to find the section header\n");
      return NULL;
    }

  data = elf_getdata (scn, NULL);
  if (data == NULL)
    {
      fprintf (stderr, "Unable to get the dynamic section data: %s\n",
               elf_errmsg (elf_errno ()));
      return NULL;
    }

  sh_entsize = gelf_fsize (elf, ELF_T_DYN, 1, EV_CURRENT);
  for (i = 0; i < shdr->sh_size / sh_entsize; i++)
    {
      GElf_Dyn dyn_mem;
      GElf_Dyn *dyn = gelf_getdyn (data, i, &dyn_mem);
      if (dyn == NULL)
        break;

      if (dyn->d_tag == DT_VERDEF)
        {
          verdef_ptr = dyn->d_un.d_ptr;
          found_verdef = true;
          break;
        }
    }

  if (!found_verdef)
    {
      /* The version definition table is not available */
      versions = malloc_or_die (sizeof (char *));
      versions[0] = NULL;
      return steal_pointer (&versions);
    }

  scn = gelf_offscn (elf, verdef_ptr);
  data = elf_getdata (scn, NULL);
  if (data == NULL)
    {
      fprintf (stderr, "Unable to get symbols data: %s\n", elf_errmsg (elf_errno ()));
      return NULL;
    }

  def = gelf_getverdef (data, 0, &def_mem);
  while (def != NULL)
  {
    GElf_Verdaux aux_mem;
    GElf_Verdaux *aux;
    const char *version;

    auxoffset = offset + def->vd_aux;
    offset += def->vd_next;

    /* The first Verdaux array must exist and it points to the version
     * definition string that Verdef defines. Every possible additional
     * Verdaux arrays are the dependencies of said version definition.
     * In our case we don't need to list the dependencies, so we just
     * get the first Verdaux of every Verdef. */
    aux = gelf_getverdaux (data, auxoffset, &aux_mem);

    if (aux == NULL)
      continue;
    version = elf_strptr (elf, shdr->sh_link, aux->vda_name);
    if (version == NULL)
      continue;

    if ((def->vd_flags & VER_FLG_BASE) == 0)
      argz_add_or_die (&versions_argz, &versions_n, version);

    if (def->vd_next == 0)
      def = NULL;
    else
      def = gelf_getverdef (data, offset, &def_mem);
  }

  *versions_count = argz_count (versions_argz, versions_n);

  /* Make space for an additional %NULL terminator */
  versions = malloc_or_die (sizeof (char *) * ((*versions_count) + 1));

  for (i = 0; i < *versions_count; i++)
    {
      entry = argz_next (versions_argz, versions_n, entry);
      versions[i] = strdup (entry);
    }

  /* Add a final %NULL terminator */
  versions[*versions_count] = NULL;
  qsort (versions, *versions_count, sizeof (char *), qsort_strcmp_cb);

  return steal_pointer (&versions);
}

/*
 * open_elf:
 * @file_path: (type filename): Path to a library
 * @fdp: (out) (not nullable): Used to return a file descriptor of the opened library
 * @elfp: (out) (not nullable): Used to return an initialized Elf of the library
 *
 * Returns: %TRUE if the Elf has been opened correctly
 */
static bool
open_elf (const char *file_path,
          int *fdp,
          Elf **elfp)
{
  autofd int fd = -1;
  autoelf Elf *elf = NULL;

  assert (file_path != NULL);
  assert (fdp != NULL);
  assert (*fdp < 0);
  assert (elfp != NULL);
  assert (*elfp == NULL);

  if (elf_version (EV_CURRENT) == EV_NONE)
    {
      fprintf (stderr, "elf_version(EV_CURRENT): %s\n",
               elf_errmsg (elf_errno ()));
      return false;
    }

  if ((fd = open (file_path, O_RDONLY | O_CLOEXEC, 0)) < 0)
    {
      fprintf (stderr, "failed to open: %s\n", file_path);
      return false;
    }

  if ((elf = elf_begin (fd, ELF_C_READ, NULL)) == NULL)
    {
      fprintf (stderr, "Error reading library \"%s\": %s\n",
               file_path, elf_errmsg (elf_errno ()));
      return false;
    }

  *fdp = steal_fd (&fd);
  *elfp = steal_pointer (&elf);

  return true;
}

int
main (int argc,
      char **argv)
{
  const char *soname;
  const char *version;
  const char *symbol;
  autodlclose void *handle = NULL;
  struct link_map *dep_map = NULL;
  struct link_map *the_library = NULL;
  autofclose FILE *fp = NULL;
  autofree char *missing_symbols = NULL;
  autofree char *misversioned_symbols = NULL;
  autofree char *missing_versions = NULL;
  size_t missing_n = 0;
  size_t misversioned_n = 0;
  size_t missing_versions_n = 0;
  autofree char *line = NULL;
  size_t len = 0;
  ssize_t chars;
  bool first;
  bool deb_symbols = false;
  int opt;
  autofree char *hidden_deps = NULL;
  size_t hidden_deps_len = 0;
  const char *hidden_dep;
  bool line_based = false;

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_HIDDEN_DEPENDENCY:
            argz_add_or_die (&hidden_deps, &hidden_deps_len, optarg);
            break;

          case OPTION_DEB_SYMBOLS:
            deb_symbols = true;
            break;

          case OPTION_HELP:
            usage (0);
            break;

          case OPTION_LINE_BASED:
            line_based = true;
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

  soname = argv[optind];

  if (line_based)
    {
      fputs ("requested=", stdout);
      print_strescape (soname);
      putc ('\n', stdout);
    }
  else
    {
      printf ("{\n");
      printf ("  \"");
      print_json_string_content (soname);
      printf ("\": {");
    }

  for (hidden_dep = argz_next (hidden_deps, hidden_deps_len, NULL);
       hidden_dep != NULL;
       hidden_dep = argz_next (hidden_deps, hidden_deps_len, hidden_dep))
    {
      /* We don't call "dlclose" on global hidden dependencies, otherwise ubsan
       * will report an indirect memory leak */
      if (dlopen (hidden_dep, RTLD_NOW|RTLD_GLOBAL) == NULL)
        {
          fprintf (stderr, "Unable to find the dependency library: %s\n", dlerror ());
          return 1;
        }
    }

  handle = dlopen (soname, RTLD_NOW);
  if (handle == NULL)
    {
      fprintf (stderr, "Unable to find the library: %s\n", dlerror ());
      return 1;
    }
  /* Using RTLD_DI_LINKMAP insted of RTLD_DI_ORIGIN we don't need to worry
   * about allocating a big enough array for the path. */
  if (dlinfo (handle, RTLD_DI_LINKMAP, &the_library) != 0 || the_library == NULL)
    {
      fprintf (stderr, "Unable to obtain the path: %s\n", dlerror ());
      return 1;
    }

  const ElfW(Dyn) * const dyn_start = the_library->l_ld;
  const ElfW(Addr) load_addr = the_library->l_addr;
  const char *strtab = find_dyn_entry (dyn_start, load_addr, DT_STRTAB);
  size_t soname_val = find_tag_value (dyn_start, DT_SONAME);
  if (strtab != NULL && soname_val != (size_t) -1)
    {
      if (line_based)
        {
          fputs ("soname=", stdout);
          print_strescape (&strtab[soname_val]);
          putc ('\n', stdout);
        }
      else
        {
          printf ("\n    \"SONAME\": \"");
          print_json_string_content (&strtab[soname_val]);
          printf("\",");
        }
    }
  else
    {
      fprintf (stderr,
               "Warning: we were not able to get the SONAME of \"%s\"\n",
               soname);
    }

  if (line_based)
    {
      fputs ("path=", stdout);
      print_strescape (the_library->l_name);
      putc ('\n', stdout);
    }
  else
    {
      printf ("\n    \"path\": \"");
      print_json_string_content (the_library->l_name);
      printf ("\"");
    }

  if (argc >= optind + 2)
    {
      size_t soname_len = strlen (soname);
      bool found_our_soname = false;
      bool in_our_soname = false;
      autofd int fd = -1;
      autoelf Elf *soname_elf = NULL;
      size_t versions_count = 0;
      autofreev char **versions = NULL;
      bool unexpectedly_unversioned = false;

      if (strcmp(argv[optind + 1], "-") == 0)
        fp = stdin;
      else
        fp = fopen(argv[optind + 1], "r");

      if (fp == NULL)
        {
          int saved_errno = errno;

          fprintf (stderr, "Error reading \"%s\": %s\n",
                   argv[optind + 1], strerror (saved_errno));
          return 1;
        }

      if (!open_elf (the_library->l_name, &fd, &soname_elf))
        return 1;

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
                  fprintf (stderr, "Probably the symbol@version pair is misspelled.\n");
                  return 1;
                }

              if (version == NULL || strcmp (version, BASE) == 0)
                {
                  if (!has_symbol (handle, symbol))
                    argz_add_or_die (&missing_symbols, &missing_n, symbol);
                }
              else
                {
                  if (strcmp (symbol, version) == 0)
                    {
                      /* dlsym() and dlvsym() don't find the
                       * special symbol representing the version itself,
                       * because it is neither data nor code.
                       * Instead, we manually look for the version in the
                       * header's verdef. */
                      const char *found = NULL;

                      if (versions == NULL)
                        {
                          versions = get_versions (soname_elf, &versions_count);
                          if (versions == NULL)
                            return 1;
                        }

                      if (versions_count == 0)
                        unexpectedly_unversioned = true;
                      else
                        found = bsearch (version, versions, versions_count,
                                         sizeof (char *), bsearch_strcmp_cb);

                      if (found == NULL)
                        argz_add_or_die (&missing_versions, &missing_versions_n, version);
                    }
                  else if (!has_versioned_symbol (handle, symbol, version))
                    {
                      autofree char * merged_string = NULL;

                      asprintf_or_die (&merged_string, "%s@%s", symbol, version);
                      if (has_symbol (handle, symbol))
                          argz_add_or_die (&misversioned_symbols, &misversioned_n, merged_string);
                      else
                          argz_add_or_die (&missing_symbols, &missing_n, merged_string);
                    }
                }
            }
        }

      if (deb_symbols && !found_our_soname)
        {
          fprintf (stderr, "Warning: \"%s\" does not describe ABI of \"%s\"\n",
                   argv[optind + 1], soname);
        }

      if (unexpectedly_unversioned)
       {
        if (line_based)
          printf ("unexpectedly_unversioned=true\n");
        else
          printf (",\n    \"unexpectedly_unversioned\": true");
       }

      print_argz (line_based ? "missing_symbol" : "missing_symbols",
                  missing_symbols, missing_n, line_based);

      print_argz (line_based ? "misversioned_symbol" : "misversioned_symbols",
                  misversioned_symbols, misversioned_n, line_based);

      print_argz (line_based ? "missing_version" : "missing_versions",
                  missing_versions, missing_versions_n, line_based);
    }
  dep_map = the_library;

  /* Some loaded libraries may be before our handle.
   * To list them all we move the pointer at the beginning. */
  while (dep_map != NULL && dep_map->l_prev != NULL)
    dep_map = dep_map->l_prev;

  if (!line_based)
    printf (",\n    \"dependencies\": [");

  first = true;
  for (; dep_map != NULL; dep_map = dep_map->l_next)
    {
      if (dep_map == the_library || strcmp (dep_map->l_name, "") == 0)
        continue;

      print_array_entry (dep_map->l_name, line_based ? "dependency" : NULL, &first);
    }

  if (!line_based)
    {
      printf ("\n    ]\n  }");
      printf ("\n}\n");
    }

  return 0;
}

static void
print_json_string_content (const char *s)
{
  const unsigned char *p;

  for (p = (const unsigned char *) s; *p != '\0'; p++)
    {
      if (*p == '"' || *p == '\\' || *p <= 0x1F || *p >= 0x80)
        printf ("\\u%04x", *p);
      else
        printf ("%c", *p);
    }
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
