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

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <glib-object.h>

/**
 * SECTION:architecture
 * @title: Architectures
 * @short_description: CPU architectures and ABIs
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * On a typical x86 PC, it might be possible to run 32-bit and/or 64-bit
 * executables, depending on the capabilities of the CPU, OS kernel and
 * operating system.
 */

gboolean
_srt_architecture_can_run (gchar **envp,
                           const char *helpers_path,
                           const char *multiarch)
{
  gchar *helper = NULL;
  GPtrArray *argv = NULL;
  int exit_status = -1;
  GError *error = NULL;
  gboolean ret = FALSE;
  GStrv my_environ = NULL;
  const gchar *ld_preload;
  gchar *filtered_preload = NULL;

  g_return_val_if_fail (envp != NULL, FALSE);
  g_return_val_if_fail (multiarch != NULL, FALSE);
  g_return_val_if_fail (_srt_check_not_setuid (), FALSE);

  argv = _srt_get_helper (helpers_path, multiarch, "true",
                          SRT_HELPER_FLAGS_NONE, &error);
  if (argv == NULL)
    {
      g_debug ("%s", error->message);
      goto out;
    }

  g_ptr_array_add (argv, NULL);

  g_debug ("Testing architecture %s with %s",
           multiarch, (const char *) g_ptr_array_index (argv, 0));

  my_environ = g_strdupv (envp);
  ld_preload = g_environ_getenv (my_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      my_environ = g_environ_setenv (my_environ, "LD_PRELOAD", filtered_preload, TRUE);
    }

  if (!g_spawn_sync (NULL,       /* working directory */
                     (gchar **) argv->pdata,
                     my_environ, /* envp */
                     0,          /* flags */
                     _srt_child_setup_unblock_signals,
                     NULL,       /* user data */
                     NULL,       /* stdout */
                     NULL,       /* stderr */
                     &exit_status,
                     &error))
    {
      g_debug ("... %s", error->message);
      goto out;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      goto out;
    }

  g_debug ("... it works");
  ret = TRUE;
out:
  g_strfreev (my_environ);
  g_free (helper);
  g_free (filtered_preload);
  g_clear_error (&error);
  g_clear_pointer (&argv, g_ptr_array_unref);
  return ret;
}

/**
 * srt_architecture_can_run_i386:
 *
 * Check whether we can run an i386 (%SRT_ABI_I386) executable.
 *
 * For this check to work as intended, the contents of the
 * `libsteam-runtime-tools-0-helpers:i386` package must be available
 * in the same directory hierarchy as the `libsteam-runtime-tools-0`
 * shared library, something like this:
 *
 * |[
 * any directory/
 *      lib/
 *          x86_64-linux-gnu/
 *              libsteam-runtime-tools-0.so.0
 *      libexec/
 *          steam-runtime-tools-0/
 *              i386-linux-gnu-*
 *              x86_64-linux-gnu-*
 * ]|
 *
 * Returns: %TRUE if we can run an i386 executable.
 */
gboolean
srt_architecture_can_run_i386 (void)
{
  return _srt_architecture_can_run ((gchar **) _srt_peek_environ_nonnull (),
                                    NULL, SRT_ABI_I386);
}

/**
 * srt_architecture_can_run_x86_64:
 *
 * Check whether we can run an x86_64 (%SRT_ABI_X86_64) executable.
 *
 * For this check to work as intended, the contents of the
 * `libsteam-runtime-tools-0-helpers:amd64` package must be available
 * in the same directory hierarchy as the `libsteam-runtime-tools-0`
 * shared library. See srt_architecture_can_run_i386() for details.
 *
 * Returns: %TRUE if we can run an x86_64 executable.
 */
gboolean
srt_architecture_can_run_x86_64 (void)
{
  return _srt_architecture_can_run ((gchar **) _srt_peek_environ_nonnull (),
                                    NULL, SRT_ABI_X86_64);
}

/**
 * _srt_architecture_can_run_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "can-run"
 *  property
 *
 * Returns: %TRUE if the provided @json_obj has the "can-run" member with a
 *  positive boolean value.
 */
gboolean
_srt_architecture_can_run_from_report (JsonObject *json_obj)
{
  gboolean can_run = FALSE;

  g_return_val_if_fail (json_obj != NULL, FALSE);

  if (json_object_has_member (json_obj, "can-run"))
    can_run = json_object_get_boolean_member (json_obj, "can-run");

  return can_run;
}
