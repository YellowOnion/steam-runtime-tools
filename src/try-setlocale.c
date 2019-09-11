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

#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Please do not add non-glibc dependencies here: this tool is designed
 * to work outside the Steam Runtime environment. */

enum
{
  OPTION_HELP = 1
};

struct option long_options[] =
{
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

  fprintf (fp, "Usage: %s [LOCALE]\n",
           program_invocation_short_name);
  exit (code);
}

int
main (int argc,
      char **argv)
{
  const char *locale_name;
  const char *locale_result;
  int opt;

  while ((opt = getopt_long (argc, argv, "", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_HELP:
            usage (0);
            break;

          case '?':
          default:
            usage (2);
            break;  /* not reached */
        }
    }

  if (argc < optind || argc > optind + 1)
    {
      usage (2);
    }

  if (argc > optind)
    locale_name = argv[optind];
  else
    locale_name = "";

  locale_result = setlocale (LC_ALL, locale_name);

  if (locale_result == NULL)
    {
      int saved_errno = errno;

      fprintf (stderr, "setlocale \"%s\": %s\n",
               locale_name, strerror (saved_errno));
      return 1;
    }

  return 0;
}
