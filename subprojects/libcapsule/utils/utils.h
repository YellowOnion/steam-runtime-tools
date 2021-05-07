// Copyright © 2017-2020 Collabora Ltd

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

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include <link.h> // for __ELF_NATIVE_CLASS

#include "debug.h"

#ifdef HAVE_STDALIGN_H
#include <stdalign.h>
#endif

#define UNLIKELY(x) __builtin_expect(x, 0)
#define LIKELY(x)   __builtin_expect(x, 1)

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

typedef int (*ptrcmp) (const void *a, const void *b);

ptr_list *ptr_list_alloc (size_t size);
void *ptr_list_nth_ptr (ptr_list *list, size_t nth);
void ptr_list_free (ptr_list *list);
void **ptr_list_free_to_array (ptr_list *list, size_t *n);
void ptr_list_push_ptr  (ptr_list *list, void *ptr);
void ptr_list_push_addr (ptr_list *list, ElfW(Addr) addr);
int  ptr_list_contains  (ptr_list *list, ElfW(Addr) addr);
int  ptr_list_add_ptr   (ptr_list *list, void *ptr, ptrcmp equals);

#define strstarts(str, start) \
  (strncmp( str, start, strlen( start ) ) == 0)

char *safe_strncpy (char *dest, const char *src, size_t n);
int   resolve_link (const char *prefix, char path[PATH_MAX]);
int soname_matches_path (const char *soname, const char *path);
size_t build_filename_va (char *buf, size_t len, const char *first_path, va_list ap);
size_t build_filename (char *buf, size_t len, const char *first_path, ...) __attribute__((sentinel));
char *build_filename_alloc (const char *first_path, ...) __attribute__((sentinel));

const void *      fix_addr (const void *base, ElfW(Addr) offset_or_addr);
const ElfW(Dyn) * find_dyn (ElfW(Addr) base, void *start, int what);
size_t            find_value (ElfW(Addr) base, void *start, int what);
ElfW(Addr)        find_ptr (ElfW(Addr) base, void *start, int what);
const char *      dynamic_section_find_strtab (const ElfW(Dyn) *entries, const void *base, size_t *siz);
const ElfW(Sym) * find_symbol (int idx,
                               const ElfW(Sym) *stab,
                               size_t symsz,
                               const char *str,
                               size_t strsz,
                               const char **name);

void oom( void ) __attribute__((noreturn));
char *xstrdup( const char *s );
void *xrealloc( void *ptr, size_t size ) __attribute__((alloc_size(2)));
void *xcalloc( size_t n, size_t size ) __attribute__((alloc_size(1, 2), malloc));
int xasprintf( char **s, const char *format, ...) __attribute__((format(printf, 2, 3)));
void free_strv_full( char **strings_array );

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

/*
 * N_ELEMENTS:
 * @array: A fixed-size array (not a pointer!)
 *
 * Same as `G_N_ELEMENTS`, `_DBUS_N_ELEMENTS`, systemd `ELEMENTSOF`, etc.
 */
#define N_ELEMENTS(array) ( sizeof(array) / sizeof(array[0]) )

/*
 * _capsule_clear:
 * @pp: A pointer to a pointer that can be freed by Standard C `free()`,
 *  i.e. type `void **`, `char **` or more rarely `something **`
 *
 * Free whatever object is pointed to by `*pp`, and set `*pp` to NULL.
 */
static inline void
_capsule_clear( void *pp )
{
    free( _capsule_steal_pointer( pp ) );
}

/*
 * _capsule_cleanup:
 * @clear:
 *
 * An attribute marking a variable to be cleared by `clear(&variable)`
 * on exit from its scope.
 */
#define _capsule_cleanup(clear) __attribute__((cleanup(clear)))

/*
 * _capsule_autofree:
 *
 * An attribute marking a variable to be freed by `free(variable)`
 * on exit from its scope.
 */
#define _capsule_autofree _capsule_cleanup(_capsule_clear)

/*
 * alignof:
 * @type: A type
 *
 * The same as in Standard C: return the minimal alignment of a type.
 *
 * Note that this is not the same as gcc __alignof__, which returns the
 * type's *preferred* alignment, which is sometimes greater than the
 * *minimal* alignment returned by this macro.
 */
#ifndef alignof
# define alignof(type) (__builtin_offsetof(struct { char a; type b; }, b))
#endif

/*
 * offsetof:
 * @t: A `struct` type
 * @m: A member
 *
 * The same as in Standard C: return the offset of @m within @t.
 */
#ifndef offsetof
# define offsetof(t, m) (__builtin_offsetof(t, m))
#endif

/*
 * static_assert:
 * @expr: An expression to evaluate at compile-time
 * @message: A diagnostic message used if @expr is not true
 *
 * The same as in Standard C: evaluate @expr as a compile-time constant
 * expression, and fail to build if it is zero.
 */
#ifndef static_assert
# define static_assert(expr, message) _Static_assert(expr, message)
#endif

const char *_capsule_basename (const char *path);
