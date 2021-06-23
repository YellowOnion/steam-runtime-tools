/*
 * Copyright © 2021 Collabora Ltd.
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

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gelf.h>
#include <libelf.h>
#include <sysexits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/utils-internal.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable */
static GPtrArray *nftw_libraries = NULL;

static gchar *opt_directory = FALSE;
static gboolean opt_ldconfig = FALSE;
static gboolean opt_print0 = FALSE;
static gboolean opt_print_version = FALSE;
static gboolean opt_skip_unversioned = FALSE;

static const GOptionEntry option_entries[] =
{
  { "directory", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME,
    &opt_directory, "Check the word size for the libraries recursively found in this directory",
    NULL },
  { "ldconfig", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_ldconfig, "Check the word size for the libraries listed in ldconfig", NULL },
  { "print0", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_print0, "The generated library=value pairs are terminated with a "
    "null character instead of a newline", NULL },
  { "skip-unversioned", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_skip_unversioned, "Skip the libraries that have a filename that end with "
    "just \".so\"", NULL },
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { NULL }
};

static gint
list_libraries_helper (const char *fpath,
                       const struct stat *sb,
                       int typeflag,
                       struct FTW *ftwbuf)
{
  if (typeflag == FTW_SL)
    {
      if (strstr (fpath, ".so.") != NULL
          || (!opt_skip_unversioned && g_str_has_suffix (fpath, ".so")))
        g_ptr_array_add (nftw_libraries, g_strdup (fpath));
    }
  return 0;
}

static void
print_library_details (const gchar *library_path,
                       const gchar separator,
                       FILE *original_stdout)
{
  glnx_autofd int fd = -1;
  g_autoptr(Elf) elf = NULL;
  int class = ELFCLASSNONE;
  const gchar *identifier = NULL;

  GElf_Ehdr eh;

  if ((fd = open (library_path, O_RDONLY | O_CLOEXEC, 0)) < 0)
    {
      int saved_errno = errno;
      g_debug ("Error reading \"%s\": %s\n",
               library_path, strerror (saved_errno));
      return;
    }

    if ((elf = elf_begin (fd, ELF_C_READ, NULL)) == NULL)
      {
        g_debug ("Error reading the library ELF: %s", elf_errmsg (elf_errno ()));
        return;
      }

    if (gelf_getehdr(elf, &eh) == NULL)
      {
        g_debug ("Error reading the library ELF header: %s",
                 elf_errmsg (elf_errno ()));
        return;
      }

    class = gelf_getclass (elf);

    if (class == ELFCLASS32 && eh.e_machine == EM_386)
      identifier = "i386-linux-gnu";
    else if (class == ELFCLASS32 && eh.e_machine == EM_X86_64)
      identifier = "x86_64-linux-gnux32";
    else if (class == ELFCLASS64 && eh.e_machine == EM_X86_64)
      identifier = "x86_64-linux-gnu";
    else
      identifier = "?";

    fprintf (original_stdout, "%s=%s%c", library_path, identifier, separator);
}

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(FILE) original_stdout = NULL;
  g_autofree gchar *output = NULL;
  gint wait_status = 0;
  gsize i;
  const gchar *ldconfig_argv[] =
    {
      "/sbin/ldconfig", "-XNv", NULL,
    };
  char separator = '\n';

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    return FALSE;

  if (opt_print0)
    separator = '\0';

  if (elf_version (EV_CURRENT) == EV_NONE)
    return glnx_throw (error, "elf_version(EV_CURRENT): %s", elf_errmsg (elf_errno ()));

  if (opt_ldconfig)
    {
      g_auto(GStrv) ldconfig_entries = NULL;
      g_autofree gchar *library_prefix = NULL;

      if (!g_spawn_sync (NULL,   /* working directory */
                        (gchar **) ldconfig_argv,
                        NULL,    /* envp */
                        G_SPAWN_SEARCH_PATH,
                        NULL,    /* child setup */
                        NULL,    /* user data */
                        &output, /* stdout */
                        NULL,    /* stderr */
                        &wait_status,
                        error))
        {
          return FALSE;
        }

      if (wait_status != 0)
        return glnx_throw (error, "Cannot run ldconfig: wait status %d", wait_status);

      if (output == NULL)
        return glnx_throw (error, "ldconfig didn't produce anything in output");

      ldconfig_entries = g_strsplit (output, "\n", -1);

      if (ldconfig_entries == NULL)
        return glnx_throw (error, "ldconfig didn't produce anything in output");

      for (i = 0; ldconfig_entries[i] != NULL; i++)
        {
          g_auto(GStrv) line_elements = NULL;
          const gchar *library = NULL;
          const gchar *colon = NULL;
          g_autofree gchar *library_path = NULL;

          /* skip empty lines */
          if (ldconfig_entries[i][0] == '\0')
            continue;

          colon = strchr (ldconfig_entries[i], ':');

          if (colon != NULL)
            {
              g_clear_pointer (&library_prefix, g_free);
              library_prefix = g_strndup (ldconfig_entries[i], colon - ldconfig_entries[i]);
              continue;
            }

          line_elements = g_strsplit (ldconfig_entries[i], " -> ", 2);
          library = g_strstrip (line_elements[0]);
          library_path = g_build_filename (library_prefix, library, NULL);

          print_library_details (library_path, separator, original_stdout);
        }
    }
  else if (opt_directory != NULL)
    {
      g_autofree gchar *real_directory = NULL;

      nftw_libraries = g_ptr_array_new_full (512, g_free);
      real_directory = realpath (opt_directory, NULL);

      if (real_directory == NULL)
        return glnx_throw_errno_prefix (error, "Unable to realpath \"%s\"", opt_directory);

      if (nftw (real_directory, list_libraries_helper, 10, FTW_DEPTH|FTW_PHYS) < 0)
        {
          g_ptr_array_free (nftw_libraries, TRUE);
          return glnx_throw_errno_prefix (error, "Unable to iterate through \"%s\"", opt_directory);
        }

      for (i = 0; i < nftw_libraries->len; i++)
        print_library_details (g_ptr_array_index (nftw_libraries, i), separator,
                               original_stdout);

      g_ptr_array_free (nftw_libraries, TRUE);
    }

  return TRUE;
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(GError) error = NULL;
  int status = EXIT_SUCCESS;

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      status = EX_USAGE;
      goto out;
    }

  if (opt_print_version)
    {
      /* Output version number as YAML for machine-readability,
       * inspired by `ostree --version` and `docker version` */
      g_print ("%s:\n"
               " Package: steam-runtime-tools\n"
               " Version: %s\n",
               g_get_prgname (), VERSION);
      goto out;
    }

  if (opt_ldconfig && opt_directory != NULL)
    {
      glnx_throw (&error, "--ldconfig and --directory cannot be used at the same time");
      status = EX_USAGE;
      goto out;
    }

  if (!opt_ldconfig && opt_directory == NULL)
    {
      glnx_throw (&error, "Either --ldconfig or --directory are required");
      status = EX_USAGE;
      goto out;
    }

  if (!run (argc, argv, &error))
    status = EXIT_FAILURE;

out:
  if (status != EXIT_SUCCESS)
    g_printerr ("%s: %s\n", g_get_prgname (), error->message);

  return status;
}
