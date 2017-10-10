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

#include <stddef.h>
#include <stdlib.h>

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
                               const char **name);

void oom( void ) __attribute__((noreturn));
char *xstrdup( const char *s );
void *xrealloc( void *ptr, size_t size ) __attribute__((alloc_size(2)));
void *xcalloc( size_t n, size_t size ) __attribute__((alloc_size(1, 2), malloc));

/*
 * _capsule_steal_pointer:
 * @pp: A non-%NULL pointer to any pointer type (void ** or Something **)
 *
 * Transfer the contents of @pp to the caller, setting @pp to %NULL.
 * This is essentially the same thing as g_steal_pointer().
 */
static inline void *
_capsule_steal_pointer( void *pp )
{
    typedef void *__attribute__((may_alias)) voidp_alias;
    voidp_alias *pointer_to_pointer = pp;
    void *ret = *pointer_to_pointer;
    *pointer_to_pointer = NULL;
    return ret;
}

/*
 * _capsule_propagate_error:
 * @code_dest: (out) (optional): where to put the error code (errno), or %NULL
 * @message_dest: (out) (transfer full) (optional) (nullable): where to
 *  put the message, or %NULL
 * @code_src: the error code (errno)
 * @message_src: (nullable) (transfer full): the error message
 *
 * This is essentially the same thing as g_propagate_error(), and leaves
 * message_src undefined. It should typically be used as:
 *
 *     _capsule_propagate_error( &code, &message, local_code,
 *                               _capsule_steal_pointer( &local_message ) );
 */
static inline void
_capsule_propagate_error( int *code_dest, char **message_dest, int code_src, char *message_src )
{
    if( code_dest != NULL )
        *code_dest = code_src;

    if( message_dest != NULL )
        *message_dest = message_src;
    else
        free( message_src );
}

void _capsule_set_error_literal( int *code_dest, char **message_dest,
                                 int code, const char *message );

void _capsule_set_error( int *code_dest, char **message_dest,
                         int code, const char *format, ... )
    __attribute__((format(printf, 4, 5)));
