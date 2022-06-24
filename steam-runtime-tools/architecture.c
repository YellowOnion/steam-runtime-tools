/*
 * Copyright © 2019-2021 Collabora Ltd.
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

#include <gelf.h>
#include <libelf.h>

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

G_DEFINE_QUARK (srt-architecture-error-quark, srt_architecture_error)

/* Backport the ARM 64 bit definition to support older toolchains */
#ifndef EM_AARCH64
#define EM_AARCH64	183	/* ARM AARCH64 */
#endif

G_STATIC_ASSERT (SRT_MACHINE_TYPE_UNKNOWN == EM_NONE);
G_STATIC_ASSERT (SRT_MACHINE_TYPE_386 == EM_386);
G_STATIC_ASSERT (SRT_MACHINE_TYPE_X86_64 == EM_X86_64);
G_STATIC_ASSERT (SRT_MACHINE_TYPE_AARCH64 == EM_AARCH64);

static const SrtKnownArchitecture known_architectures[] =
{
    {
      .multiarch_tuple = SRT_ABI_X86_64,
      .interoperable_runtime_linker = "/lib64/ld-linux-x86-64.so.2",
      .machine_type = EM_X86_64,
      .elf_class = ELFCLASS64,
      .elf_encoding = ELFDATA2LSB,
    },

    {
      .multiarch_tuple = SRT_ABI_I386,
      .interoperable_runtime_linker = "/lib/ld-linux.so.2",
      .machine_type = EM_386,
      .elf_class = ELFCLASS32,
      .elf_encoding = ELFDATA2LSB,
    },

    {
      .multiarch_tuple = "x86_64-linux-gnux32",
      .interoperable_runtime_linker = "/libx32/ld-linux-x32.so.2",
      .machine_type = EM_X86_64,
      .elf_class = ELFCLASS32,
      .elf_encoding = ELFDATA2LSB,
    },

    {
      .multiarch_tuple = SRT_ABI_AARCH64,
      .interoperable_runtime_linker = "/lib/ld-linux-aarch64.so.1",
      .machine_type = EM_AARCH64,
      .elf_class = ELFCLASS64,
      .elf_encoding = ELFDATA2LSB,
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

/*
 * @dfd: a directory file descriptor, `AT_FDCWD` or -1
 * @file_path: (type filename):
 * @cls: (not optional) (out): ELF class, normally `ELFCLASS32` or `ELFCLASS64`
 * @data_encoding: (not optional) (out): ELF data encoding, normally
 *  `ELFDATA2LSB` or `ELFDATA2MSB`
 * @machine: (not optional) (out): ELF machine, for example `EM_X86_64` or
 *  `EM_386`
 * @error: On failure set to GIOErrorEnum, used to describe the error
 *
 * Returns: %TRUE on success
 */
static gboolean
_srt_architecture_read_elf (int dfd,
                            const char *file_path,
                            guint8 *cls,
                            guint8 *data_encoding,
                            guint16 *machine,
                            GError **error)
{
  glnx_autofd int fd = -1;
  g_autoptr(Elf) elf = NULL;
  GElf_Ehdr eh;

  g_return_val_if_fail (file_path != NULL, FALSE);
  g_return_val_if_fail (cls != NULL, FALSE);
  g_return_val_if_fail (machine != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!_srt_open_elf (dfd, file_path, &fd, &elf, error))
    return FALSE;

  if (gelf_getehdr (elf, &eh) == NULL)
    return glnx_throw (error, "Error reading \"%s\" ELF header: %s",
                       file_path, elf_errmsg (elf_errno ()));

  *cls = eh.e_ident[EI_CLASS];
  *data_encoding = eh.e_ident[EI_DATA];
  *machine = eh.e_machine;

  return TRUE;
}

/*
 * @dfd: a directory file descriptor, `AT_FDCWD` or -1
 * @file_path: (type filename):
 * @error: On failure set to GIOErrorEnum or SrtArchitectureError, used to
 *  describe the error
 *
 * Returns: (nullable): The multiarch tuple or %NULL on error.
 */
const gchar *
_srt_architecture_guess_from_elf (int dfd,
                                  const char *file_path,
                                  GError **error)
{
  guint8 cls = ELFCLASSNONE;
  guint8 data_encoding = ELFDATANONE;
  guint16 machine = EM_NONE;
  gsize i;

  g_return_val_if_fail (file_path != NULL, FALSE);

  if (!_srt_architecture_read_elf (dfd, file_path, &cls, &data_encoding, &machine, error))
    return NULL;

  for (i = 0; i < G_N_ELEMENTS (known_architectures); i++)
    {
      if (known_architectures[i].multiarch_tuple != NULL
          && machine == known_architectures[i].machine_type
          && cls == known_architectures[i].elf_class
          && data_encoding == known_architectures[i].elf_encoding)
        return known_architectures[i].multiarch_tuple;
    }

  g_set_error (error, SRT_ARCHITECTURE_ERROR, SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
               "ELF class, data encoding and machine (%u,%u,%u) are unknown",
               cls, data_encoding, machine);
  return NULL;
}
