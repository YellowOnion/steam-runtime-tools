/*
 * Copyright © 2020 Collabora Ltd.
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

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"

/*
 * Usage: copy the compiled executable into some random prefix and
 * run it.
 *
 * If you put it in /prefix/bin, /prefix/libexec or
 * /prefix/libexec/steam-runtime-tools-0, it should say that its prefix
 * is /prefix.
 */

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  const char *helpers = NULL;
  const char *prefix = _srt_find_myself (&helpers, &error);

  if (prefix == NULL)
    {
      g_warning ("Unable to find myself: %s", error->message);
      return 1;
    }

  g_print ("Prefix: %s\n", prefix);
  g_print ("Looking for helpers in: %s\n", helpers);
  return 0;
}
