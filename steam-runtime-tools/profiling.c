/*
 * Copyright © 2021 Collabora Ltd.
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

#include "steam-runtime-tools/profiling-internal.h"

#include <errno.h>
#include <sys/times.h>

/* If strictly positive, profiling is enabled. */
static long profiling_ticks_per_sec = 0;

/*
 * Enable time measurement and profiling messages.
 */
void
_srt_profiling_enable (void)
{
  errno = 0;
  profiling_ticks_per_sec = sysconf (_SC_CLK_TCK);

  if (profiling_ticks_per_sec <= 0)
    g_warning ("Unable to enable profiling: %s", g_strerror (errno));
  else
    g_message ("Enabled profiling");
}

struct _SrtProfilingTimer
{
  char *message;
  clock_t wallclock;
  struct tms cpu;
};

/*
 * Start a time measurement. Must be paired with _srt_profiling_end(),
 * unless %NULL is returned.
 *
 * Returns: (transfer full): an object representing the start time,
 *  or %NULL if profiling is disabled
 */
SrtProfilingTimer *
_srt_profiling_start (const char *format,
                      ...)
{
  SrtProfilingTimer *ret;
  va_list args;

  if (profiling_ticks_per_sec <= 0)
    return NULL;

  ret = g_new0 (SrtProfilingTimer, 1);
  va_start (args, format);
  ret->message = g_strdup_vprintf (format, args);
  va_end (args);

  g_message ("Profiling: start: %s", ret->message);
  ret->wallclock = times (&ret->cpu);
  return ret;
}

/*
 * @start: (nullable) (transfer full): The start of the measurement
 *
 * Finish a time measurement and log how much real (wallclock) time,
 * user CPU time and system CPU type was taken.
 *
 * A `g_autoptr(SrtProfilingTimer)` automatically ends profiling when the
 * timer goes out of scope.
 */
void
_srt_profiling_end (SrtProfilingTimer *start)
{
  clock_t end;
  struct tms end_cpu;

  if (start == NULL)
    return;

  end = times (&end_cpu);

  g_message ("Profiling: end (real %.1fs, user %.1fs, sys %.1fs): %s",
           (end - start->wallclock) / (double) profiling_ticks_per_sec,
           (end_cpu.tms_utime
            + end_cpu.tms_cutime
            - start->cpu.tms_utime
            - start->cpu.tms_cutime) / (double) profiling_ticks_per_sec,
           (end_cpu.tms_stime
            + end_cpu.tms_cstime
            - start->cpu.tms_stime
            - start->cpu.tms_cstime) / (double) profiling_ticks_per_sec,
           start->message);
  g_free (start->message);
  g_free (start);
}
