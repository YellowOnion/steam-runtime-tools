/*
 * steam-runtime-launcher-interface-0 — convenience interface for compat tools
 *
 * Copyright © 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/*
 * Note that because this is on the critical path for running a game,
 * and because it doesn't actually do very much, it intentionally
 * does not depend on GLib or do non-trivial command-line parsing, etc.
 */

#include <argz.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NAME "steam-runtime-launcher-interface-0"

/* Chosen to be similar to env(1) */
enum
{
  LAUNCH_EX_USAGE = 125,
  LAUNCH_EX_FAILED = 125,
  LAUNCH_EX_CANNOT_INVOKE = 126,
  LAUNCH_EX_NOT_FOUND = 127,
  LAUNCH_EX_CANNOT_REPORT = 128
};

#if 0
#define trace(...) do { \
    fprintf (stderr, "%s: trace: ", NAME); \
    fprintf (stderr, __VA_ARGS__); \
    fprintf (stderr, "\n"); \
} while (0)
#else
#define trace(...) do { } while (0)
#endif

static int
usage (void)
{
  fprintf (stderr,
           "Usage: %s TOOL-NAME[:TOOL-NAME...] COMMAND [ARGUMENTS]\n",
           NAME);
  return LAUNCH_EX_USAGE;
}

static void
log_oom (void)
{
  fprintf (stderr, "%s: %s\n", NAME, strerror (ENOMEM));
}

static inline void
indirect_free (void *location)
{
  void **pointer_to_pointer = location;

  free (*pointer_to_pointer);
}

static void *
steal_pointer (void *location)
{
  void **pointer_to_pointer = location;
  void *value = *pointer_to_pointer;

  *pointer_to_pointer = NULL;
  return value;
}

#define autofree __attribute__((__cleanup__ (indirect_free)))

static bool
want_launcher_service (const char *tool_names)
{
  const char *requested = getenv ("STEAM_COMPAT_LAUNCHER_SERVICE");
  autofree char *needle = NULL;
  autofree char *haystack = NULL;

  trace ("Checking whether %s contains %s", tool_names, requested);

  if (requested == NULL)
    return false;

  if (strchr (requested, ':') != NULL)
    {
      fprintf (stderr,
               "%s: Expected a single entry in $STEAM_COMPAT_LAUNCHER_SERVICE\n",
               NAME);
      return false;
    }

  if (asprintf (&needle, ":%s:", requested) < 0)
    {
      log_oom ();
      return false;
    }

  if (asprintf (&haystack, ":%s:", tool_names) < 0)
    {
      log_oom ();
      return false;
    }

  if (strstr (haystack, needle) != NULL)
    return true;

  return false;
}

static void *
log_if_oom (void *copy)
{
  if (copy == NULL)
    log_oom ();

  return copy;
}

#define N_ELEMENTS(arr) (sizeof (arr) / sizeof (arr[0]))

static char *
find_launcher_service (void)
{
  static const char * const search_dirs[] =
  {
    "/run/pressure-vessel/pv-from-host/bin",
    "/usr/lib/pressure-vessel/from-host/bin",
  };
  size_t i;
  const char *env;
  autofree char *argz = NULL;
  size_t argz_len = 0;

  /* Check for environment variable override */
  env = getenv ("SRT_LAUNCHER_SERVICE");

  if (env != NULL
      && access (env, X_OK) == 0)
    return log_if_oom (strdup (env));

  /* Check for the version provided by pressure-vessel, which if anything
   * is probably newer than the one in the container's PATH */
  for (i = 0; i < N_ELEMENTS (search_dirs); i++)
    {
      autofree char *path = NULL;

      if (asprintf (&path, "%s/steam-runtime-launcher-service", search_dirs[i]) < 0)
        {
          log_oom ();
          return NULL;
        }

      if (access (path, X_OK) == 0)
        return steal_pointer (&path);
    }

  /* Check the PATH */
  env = getenv ("PATH");

  if (env != NULL)
    {
      char *next = NULL;

      if (argz_add_sep (&argz, &argz_len, env, ':') != 0)
        {
          log_oom ();
          return NULL;
        }

      for (next = argz_next (argz, argz_len, NULL);
           next != NULL;
           next = argz_next (argz, argz_len, next))
        {
          autofree char *path = NULL;

          if (asprintf (&path,
                        "%s/steam-runtime-launcher-service",
                        next) < 0)
            {
              log_oom ();
              return NULL;
            }

          if (access (path, X_OK) == 0)
            return steal_pointer (&path);
        }
    }

  argz_len = 0;
  free (steal_pointer (&argz));

  /* As a last resort, check in all Steam compat tools */
  env = getenv ("STEAM_COMPAT_TOOL_PATHS");

  if (env != NULL)
    {
      char *next = NULL;

      if (argz_add_sep (&argz, &argz_len, env, ':') != 0)
        {
          log_oom ();
          return NULL;
        }

      for (next = argz_next (argz, argz_len, NULL);
           next != NULL;
           next = argz_next (argz, argz_len, next))
        {
          autofree char *path = NULL;

          if (asprintf (&path,
                        "%s/pressure-vessel/bin/steam-runtime-launcher-service",
                        next) < 0)
            {
              log_oom ();
              return NULL;
            }

          if (access (path, X_OK) == 0)
            return steal_pointer (&path);
        }
    }

  argz_len = 0;
  free (steal_pointer (&argz));

  fprintf (stderr, "%s: Cannot find steam-runtime-launcher-service\n", NAME);
  return NULL;
}

int
main (int argc, char **argv)
{
  const char *tool_names;
  autofree char *launcher_service = NULL;
  int i;

  if (argc < 3)
    return usage ();

  if (argv[1][0] == '-')
    {
      fprintf (stderr, "%s does not accept any --options", NAME);
      return usage ();
    }

  if (argv[2][0] == '-')
    {
      fprintf (stderr, "%s does not accept any --options", NAME);
      return usage ();
    }

  tool_names = argv[1];

  trace ("Starting tool %s, wrapped program %s", tool_names, argv[2]);

  if (want_launcher_service (tool_names)
      && (launcher_service = find_launcher_service ()) != NULL)
    {
      autofree char *argz = NULL;
      size_t argz_len = 0;
      autofree char **launcher_service_argv = NULL;

      trace ("Trying to run launcher service: %s", launcher_service);

      if (argz_add (&argz, &argz_len, launcher_service) != 0
          || argz_add (&argz, &argz_len, "--exec-fallback")
          || argz_add (&argz, &argz_len, "--hint")
          || argz_add (&argz, &argz_len, "--no-stop-on-name-loss")
          || argz_add (&argz, &argz_len, "--replace")
          || argz_add (&argz, &argz_len, "--session")
          || argz_add (&argz, &argz_len, "--"))
        {
          log_oom ();
          goto fallback;
        }

      for (i = 2; i < argc; i++)
        {
          if (argz_add (&argz, &argz_len, argv[i]) != 0)
            {
              log_oom ();
              goto fallback;
            }
        }

      launcher_service_argv = calloc (argz_count (argz, argz_len) + 1,
                                      sizeof (char *));

      if (launcher_service_argv == NULL)
        {
          log_oom ();
          goto fallback;
        }

      unsetenv ("STEAM_COMPAT_LAUNCHER_SERVICE");
      argz_extract (argz, argz_len, launcher_service_argv);
      execvp (launcher_service, launcher_service_argv);
      fprintf (stderr, "%s: execvp %s: %s\n",
               NAME, launcher_service, strerror (errno));

fallback:
      fprintf (stderr,
               "%s: Cannot run launcher service, falling back to "
               "running command without it\n",
               NAME);
    }

  execvp (argv[2], &argv[2]);

  if (errno == ENOENT)
    return LAUNCH_EX_NOT_FOUND;

  return LAUNCH_EX_CANNOT_INVOKE;
}
