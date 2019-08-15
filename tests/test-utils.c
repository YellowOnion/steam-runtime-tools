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

#include "test-utils.h"

#include <ftw.h>
#include <glib.h>
#include <stdio.h>

static gint
ftw_remove (const gchar *path,
            const struct stat *sb,
            gint typeflags,
            struct FTW *ftwbuf)
{
  if (remove (path) < 0)
    return -1;

  return 0;
}

/**
 * rm_rf:
 * @directory: (type filename): The folder to remove.
 *
 * Recursively delete @directory within the same file system and
 * without following symbolic links.
 *
 * Returns: %TRUE if the removal was successful
 */
gboolean rm_rf (char *directory)
{
  g_return_val_if_fail (directory != NULL, FALSE);

  if (nftw (directory, ftw_remove, 10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0)
    return FALSE;

  return TRUE;
}