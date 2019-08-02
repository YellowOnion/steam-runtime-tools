/*<private_header>*/
/*
 * Copyright © 2019 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <glib.h>

#if !GLIB_CHECK_VERSION(2, 44, 0)
#define g_steal_pointer(x) _srt_steal_pointer (x)
/* A simplified version of g_steal_pointer without type-safety. */
static inline gpointer
_srt_steal_pointer (gpointer pointer_to_pointer)
{
  gpointer *pp = pointer_to_pointer;
  gpointer ret;

  ret = *pp;
  *pp = NULL;
  return ret;
}
#endif

#if !GLIB_CHECK_VERSION(2, 34, 0)
#define g_clear_pointer(x, destroy) \
  _srt_clear_pointer (x, (GDestroyNotify) (void (*)(void)) destroy)
/* A simplified version of g_clear_pointer without type-safety. */
static inline void
_srt_clear_pointer (gpointer pointer_to_pointer,
                    GDestroyNotify destroy)
{
  gpointer *pp = pointer_to_pointer;
  gpointer p = g_steal_pointer (pp);

  if (p != NULL)
    destroy (p);
}
#endif
