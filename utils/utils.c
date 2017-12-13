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

#include <assert.h>
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "debug.h"
#include "utils.h"

unsigned long debug_flags;

// ==========================================================================
// these are for finding entries in the dynamic section
// note that tha d_un.d_ptr member may be pre-offset by the
// linker, or we may beed to adjust it by the value of base ourselves:
// this is effectively private linker information and there's no
// hard and fast rule:
const void *
fix_addr (const void *base, ElfW(Addr) offset_or_addr)
{
    if (offset_or_addr < (ElfW(Addr)) base)
    {
        // Assume it's an offset, so an address relative to addr
        return base + offset_or_addr;
    }
    else
    {
        // Assume it's an absolute address
        return (const void *) offset_or_addr;
    }
}

const ElfW(Dyn) *
find_dyn (ElfW(Addr) base, void *start, int what)
{
    ElfW(Dyn) *entry = start + base;

    for( ; entry->d_tag != DT_NULL; entry++ )
        if( entry->d_tag == what )
            return entry;

    return NULL;
}

int
find_value (ElfW(Addr) base, void *start, int what)
{
    const ElfW(Dyn) *entry = find_dyn( base, start, what );
    // TODO: what if it doesn't fit in an int?
    return entry ? (int) entry->d_un.d_val : -1;
}

ElfW(Addr)
find_ptr (ElfW(Addr) base, void *start, int what)
{
    const ElfW(Dyn) *entry = find_dyn( base, start, what );

    if( entry )
    {
        if( entry->d_un.d_ptr < base )
            return base + entry->d_un.d_ptr;
        else
            return entry->d_un.d_ptr;
    }

    return (ElfW(Addr)) NULL;
}

/*
 * dynamic_section_find_strtab:
 * @start: Array of dynamic section entries
 * @base: Starting address of the shared object in memory
 * @siz: (out) (optional): Used to return the length of the string
 *  table
 *
 * Find the string table for the given dynamic section.
 *
 * Returns: (nullable): The string table, a series of concatenated
 *  0-terminated strings whose total size is written through @siz if
 *  non-%NULL, or %NULL if not found
 */
const char *
dynamic_section_find_strtab (const ElfW(Dyn) *entries, const void *base, size_t *siz)
{
    const ElfW(Dyn) *entry;
    ElfW(Addr) stab = 0;

    for( entry = entries; entry->d_tag != DT_NULL; entry++ )
    {
        if( entry->d_tag == DT_STRTAB )
        {
            stab = entry->d_un.d_ptr;
        }
        else if( entry->d_tag == DT_STRSZ  )
        {
            if( siz )
                *siz = entry->d_un.d_val;
        }
    }

    if (stab == 0)
    {
        return NULL;
    }
    else if (stab < (ElfW(Addr)) base)
    {
        return base + stab;
    }
    else
    {
        return (const char *) stab;
    }
}

/*
 * find_symbol:
 * @idx: The symbol index
 * @stab: (array element-type=ElfW(Sym)): The symbol table
 * @str: The string table
 * @name: (out) (optional): Used to return the name, a pointer into @str
 *
 * If @idx is in-bounds for @stab, return a pointer to the @idx'th
 * entry in @stab. Otherwise return %NULL.
 */
const ElfW(Sym) *
find_symbol (int idx, const ElfW(Sym) *stab, const char *str, const char **name)
{
    const ElfW(Sym) *entry;
    const ElfW(Sym) *target = stab + idx;

    if( idx < 0 )
        return NULL;

    // we could just accept the index as legitimate but then we'd
    // run the risk of popping off into an unknown hyperspace coordinate
    // this way we stop if the target is past the known end of the table:
    for( entry = stab;
         ( (ELFW_ST_TYPE(entry->st_info) < STT_NUM) &&
           (ELFW_ST_BIND(entry->st_info) < STB_NUM) );
         entry++ )
    {
        if( entry == target )
        {
            if( name )
                *name = str + entry->st_name;
            return target;
        }
    }

    return NULL;
}

// ==========================================================================

// like strncpy except it guarantees the final byte is NUL in
// case we truncated the string. does not yet warn you in any
// way about truncation though, should probably fix that:
char *safe_strncpy (char *dest, const char *src, size_t n)
{
    char *rv = strncpy( dest, src, n );
    dest[ n - 1 ] = '\0';
    return rv;
}

// prefix is the root of the external tree we're patching in
// with libcapsule, path is what we're trying to resolve if
// it is a symlink. path must have at least PATH_MAX chars allocated.
//
// Designed to be called repeatedly, starting with an ABSOLUTE
// path the first time. Will write the resolved link back into
// path each time and return true, until fed a path which is
// not a symlink, at which point it will leave path alone and
// return false:
int resolve_link(const char *prefix, char *path)
{
    int dfd;
    char rl[PATH_MAX];
    char dir[PATH_MAX];
    char *end = NULL;
    int rv = 0;

    safe_strncpy( dir, path, PATH_MAX );
    end = strrchr( dir, '/' );

    if( end )
        *end = '\0';
    else
        strcpy( dir, "." ); // not sure this is right, FIXME?
                            // but as long as the first call to us
                            // in any sequence was an absolute path
                            // this will never come up
    dfd = open( dir, O_RDONLY );

    if( dfd < 0 )
        return 0;

    rv = readlinkat( dfd, path, rl, sizeof(rl) );

    if( rv == sizeof(rl) )
    {
        // Either rl was truncated, or there is no longer space for '\0'
        rv = -1;
        goto out;
    }

    if( rv >= 0 )
    {
        rl[ rv ] = '\0';

        if( rl[0] == '/' )
        {
            if( build_filename( path, PATH_MAX, prefix, rl, NULL ) >= PATH_MAX )
            {
                goto out;
            }
        }
        else
        {
            if( build_filename( path, PATH_MAX, dir, rl, NULL ) >= PATH_MAX )
            {
                goto out;
            }
        }
    }

out:
    close( dfd );

    return rv != -1;
}

// todo - check this properly for actual word boundaries and
// make it warn about unknown debug keywords:
void set_debug_flags (const char *control)
{
    debug_flags = DEBUG_NONE;

    if ( !control )
        return;

    if( strstr( control, "path"     ) ) debug_flags |= DEBUG_PATH;
    if( strstr( control, "search"   ) ) debug_flags |= DEBUG_SEARCH;
    if( strstr( control, "ldcache"  ) ) debug_flags |= DEBUG_LDCACHE;
    if( strstr( control, "capsule"  ) ) debug_flags |= DEBUG_CAPSULE;
    if( strstr( control, "mprotect" ) ) debug_flags |= DEBUG_MPROTECT;
    if( strstr( control, "wrappers" ) ) debug_flags |= DEBUG_WRAPPERS;
    if( strstr( control, "reloc"    ) ) debug_flags |= DEBUG_RELOCS;
    if( strstr( control, "elf"      ) ) debug_flags |= DEBUG_ELF;
    if( strstr( control, "dlfunc"   ) ) debug_flags |= DEBUG_DLFUNC;
    if( strstr( control, "all"      ) ) debug_flags |= DEBUG_ALL;

    if( !debug_flags )
        return;

    fprintf(stderr, "capsule debug flags: \n"
            "  path    : %c # path manipulation and translation"           "\n"
            "  search  : %c # searching for DSOs"                          "\n"
            "  ldcache : %c # loading/processing the ld cache"             "\n"
            "  capsule : %c # setting up the proxy capsule"                "\n"
            "  mprotect: %c # handling mprotect (for RELRO)"               "\n"
            "  wrappers: %c # function wrappers installed in the capsule"  "\n"
            "  reloc   : %c # patching capsule symbols into external DSOs" "\n"
            "  dlfunc  : %c # special handling of dlopen/dlsym calls"      "\n"
            "  elf     : %c # detailed ELF introspection logging"          "\n",
            (debug_flags & DEBUG_PATH    ) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_SEARCH  ) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_LDCACHE ) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_CAPSULE ) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_MPROTECT) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_WRAPPERS) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_RELOCS  ) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_DLFUNC  ) ? 'Y' : 'n' ,
            (debug_flags & DEBUG_ELF     ) ? 'Y' : 'n' );
}

// soname: bare libfoo.so.X style name
// path: [possibly absolute] path to DSO
// return true if soname: libFOO.so.X matches
// path: /path/to/libFOO.so.X.Y or /path/to/libFOO.so.X
int soname_matches_path (const char *soname, const char *path)
{
    const char *path_soname = strrchr( path, '/' );
    const char *pattern = path_soname ? path_soname + 1: path;
    const size_t slen = strlen( soname );

    if( strncmp( soname, pattern, slen ) != 0 )
        return 0;

    const char *end = pattern + slen;

    return ( *end == '\0' || *end == '.' );
}

/*
 * build_filename_va:
 * @buf: a buffer
 * @len: length of buffer
 * @first_path: an absolute or relative path
 * @ap: further path segments
 *
 * Fill @buf with a copy of @first_path, with subsequent path segments
 * appended to it.
 *
 * Returns: The number of bytes that would have been used in buf, not
 *  including the '\0', if there was enough space. If this is >= len,
 *  then truncation has occurred.
 */
size_t
build_filename_va (char *buf, size_t len, const char *first_path, va_list ap)
{
    const char *path;
    size_t used = 0;
    int first = 1;
    int need_separator = 0;


    if( len > 0 )
        *buf = '\0';

    for( path = first_path, first = 1;
         path != NULL;
         path = va_arg( ap, const char * ), first = 0)
    {
        size_t path_len;

        // Collapse any leading '//' to '/'
        while( path[0] == '/' && path[1] == '/' )
            path++;

        // If this is not the first path segment, strip any leading '/'
        if( path[0] == '/' && !first )
            path++;

        path_len = strlen( path );

        // Collapse any trailing '/' to nothing, unless this is the
        // first path segment, in which case collapse them to just '/'
        while( path_len > (first ? 1 : 0) && path[path_len - 1] == '/' )
        {
            path_len--;
        }

        // Ensure there is a '/' before we append path if necessary
        if( need_separator )
        {
            used++;
            if( used < len )
            {
                buf[used - 1] = '/';
                buf[used] = '\0';
            }
        }

        // If there's still any space left, try to append the path
        if( used < len )
        {
            strncpy( buf + used, path, MIN( len - used, path_len + 1 ) );
            buf[len - 1] = '\0';
        }

        used += path_len;

        // Next time, we need to append a separator, unless this was
        // the first path segment and it was '/'
        need_separator = (path_len == 0 || path[path_len - 1] != '/');
    }

    va_end( ap );
    return used;
}

/*
 * build_filename:
 * @buf: a buffer
 * @len: length of buffer
 * @first_path: an absolute or relative path
 * @...: further path segments
 *
 * Fill @buf with a copy of @first_path, with subsequent path segments
 * appended to it.
 *
 * Returns: The number of bytes that would have been used in buf, not
 *  including the '\0', if there was enough space. If this is >= len,
 *  then truncation has occurred.
 */
size_t
build_filename (char *buf, size_t len, const char *first_path, ...)
{
    size_t used;
    va_list ap;

    va_start( ap, first_path );
    used = build_filename_va( buf, len, first_path, ap );
    va_end( ap );

    return used;
}

/*
 * build_filename_alloc:
 * @first_path: an absolute or relative path
 * @...: further path segments
 *
 * Allocate and return a string built from @first_path and subsequent
 * segments. Abort if there is not enough memory.
 *
 * Returns: A string that can be freed with free()
 */
char *
build_filename_alloc (const char *first_path, ...)
{
    char *buf;
    size_t allocate;
    size_t len;
    va_list ap;

    // Do a first pass to count how much space we're going to need
    va_start( ap, first_path );
    // We need an extra byte for the "\0" which isn't included in the
    // result, consistent with strlcpy()
    allocate = build_filename_va( NULL, 0, first_path, ap ) + 1;
    va_end( ap );

    buf = xrealloc( NULL, allocate );

    // Iterate over the arguments again to fill buf
    va_start( ap, first_path );
    len = build_filename_va( buf, allocate, first_path, ap );
    va_end( ap );

    // build_filename_va() returns the same thing every time. In
    // particular this means truncation did not occur, because that
    // would be indicated by len >= allocate.
    assert( len + 1 == allocate );

    return buf;
}

ptr_list *
ptr_list_alloc(size_t size)
{
    ptr_list *list = xcalloc( 1, sizeof(ptr_list) );

    list->loc = xcalloc( size, sizeof(ptr_item) );
    list->allocated = size;
    list->next = 0;
    return list;
}

void
ptr_list_free (ptr_list *list)
{
    free( list->loc );
    list->loc = NULL;
    list->allocated = 0;
    list->next = 0;
    free( list );
}

void
ptr_list_push_addr (ptr_list *list, ElfW(Addr) addr)
{
    if( list->next >= list->allocated )
    {
        list->loc = xrealloc( list->loc,
                             (list->allocated + 16) * sizeof(ptr_item) );
        list->allocated += 16;
    }

    list->loc[ list->next++ ].addr = addr;
}


void
ptr_list_push_ptr (ptr_list *list, void *ptr)
{
    if( list->next >= list->allocated )
    {
        list->loc = realloc( list->loc,
                             (list->allocated + 16) * sizeof(ptr_item) );
        if( !list->loc )
        {
            fprintf( stderr, "failed to realloc ptr_list\n" );
            abort();
        }

        list->allocated += 16;
    }

    list->loc[ list->next++ ].ptr = ptr;
}

int
ptr_list_add_ptr (ptr_list *list, void *ptr, ptrcmp equals)
{
    for( size_t n = 0; n < list->next; n++ )
        if( equals( list->loc[ n ].ptr, ptr ) )
            return 0;

    ptr_list_push_ptr( list, ptr );

    return 1;
}

int
ptr_list_contains (ptr_list *list, ElfW(Addr) addr)
{
    if( list->next == 0 )
        return 0;

    for( size_t n = 0; n < list->next; n++ )
        if( list->loc[ n ].addr == addr )
            return 1;
    return 0;
}

void *
ptr_list_nth_ptr (ptr_list *list, size_t nth)
{
    if( nth < list->next )
        return list->loc[ nth ].ptr;

    return NULL;
}

void
oom( void )
{
    fprintf( stderr, "libcapsule: out of memory\n" );
    abort();
}

char *
xstrdup( const char *s )
{
    char *ret = strdup( s );

    if (s != NULL && ret == NULL)
        oom();

    return ret;
}

void *
xrealloc( void *ptr, size_t size )
{
    void *ret = realloc( ptr, size );

    if( ptr != NULL && size != 0 && ret == NULL )
        oom();

    return ret;
}

void *
xcalloc( size_t n, size_t size )
{
    void *ret = calloc( n, size );

    if( n != 0 && size != 0 && ret == NULL )
        oom();

    return ret;
}

int
xasprintf( char **dest, const char *format, ...)
{
    int ret;
    va_list ap;

    va_start( ap, format );
    ret = vasprintf( dest, format, ap );
    va_end( ap );

    if( ret < 0 )
        oom();

    return ret;
}

/*
 * _capsule_set_error_literal:
 * @code_dest: (out) (optional): used to return an errno value
 * @message_dest: (out) (optional) (transfer full): used to return an
 *  error message
 * @code: an errno value
 * @message: an error message
 *
 * Set an error code, like g_set_error_literal().
 */
void
_capsule_set_error_literal( int *code_dest, char **message_dest,
                            int code, const char *message )
{
    if( code_dest != NULL )
        *code_dest = code;

    if( message_dest != NULL )
        *message_dest = xstrdup( message );
}

/*
 * _capsule_set_error:
 * @code_dest: (out) (optional): used to return an errno value
 * @message_dest: (out) (optional) (transfer full): used to return an
 *  error message
 * @code: an errno value
 * @message: an error message
 *
 * Set an error code, like g_set_error().
 */
void
_capsule_set_error( int *code_dest, char **message_dest,
                    int code, const char *format,
                    ... )
{
    va_list ap;

    if( code_dest != NULL )
        *code_dest = code;

    if( message_dest != NULL )
    {
        va_start( ap, format );

        if( vasprintf( message_dest, format, ap ) < 0 )
            oom();

        va_end( ap );
    }
}
