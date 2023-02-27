/*
 * Copyright © 2019-2023 Collabora Ltd.
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

#include "inspect-library-utils.h"

void *
steal_pointer (void *pp)
{
    typedef void *__attribute__((may_alias)) voidp_alias;
    voidp_alias *pointer_to_pointer = pp;
    void *ret = *pointer_to_pointer;
    *pointer_to_pointer = NULL;
    return ret;
}

int
steal_fd (int *fdp)
{
  int fd = *fdp;
  *fdp = -1;
  return fd;
}

void
clear_with_free (void *pp)
{
  free (steal_pointer (pp));
}

void
clear_with_fclose (void *pp)
{
  FILE *fh = steal_pointer (pp);

  if (fh != NULL)
    fclose (fh);
}

/*
 * Print a bytestring to stdout, escaping backslashes and control
 * characters in octal. The result can be parsed with g_strcompress().
 */
void
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

void
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

/*
 * Print an element as either a line based or, if @name_line_based
 * is %NULL, as an entry in a JSON array.
 */
void
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
void
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
