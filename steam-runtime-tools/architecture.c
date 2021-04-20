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
#include "steam-runtime-tools/json-glib-backports-internal.h"
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

G_DEFINE_QUARK (srt-architecture-error-quark, srt_architecture_error)

static const SrtKnownArchitecture known_architectures[] =
{
    {
      .multiarch_tuple = SRT_ABI_X86_64,
      .interoperable_runtime_linker = "/lib64/ld-linux-x86-64.so.2",
    },

    {
      .multiarch_tuple = SRT_ABI_I386,
      .interoperable_runtime_linker = "/lib/ld-linux.so.2",
    },

    {
      .multiarch_tuple = "x86_64-linux-gnux32",
      .interoperable_runtime_linker = "/libx32/ld-linux-x32.so.2",
    },

    { NULL }
};

/*
 * Returns: A table of known architectures, terminated by one
 *  with @multiarch_tuple set to %NULL.
 */
const SrtKnownArchitecture *
_srt_architecture_get_known (void)
{
  return &known_architectures[0];
}

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

  my_environ = _srt_filter_gameoverlayrenderer_from_envp (envp);

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
 * srt_architecture_get_expected_runtime_linker:
 * @multiarch_tuple: A multiarch tuple defining an ABI, as printed
 *  by `gcc -print-multiarch` in the Steam Runtime
 *
 * Return the interoperable path to the runtime linker `ld.so(8)`,
 * if known. For example, for x86_64, this returns
 * `/lib64/ld-linux-x86-64.so.2`.
 *
 * Returns: (type filename) (transfer none): An absolute path,
 *  or %NULL if not known
 */
const char *
srt_architecture_get_expected_runtime_linker (const char *multiarch_tuple)
{
  gsize i;

  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  for (i = 0; known_architectures[i].multiarch_tuple != NULL; i++)
    {
      if (strcmp (multiarch_tuple,
                  known_architectures[i].multiarch_tuple) == 0)
        return known_architectures[i].interoperable_runtime_linker;
    }

  return NULL;
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
  g_return_val_if_fail (json_obj != NULL, FALSE);

  return json_object_get_boolean_member_with_default (json_obj, "can-run", FALSE);
}
