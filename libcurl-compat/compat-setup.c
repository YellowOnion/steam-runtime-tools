/*
 * Copyright © 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 *
 * Configure the LD_LIBRARY_PATH Steam Runtime to use the most
 * widely-compatible libcurl ABI that we can.
 */

#include <libglnx.h>

#include <locale.h>
#include <sysexits.h>
#include <unistd.h>

#include <glib.h>
#include <gio/gio.h>
#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static void
log_compat (const char *description,
            SrtLibraryIssues issues,
            SrtLibrary *library)
{
  g_autoptr(GString) str = g_string_new ("");
  GFlagsClass *cls = g_type_class_ref (SRT_TYPE_LIBRARY_ISSUES);
  guint i;

  for (i = 0; i < cls->n_values; i++)
    {
      const GFlagsValue *val = &cls->values[i];

      if (val->value == 0)
        continue;

      if ((val->value & issues) == val->value)
        {
          if (str->len > 0)
            g_string_append_c (str, '|');

          g_string_append (str, val->value_nick);
        }
    }

  if (str->len == 0)
    g_string_append (str, "none");

  g_debug ("Incompatibilities between host %s and %s: %s",
           srt_library_get_requested_name (library),
           description,
           str->str);
  g_type_class_unref (cls);
}

/**
 * record_dependency:
 * @soname_symlink: Absolute path to a shared library's SONAME symlink such as
 *  /usr/lib/x86_64-linux-gnu/libcurl.so.4
 * @target: The `realpath()` of @soname_symlink
 * @runtime: `${STEAM_RUNTIME}`
 * @word_size: 32 or 64
 * @soname: SONAME of @soname_symlink
 */
static gboolean
record_dependency (const char *soname_symlink,
                   const char *target,
                   const char *runtime,
                   int word_size,
                   const char *soname,
                   GError **error)
{
  g_autofree gchar *pin_path = NULL;
  g_autofree gchar *contents = NULL;

  g_return_val_if_fail (soname_symlink != NULL, FALSE);
  g_return_val_if_fail (target != NULL, FALSE);
  g_return_val_if_fail (runtime != NULL, FALSE);
  g_return_val_if_fail (word_size == 32 || word_size == 64, FALSE);
  g_return_val_if_fail (soname != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  pin_path = g_strdup_printf ("%s/pinned_libs_%d/system_%s",
                              runtime, word_size, soname);
  contents = g_strdup_printf ("%s\n%s\n", soname_symlink, target);

  g_debug ("Recording dependency on system library \"%s\" -> \"%s\" in \"%s\"",
           soname_symlink, target, pin_path);

  if (!g_file_set_contents (pin_path, contents, -1, error))
    return FALSE;

  return TRUE;
}

/**
 * create_symlink:
 * @target: Path to a library. It may be absolute or relative to
 *  `${STEAM_RUNTIME}/pinned_libs_${word_size}`.
 * @runtime: `${STEAM_RUNTIME}`
 * @word_size: 32 or 64
 * @link_name: Name of symlink to create in
 *  `${STEAM_RUNTIME}/pinned_libs_${word_size}`
 *
 * Create a symlink in @runtime as if via `ln -fns`.
 */
static gboolean
create_symlink (const char *target,
                const char *runtime,
                int word_size,
                const char *link_name,
                GError **error)
{
  g_autofree gchar *full_path = NULL;

  g_return_val_if_fail (target != NULL, FALSE);
  g_return_val_if_fail (runtime != NULL, FALSE);
  g_return_val_if_fail (word_size == 32 || word_size == 64, FALSE);
  g_return_val_if_fail (link_name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  full_path = g_strdup_printf ("%s/pinned_libs_%d/%s",
                               runtime, word_size, link_name);

  g_debug ("Creating symlink \"%s\" -> \"%s\"", link_name, target);

  if (unlink (full_path) != 0 && errno != ENOENT)
    return glnx_throw_errno_prefix (error, "removing \"%s\"", full_path);

  if (symlink (target, full_path) != 0)
    return glnx_throw_errno_prefix (error, "creating symlink \"%s\" -> \"%s\"",
                                    full_path, target);

  return TRUE;
}

typedef enum
{
  ABI_I386,
  ABI_X86_64,
  N_ABIS
} Abi;

static const char *multiarch_tuples[] =
{
  SRT_ABI_I386,
  SRT_ABI_X86_64,
};

static int word_sizes[] =
{
  32,
  64,
};

static const char *suffixes[] =
{
  "",
  "-gnutls",
};

G_STATIC_ASSERT (G_N_ELEMENTS (multiarch_tuples) == N_ABIS);

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(FILE) original_stdout = NULL;
  g_autofree gchar *scout_expectations = NULL;
  g_autofree gchar *upstream_expectations = NULL;
  g_autoptr(SrtSystemInfo) scout_abi = NULL;
  g_autoptr(SrtSystemInfo) upstream_abi = NULL;
  const char *env;
  const char *runtime;
  gsize i, j;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    return FALSE;

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Path to LD_LIBRARY_PATH Steam Runtime is required");
      return FALSE;
    }

  if (argc > 2)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Exactly one positional parameter is required");
      return FALSE;
    }

  /* If we're already running under the Steam Runtime, escape from it
   * so that we can look at the system copy of libcurl. */

  env = g_getenv ("SYSTEM_LD_LIBRARY_PATH");

  if (env != NULL)
    {
      g_debug ("Resetting LD_LIBRARY_PATH to \"%s\"", env);
      g_setenv ("LD_LIBRARY_PATH", env, TRUE);
    }
  else
    {
      env = g_getenv ("LD_LIBRARY_PATH");

      if (env != NULL)
        g_debug ("Keeping LD_LIBRARY_PATH, \"%s\"", env);
      else
        g_debug ("LD_LIBRARY_PATH is not set");
    }

  runtime = argv[1];
  g_debug ("Using Steam Runtime in \"%s\"", runtime);
  scout_expectations = g_build_filename (runtime,
                                         "usr/lib/steamrt/expectations",
                                         NULL);
  upstream_expectations = g_build_filename (runtime,
                                            "usr/lib/steamrt/libcurl-compat/expectations",
                                            NULL);

  if (!g_file_test (scout_expectations, G_FILE_TEST_IS_DIR))
    return glnx_throw (error, "\"%s\" is not a directory", scout_expectations);

  if (!g_file_test (upstream_expectations, G_FILE_TEST_IS_DIR))
    return glnx_throw (error, "\"%s\" is not a directory", upstream_expectations);

  scout_abi = srt_system_info_new (scout_expectations);
  upstream_abi = srt_system_info_new (upstream_expectations);

  for (i = 0; i < N_ABIS; i++)
    {
      SrtLibraryIssues glibc_issues;
      const char *multiarch_tuple = multiarch_tuples[i];
      int word_size = word_sizes[i];

      glibc_issues = srt_system_info_check_library (upstream_abi,
                                                    multiarch_tuple,
                                                    "libc.so.6", NULL);

      if (glibc_issues == SRT_LIBRARY_ISSUES_NONE)
        {
          g_debug ("%s glibc is sufficiently new", multiarch_tuple);
        }
      else
        {
          if (glibc_issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD)
            {
              g_warning ("Cannot load %s glibc", multiarch_tuple);
            }
          else if (glibc_issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS)
            {
              g_debug ("%s glibc is too old to use libcurl compatibility shim",
                       multiarch_tuple);
            }
          else if (glibc_issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS)
            {
              g_warning ("%s glibc does not have expected symbol-versions",
                         multiarch_tuple);
            }
          else
            {
              g_warning ("Unable to use %s glibc for some reason",
                         multiarch_tuple);
            }

          continue;
        }

      for (j = 0; j < G_N_ELEMENTS (suffixes); j++)
        {
          g_autoptr(GError) local_error = NULL;
          const char *suffix = suffixes[j];
          g_autofree gchar *soname = NULL;
          g_autofree gchar *old_soname = NULL;
          g_autofree gchar *system = NULL;
          g_autofree gchar *shim_path = NULL;
          g_autoptr(SrtLibrary) library = NULL;
          g_autoptr(SrtLibrary) scout_compatible_library = NULL;
          SrtLibraryIssues upstream_abi_compat;
          SrtLibraryIssues scout_abi_compat;

          soname = g_strdup_printf ("libcurl%s.so.4", suffix);
          old_soname = g_strdup_printf ("libcurl%s.so.3", suffix);
          system = g_strdup_printf ("libsteam-runtime-system-libcurl%s.so.4", suffix);
          shim_path = g_strdup_printf ("../usr/lib/%s/libsteam-runtime-shim-libcurl%s.so.4",
                                       multiarch_tuple, suffix);
          upstream_abi_compat = srt_system_info_check_library (upstream_abi,
                                                               multiarch_tuple,
                                                               soname, &library);
          scout_abi_compat = srt_system_info_check_library (scout_abi,
                                                            multiarch_tuple,
                                                            soname, &scout_compatible_library);

          log_compat ("upstream libcurl ABI", upstream_abi_compat, library);
          log_compat ("scout libcurl ABI", scout_abi_compat,
                      scout_compatible_library);

          if (scout_abi_compat == SRT_LIBRARY_ISSUES_NONE)
            {
              /* The libcurl${suffix}.so.4 from the host system is
               * compatible with the ABI used in Debian circa 2012, and
               * therefore in scout. For example, this happens
               * for libcurl-gnutls.so.4 in modern Debian/Ubuntu, and
               * for libcurl.so.4 in old Debian/Ubuntu.
               *
               * It might also be compatible with upstream
               * libcurl${suffix}.so.4 by implementing both verdefs,
               * but in practice nobody does this (yet?). */
              const char *path = srt_library_get_absolute_path (scout_compatible_library);
              g_autofree char *real_path = realpath (path, NULL);

              g_debug ("Host system has scout-compatible %s at %s -> %s",
                       soname, path, real_path);

              /* Point the libcurl.so.4 (or similar) symlink directly to
               * the host system's library, which is compatible with ours,
               * but only if we can successfully record the dependency
               * first. */
              if (!record_dependency (path, real_path, runtime, word_size,
                                      soname, &local_error)
                  || !create_symlink (real_path, runtime, word_size, soname,
                                      &local_error)
                  || !create_symlink (real_path, runtime, word_size, old_soname,
                                      &local_error))
                {
                  g_warning ("%s", local_error->message);
                  g_clear_error (&local_error);
                }
            }
          else if (upstream_abi_compat == SRT_LIBRARY_ISSUES_NONE)
            {
              /* The libcurl${suffix}.so.4 from the host system is
               * (sufficiently) compatible with the upstream ABI. For
               * example, this happens for libcurl.so.4 in at least Arch
               * and Debian. */
              const char *path = srt_library_get_absolute_path (library);
              g_autofree char *real_path = realpath (path, NULL);

              g_debug ("Host system has upstream-compatible %s at %s -> %s",
                       soname, path, real_path);

              /* Point the libcurl.so.4 (or similar) symlink to the
               * shim library, which will load both our library and the
               * system library; but only do this if we can successfully
               * create the dependency file and the
               * libsteam-runtime-system-libcurl.so.4 symlink first,
               * otherwise it will fail at runtime. */
              if (!record_dependency (path, real_path, runtime, word_size,
                                      soname, &local_error)
                  || !create_symlink (real_path, runtime, word_size, system,
                                      &local_error)
                  || !create_symlink (shim_path, runtime, word_size, soname,
                                      &local_error)
                  || !create_symlink (shim_path, runtime, word_size, old_soname,
                                      &local_error))
                {
                  g_warning ("%s", local_error->message);
                  g_clear_error (&local_error);
                }
            }
          else
            {
              const char * const *iter;

              if (upstream_abi_compat & SRT_LIBRARY_ISSUES_CANNOT_LOAD)
                g_debug ("Cannot load host library %s", soname);
              else if (upstream_abi_compat & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS)
                g_debug ("Host library %s does not have all expected symbols",
                         soname);
              else if (upstream_abi_compat & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS)
                g_debug ("Host library %s does not have expected symbol-versions",
                         soname);
              else
                g_debug ("Unable to use host library %s for some reason",
                         soname);

              g_debug ("Diagnostic messages: %s",
                       srt_library_get_messages (library));

              for (iter = srt_library_get_missing_symbols (library);
                   iter != NULL && *iter != NULL;
                   iter++)
                g_debug ("Missing symbol: %s", *iter);

              for (iter = srt_library_get_misversioned_symbols (library);
                   iter != NULL && *iter != NULL;
                   iter++)
                g_debug ("Symbol present but version different or missing: %s", *iter);

              g_debug ("Falling back to setup.sh default behaviour of "
                       "always pinning %s", soname);
            }
        }
    }

  return TRUE;
}

static gboolean opt_print_version = FALSE;
static gboolean opt_verbose = FALSE;

static const GOptionEntry option_entries[] =
{
  { "verbose", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose", NULL },
  { "version", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { NULL }
};

int
main (int argc,
      char **argv)
{
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(GError) error = NULL;
  int status = EXIT_SUCCESS;

  setlocale (LC_ALL, "");
  g_set_prgname ("steam-runtime-libcurl-compat-setup");
  _srt_util_set_glib_log_handler (G_LOG_DOMAIN, FALSE);

  option_context = g_option_context_new ("$STEAM_RUNTIME");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      status = EX_USAGE;
      goto out;
    }

  if (opt_print_version)
    {
      g_print ("%s:\n"
               " Package: steam-runtime-tools\n"
               " Version: %s\n",
               g_get_prgname (), VERSION);
      goto out;
    }

  if (opt_verbose)
    _srt_util_set_glib_log_handler (G_LOG_DOMAIN, opt_verbose);

  if (!run (argc, argv, &error))
    {
      if (error->domain == G_OPTION_ERROR)
        status = EX_USAGE;
      else
        status = EXIT_FAILURE;
    }

out:
  if (status != EXIT_SUCCESS)
    g_printerr ("%s: %s\n", g_get_prgname (), error->message);

  g_clear_error (&error);
  g_clear_pointer (&option_context, g_option_context_free);
  return status;
}
