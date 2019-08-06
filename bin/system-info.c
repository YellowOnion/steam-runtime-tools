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
 * Output basic information about the system on which the tool is run.
 * The output is a JSON object with the following keys:
 *
 * can-write-uinput:
 *   The values are boolean: true if we can write to `/dev/uninput`,
 *   false if we are not able to do it.
 *
 * architectures:
 *   An object. The keys are multiarch tuples like %SRT_ABI_I386,
 *   as used in Debian and the freedesktop.org SDK runtime.
 *   The values are objects with more details of the architecture:
 *
 *   can-run:
 *     The values are boolean: true if we can definitely run
 *     executables of this architecture, or false if we cannot prove
 *     that we can do so.
 *
 *   library-issues-summary:
 *     A string array listing all the libraries problems that has been found
 *     in the running system. Possible values can be: "cannot-load",
 *     "missing-symbols", "misversioned-symbols" and "internal-error".
 *     If "can-run" for this architecture is false we skip the library check
 *     and this "library-issues-summary" will not be printed at all.
 *
 *   library-details:
 *     An object. The keys are library SONAMEs, such as `libjpeg.so.62`.
 *     The values are objects with more details of the library:
 *
 *     path:
 *       The value is a string representing the full path about where the
 *       @library has been found. The value is `null` if the library is
 *       not available in the system.
 *
 *     issues:
 *       A string array listing all the @library problems that has been
 *       found in the running system. For possible values check
 *       `library-issues-summary`. This object will be available only if
 *       there are some issues.
 *
 *     missing-symbols:
 *       A string array listing all the symbols that were expected to be
 *       provided by @library but were not found. This object will be
 *       available only if there are sone missing symbols.
 *
 *     misversioned-symbols:
 *       A string array listing all the symbols that were expected to be
 *       provided by @library but were available with a different version.
 *       This object will be available only if there are some misversioned
 *       symbols.
 */

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <glib.h>

#include <json-glib/json-glib.h>

enum
{
  OPTION_HELP = 1,
  OPTION_EXPECTATION,
  OPTION_VERBOSE,
};

struct option long_options[] =
{
    { "expectations", required_argument, NULL, OPTION_EXPECTATION },
    { "verbose", no_argument, NULL, OPTION_VERBOSE },
    { "help", no_argument, NULL, OPTION_HELP },
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

  fprintf (fp, "Usage: %s [OPTIONS]\n",
           program_invocation_short_name);
  exit (code);
}

static FILE *
divert_stdout_to_stderr (GError **error)
{
  int original_stdout_fd;
  FILE *original_stdout;

  /* Duplicate the original stdout so that we still have a way to write
   * machine-readable output. */
  original_stdout_fd = dup (STDOUT_FILENO);

  if (original_stdout_fd < 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to duplicate fd %d: %s",
                   STDOUT_FILENO, g_strerror (saved_errno));
      return NULL;
    }

  /* If something like g_debug writes to stdout, make it come out of
   * our original stderr. */
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    {
      int saved_errno = errno;

      close (original_stdout_fd);
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to make fd %d a copy of fd %d: %s",
                   STDOUT_FILENO, STDERR_FILENO, g_strerror (saved_errno));
      return NULL;
    }

  /* original_stdout takes ownership of original_stdout_fd on success */
  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    {
      int saved_errno = errno;

      close (original_stdout_fd);
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to create a stdio wrapper for fd %d: %s",
                   original_stdout_fd, g_strerror (saved_errno));
      return NULL;
    }

  return original_stdout;
}

static void
jsonify_library_issues (JsonBuilder *builder,
                        SrtLibraryIssues issues)
{
  if ((issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD) != 0)
    json_builder_add_string_value (builder, "cannot-load");

  if ((issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS) != 0)
    json_builder_add_string_value (builder, "missing-symbols");

  if ((issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS) != 0)
    json_builder_add_string_value (builder, "misversioned-symbols");

  if ((issues & SRT_LIBRARY_ISSUES_INTERNAL_ERROR) != 0)
    json_builder_add_string_value (builder, "internal-error");
}

static void
print_libraries_details (JsonBuilder *builder,
                         GList *libraries,
                         gboolean verbose)
{
  json_builder_set_member_name (builder, "library-details");
  json_builder_begin_object (builder);
  for (GList *l = libraries; l != NULL; l = l->next)
    {
      if (verbose || srt_library_get_issues (l->data) != SRT_LIBRARY_ISSUES_NONE)
        {
          const char * const *missing_symbols;
          const char * const *misversioned_symbols;
          json_builder_set_member_name (builder, srt_library_get_soname (l->data));
          json_builder_begin_object (builder);
          json_builder_set_member_name (builder, "path");
          json_builder_add_string_value (builder, srt_library_get_absolute_path (l->data));

          if (srt_library_get_issues (l->data) != SRT_LIBRARY_ISSUES_NONE)
            {
              json_builder_set_member_name (builder, "issues");
              json_builder_begin_array (builder);
              jsonify_library_issues (builder, srt_library_get_issues (l->data));
              json_builder_end_array (builder);
            }

          missing_symbols = srt_library_get_missing_symbols (l->data);
          if (missing_symbols[0] != NULL)
            {
              json_builder_set_member_name (builder, "missing-symbols");
              json_builder_begin_array (builder);
              for (gsize i = 0; missing_symbols[i] != NULL; i++)
                json_builder_add_string_value (builder, missing_symbols[i]);
              json_builder_end_array (builder);
            }

          misversioned_symbols = srt_library_get_misversioned_symbols (l->data);
          if (misversioned_symbols[0] != NULL)
            {
              json_builder_set_member_name (builder, "misversioned-symbols");
              json_builder_begin_array (builder);
              for (gsize i = 0; misversioned_symbols[i] != NULL; i++)
                json_builder_add_string_value (builder, misversioned_symbols[i]);
              json_builder_end_array (builder);
            }

          json_builder_end_object (builder);
        }
    }
  json_builder_end_object (builder);

  return;
}

int
main (int argc,
      char **argv)
{
  FILE *original_stdout = NULL;
  GError *error = NULL;
  SrtSystemInfo *info;
  SrtLibraryIssues issues = SRT_LIBRARY_ISSUES_NONE;
  char *expectations = NULL;
  gboolean verbose = FALSE;
  JsonBuilder *builder;
  JsonGenerator *generator;
  gboolean can_run = FALSE;
  gchar *json_output;
  GList *libraries = NULL;
  GList *detailed_errors = NULL;
  int opt;
  static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64 };

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_EXPECTATION:
            expectations = optarg;
            break;

          case OPTION_VERBOSE:
            verbose = TRUE;
            break;

          case OPTION_HELP:
            usage (0);
            break;

          case '?':
          default:
            usage (1);
            break;  /* not reached */
        }
    }

  if (optind != argc)
    usage (1);

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = divert_stdout_to_stderr (&error);

  if (original_stdout == NULL)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return 1;
    }

  info = srt_system_info_new (expectations);

  builder = json_builder_new ();
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "can-write-uinput");
  json_builder_add_boolean_value (builder, srt_system_info_can_write_to_uinput (info));

  json_builder_set_member_name (builder, "architectures");
  json_builder_begin_object (builder);

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      json_builder_set_member_name (builder, multiarch_tuples[i]);
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, "can-run");
      can_run = srt_system_info_can_run (info, multiarch_tuples[i]);
      json_builder_add_boolean_value (builder, can_run);

      if (can_run && expectations != NULL)
        {
          json_builder_set_member_name (builder, "library-issues-summary");
          json_builder_begin_array (builder);
          issues = srt_system_info_check_libraries (info,
                                                    multiarch_tuples[i],
                                                    &libraries);
          jsonify_library_issues (builder, issues);
          json_builder_end_array (builder);
        }

      if (libraries != NULL && (issues != SRT_LIBRARY_ISSUES_NONE || verbose))
          print_libraries_details (builder, libraries, verbose);

      json_builder_end_object (builder);
      g_list_free_full (detailed_errors, g_free);
      g_list_free_full (libraries, g_object_unref);
    }

  json_builder_end_object (builder);
  json_builder_end_object (builder);

  JsonNode *root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_output = json_generator_to_data (generator, NULL);

  if (fputs (json_output, original_stdout) < 0)
    g_warning ("Unable to write output: %s", g_strerror (errno));

  if (fputs ("\n", original_stdout) < 0)
    g_warning ("Unable to write final newline: %s", g_strerror (errno));

  if (fclose (original_stdout) != 0)
    g_warning ("Unable to close stdout: %s", g_strerror (errno));

  g_free (json_output);
  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);
  g_object_unref (info);

  return 0;
}
