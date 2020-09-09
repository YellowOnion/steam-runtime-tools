/*
 * Copyright © 2017-2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "elf-utils.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libelf.h>

#include "libglnx/libglnx.h"

#include "glib-backports.h"

#define throw_elf_error(error, format, ...) \
  glnx_null_throw (error, format ": %s", ##__VA_ARGS__, elf_errmsg (elf_errno ()))

/*
 * pv_elf_open_fd:
 * @fd: An open file descriptor
 *
 * Returns: (transfer full): A libelf object representing the library
 */
Elf *
pv_elf_open_fd (int fd,
                GError **error)
{
  g_autoptr(Elf) elf = NULL;

  g_return_val_if_fail (fd >= 0, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (elf_version (EV_CURRENT) == EV_NONE)
    return throw_elf_error (error, "elf_version(EV_CURRENT)");

  elf = elf_begin (fd, ELF_C_READ, NULL);

  if (elf == NULL)
    return throw_elf_error (error, "elf_begin");

  return g_steal_pointer (&elf);
}

/*
 * pv_elf_get_soname:
 * @elf: A libelf object
 *
 * Return the `DT_SONAME` header, or %NULL on error
 */
gchar *
pv_elf_get_soname (Elf *elf,
                   GError **error)
{
  GElf_Ehdr ehdr;
  size_t phdr_count = 0;
  size_t i;
  GElf_Phdr phdr_mem;
  GElf_Phdr *phdr = NULL;
  GElf_Dyn *dynamic_header = NULL;
  GElf_Dyn dynamic_header_mem;
  Elf_Scn *dynamic_section = NULL;
  GElf_Shdr dynamic_section_header_mem;
  GElf_Shdr *dynamic_section_header = NULL;
  Elf_Scn *string_table_section = NULL;
  GElf_Shdr string_table_header_mem;
  GElf_Shdr *string_table_header = NULL;
  Elf_Data *data = NULL;
  size_t size_per_dyn;
  const char *soname = NULL;

  g_return_val_if_fail (elf != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (elf_kind (elf) != ELF_K_ELF)
    return glnx_null_throw (error, "elf_kind %d, expected ELF_K_ELF=%d",
                            elf_kind (elf), ELF_K_ELF);

  if (gelf_getehdr (elf, &ehdr) == NULL)
    return throw_elf_error (error, "elf_getehdr");

  if (ehdr.e_type != ET_DYN)
    return glnx_null_throw (error, "ehdr.e_type %d, expected ET_DYN=%d",
                            ehdr.e_type, ET_DYN);

  if (elf_getphdrnum (elf, &phdr_count) != 0)
    return throw_elf_error (error, "elf_getphdrnum");

  for (i = 0; i < phdr_count; i++)
    {
      phdr = gelf_getphdr (elf, i, &phdr_mem);

      if (phdr != NULL && phdr->p_type == PT_DYNAMIC)
        {
          dynamic_section = gelf_offscn (elf, phdr->p_offset);
          dynamic_section_header = gelf_getshdr (dynamic_section,
                                                 &dynamic_section_header_mem);
          break;
        }
    }

  if (dynamic_section == NULL || dynamic_section_header == NULL)
    return glnx_null_throw (error, "Unable to find dynamic section header");

  string_table_section = elf_getscn (elf, dynamic_section_header->sh_link);
  string_table_header = gelf_getshdr (string_table_section,
                                      &string_table_header_mem);

  if (string_table_section == NULL || string_table_header == NULL)
    return glnx_null_throw (error, "Unable to find linked string table");

  data = elf_getdata (dynamic_section, NULL);

  if (data == NULL)
    return throw_elf_error (error, "elf_getdata(dynamic_section)");

  size_per_dyn = gelf_fsize (elf, ELF_T_DYN, 1, EV_CURRENT);

  for (i = 0; i < dynamic_section_header->sh_size / size_per_dyn; i++)
    {
      dynamic_header = gelf_getdyn (data, i, &dynamic_header_mem);

      if (dynamic_header == NULL)
        break;

      if (dynamic_header->d_tag == DT_SONAME)
        {
          soname = elf_strptr (elf, dynamic_section_header->sh_link,
                               dynamic_header->d_un.d_val);

          if (soname == NULL)
            return glnx_null_throw (error, "Unable to read DT_SONAME");


        }
    }

  if (soname == NULL)
    return glnx_null_throw (error, "Unable to find DT_SONAME");

  return g_strdup (soname);
}

