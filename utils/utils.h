// Copyright © 2017 Collabora Ltd

// This file is part of libcapsule.

// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.

// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <link.h> // for __ELF_NATIVE_CLASS
#include "debug.h"

// these macros are secretly the same for elf32 & elf64:
#define ELFW_ST_TYPE(a)       ELF32_ST_TYPE(a)
#define ELFW_ST_BIND(a)       ELF32_ST_BIND(a)
#define ELFW_ST_VISIBILITY(a) ELF32_ST_VISIBILITY(a)

#if __ELF_NATIVE_CLASS == 64
#define FMT_OFF   "lu"
#define FMT_SWORD "lu"
#define FMT_WORD  "ld"
#define FMT_SIZE  "lu"
#define FMT_ADDR  "ld"
#define FMT_XADDR "lx"
#define FMT_XU64  "lx"
#else
#define FMT_OFF   "u"
#define FMT_SWORD "u"
#define FMT_WORD  "d"
#define FMT_SIZE  "u"
#define FMT_ADDR  "d"
#define FMT_XADDR "x"
#define FMT_XU64  "llx"
#endif

typedef union ptr_item
{
    ElfW(Addr) addr;
    void *ptr;
} ptr_item;

typedef struct ptr_list
{
    size_t allocated;
    size_t next;
    ptr_item *loc;
} ptr_list;

ptr_list *ptr_list_alloc (size_t size);
void ptr_list_free (ptr_list *list);
void ptr_list_push (ptr_list *list, ElfW(Addr) addr);
int ptr_list_contains (ptr_list *list, ElfW(Addr) addr);

char *safe_strncpy (char *dest, const char *src, size_t n);
int   resolve_link (const char *prefix, char *path, char *dir);
int soname_matches_path (const char *soname, const char *path);

ElfW(Addr)        fix_addr (ElfW(Addr) base, ElfW(Addr) addr);
const ElfW(Dyn) * find_dyn (ElfW(Addr) base, void *start, int what);
int               find_value (ElfW(Addr) base, void *start, int what);
ElfW(Addr)        find_ptr (ElfW(Addr) base, void *start, int what);
const char *      find_strtab (ElfW(Addr) base, void *start, int *siz);
const ElfW(Sym) * find_symbol (int idx,
                               const ElfW(Sym) *stab,
                               const char *str,
                               char **name);
