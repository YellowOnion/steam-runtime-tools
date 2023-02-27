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

static inline void oom (void) __attribute__((__noreturn__));
static inline void
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

void *steal_pointer (void *pp);
int steal_fd (int *fdp);
void clear_with_free (void *pp);
void clear_with_fclose (void *pp);

#define autofclose __attribute__((__cleanup__(clear_with_fclose)))
#define autofree __attribute__((__cleanup__(clear_with_free)))

void print_strescape (const char *bytestring);
void print_json_string_content (const char *s);
void print_array_entry (const char *entry,
                        const char *name_line_based,
                        bool *first);
void print_argz (const char *name,
                 const char *argz_values,
                 size_t argz_n,
                 bool line_based);
