/*
 * Copyright © 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "tests/test-utils.h"

#include <glib.h>
#include <glib/gprintf.h>

#include "libglnx.h"

#if !GLIB_CHECK_VERSION(2, 70, 0)
/*
 * Same as g_test_message(), but split messages with newlines into
 * multiple separate messages to avoid corrupting stdout, even in older
 * GLib versions that didn't do this
 */
void
_srt_test_message_safe (const char *format,
                        ...)
{
  g_autofree char *message = NULL;
  va_list ap;
  char *line;
  char *saveptr = NULL;

  va_start (ap, format);
  g_vasprintf (&message, format, ap);
  va_end (ap);

  for (line = strtok_r (message, "\n", &saveptr);
       line != NULL;
       line = strtok_r (NULL, "\n", &saveptr))
    (g_test_message) ("%s", line);
}
#endif
