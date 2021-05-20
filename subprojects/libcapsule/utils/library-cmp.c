// Copyright © 2020 Collabora Ltd

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

#include "library-cmp.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <search.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libelf.h>
#include <gelf.h>

#include "debug.h"
#include "utils.h"

// From binutils/include/elf/common.h (this doesn't appear to be documented
// anywhere else).
//
// /* This flag appears in a Versym structure.  It means that the symbol
//    is hidden, and is only visible with an explicit version number.
//    This is a GNU extension.  */
// #define VERSYM_HIDDEN 0x8000
//
// /* This is the mask for the rest of the Versym information.  */
// #define VERSYM_VERSION 0x7fff

#define VERSYM_HIDDEN 0x8000
#define VERSYM_VERSION 0x7fff

static int
bsearch_strcmp_cb( const void *n, const void *ip )
{
    const char *needle = n;
    const char * const *item_p = ip;
    return strcmp( needle, *item_p );
}

static int
qsort_strcmp_cb( const void* s1, const void* s2 )
{
    const char * const *a = (const char* const*) s1;
    const char* const *b = (const char* const*) s2;
    return strcmp( *a, *b );
}

/*
 * string_set_diff_flags:
 * @STRING_SET_DIFF_ONLY_IN_FIRST: At least one element is in the first set but not the second
 * @STRING_SET_DIFF_ONLY_IN_SECOND: At least one element is in the second set but not the first
 * @STRING_SET_DIFF_NONE: All elements are equal
 *
 * The result of comparing two sets of strings. If each set
 * contains elements that the other does not, then
 * both @STRING_SET_DIFF_ONLY_IN_FIRST
 * and @STRING_SET_DIFF_ONLY_IN_SECOND will be set.
 */
typedef enum
{
  STRING_SET_DIFF_ONLY_IN_FIRST = (1 << 0),
  STRING_SET_DIFF_ONLY_IN_SECOND = (1 << 1),
  STRING_SET_DIFF_NONE = 0
} string_set_diff_flags;

/*
 * compare_string_sets:
 * @first: the first set to compare
 * @first_length: number of elements in the first set
 * @second: the second set to compare
 * @second_length: number of elements in the second set
 *
 * The two sets needs to be ordered because we will use a binary search to do
 * the comparison.
 */
static string_set_diff_flags
compare_string_sets ( char **first, size_t first_length,
                      char **second, size_t second_length )
{
    string_set_diff_flags result = STRING_SET_DIFF_NONE;

    assert( first != NULL );
    assert( second != NULL );

    if( first_length > second_length )
    {
        result |= STRING_SET_DIFF_ONLY_IN_FIRST;
    }
    else
    {
        for( size_t i = 0; i < first_length; i++ )
        {
            char *found = bsearch( first[i], second, second_length, sizeof(char *), bsearch_strcmp_cb );
            if( found == NULL )
            {
                result |= STRING_SET_DIFF_ONLY_IN_FIRST;
                break;
            }
        }
    }

    if( first_length < second_length )
    {
        result |= STRING_SET_DIFF_ONLY_IN_SECOND;
    }
    else
    {
        for( size_t i = 0; i < second_length; i++ )
        {
            char *found = bsearch( second[i], first, first_length, sizeof(char *), bsearch_strcmp_cb );
            if( found == NULL )
            {
                result |= STRING_SET_DIFF_ONLY_IN_SECOND;
                break;
            }
        }
    }
    return result;
}

static void
close_elf (Elf **elfp, int *fdp)
{
  if (elfp != NULL && *elfp != NULL)
    {
      elf_end( *elfp );
      *elfp = NULL;
    }

  if (fdp != NULL && *fdp >= 0)
    {
      close( *fdp );
      *fdp = -1;
    }
}

/*
 * open_elf_library:
 * @path: (type filename): The path where the library is located
 * @fd: (out) (not optional): Used to return a file descriptor of the
 *  opened library
 * @elf: (out) (not optional): Used to return an initialized elf of the
 *  library
 * @code: (out) (optional): Used to return an error code on failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: %TRUE if the elf opening succeded, %FALSE otherwise.
 */
static bool
open_elf_library ( const char *path, int *fd, Elf **elf,
                   int *code, char **message)
{
    GElf_Ehdr ehdr;
    bool result = true;

    assert( *elf == NULL );

    if( elf_version(EV_CURRENT) == EV_NONE )
    {
        _capsule_set_error( code, message, EINVAL,
                            "elf_version(EV_CURRENT): %s",
                            elf_errmsg( elf_errno() ) );
        result = false;
        goto out;
    }

    if( ( *fd = open( path, O_RDONLY | O_CLOEXEC, 0 ) ) < 0 )
    {
        _capsule_set_error( code, message, EINVAL,
                            "failed to open %s", path );
        result = false;
        goto out;
    }

    if( ( *elf = elf_begin( *fd, ELF_C_READ, NULL ) ) == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "elf_begin() failed: %s",
                            elf_errmsg( elf_errno() ) );
        result = false;
        goto out;
    }

    if( elf_kind( *elf ) != ELF_K_ELF )
    {
        _capsule_set_error( code, message, EINVAL,
                            "%s is not in ELF format", path );
        result = false;
        goto out;
    }

    if( gelf_getehdr( *elf, &ehdr ) == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "gelf_getehdr() failed: %s",
                            elf_errmsg( elf_errno() ) );
        result = false;
        goto out;
    }
    if( ehdr.e_type != ET_DYN )
    {
        _capsule_set_error( code, message, EINVAL,
                            "%s is not a shared library, elf type is %d",
                            path, ehdr.e_type );
        result = false;
        goto out;
    }

out:
    if( !result )
        close_elf( elf, fd );

    return result;
}

static void
print_debug_string_list( char **list, const char *begin_message )
{
    if( list == NULL || list[0] == NULL )
        return;

    if( begin_message != NULL )
        DEBUG( DEBUG_ELF, "%s", begin_message );

    for( size_t i = 0; list[i] != NULL; i++ )
        DEBUG( DEBUG_ELF, "%s", list[i] );
}

static int
library_details_cmp( const void *pa, const void *pb )
{
    const library_details *a = pa;
    const library_details *b = pb;

    return strcmp( a->name, b->name );
}

static void
library_details_free( library_details *self )
{
    free_strv_full( self->public_symbol_versions );
    free_strv_full( self->public_symbols );
    free( self->comparators );
    free( self->name );
    free( self );
}

static void
library_details_free_generic( void *p )
{
    library_details_free( p );
}

void
library_knowledge_clear( library_knowledge *self )
{
    if( self->tree != NULL )
        tdestroy( self->tree, library_details_free_generic );

    self->tree = NULL;
}

/*
 * library_knowledge_lookup:
 * @self: Details of all known libraries
 * @library: The name of a library, normally the SONAME
 *
 * Returns: Details of @library, or %NULL if nothing is known about it
 */
const library_details *
library_knowledge_lookup( const library_knowledge *self,
                          const char *library )
{
    const library_details key = { (char *) library, NULL };
    library_details **node;

    node = tfind( &key, &self->tree, library_details_cmp );

    if( node == NULL )
        return NULL;

    return *node;
}

/*
 * library_cmp_split_string_by_delimiters:
 * @spec: A string of elements separated by @delimiters
 * @delimiters: Any character from this set separates the elements in @spec
 *
 * Parse a string of elements into an array.
 *
 * Returns: (transfer full): An array of the elements from @spec
 */
static char **
library_cmp_split_string_by_delimiters( const char *spec,
                                        const char *delimiters )
{
    char **ret = NULL;
    char *buf = xstrdup( spec );
    char *saveptr = NULL;
    char *token;
    ptr_list *list;

    /* Start with an arbitrary size of 8 */
    list = ptr_list_alloc( 8 );

    for( token = strtok_r( buf, delimiters, &saveptr );
         token != NULL;
         token = strtok_r( NULL, delimiters, &saveptr ) )
    {
        if( token[0] == '\0' )
            continue;

        ptr_list_push_ptr( list, xstrdup( token ) );
    }

    ret = (char **) ptr_list_free_to_array( _capsule_steal_pointer( &list ), NULL );

    free( buf );
    return ret;
}

/*
 * library_knowledge_load_from_stream:
 * @stream: A stream
 * @name: The filename of @stream or a placeholder, for diagnostic messages
 * @code: (out) (optional): Set to an `errno` value on failure
 * @message: (out) (optional): Set to an error message on failure
 *
 * Load knowledge of libraries from a stream. It is merged with anything
 * previously known by @self, with the new version preferred (so if there
 * are multiple files containing library knowledge, they should be loaded
 * in least-important-first order).
 *
 * Returns: true on success
 */
bool
library_knowledge_load_from_stream( library_knowledge *self,
                                    FILE *stream, const char *name,
                                    int *code, char **message )
{
    library_details *current = NULL;
    bool in_a_section = false;
    bool ok = false;
    int line_number = 0;
    char *line = NULL;
    size_t buf_len = 0;
    ssize_t len;

    while( ( len = getline( &line, &buf_len, stream ) ) >= 0 )
    {
        line_number++;

        if( len == 0 || *line == '\0' || *line == '\n' || *line == '#' )
            continue;

        if( line[len - 1] == '\n' )
        {
            line[--len] = '\0';
        }

        if( *line == '[' )
        {
            library_details **node;

            // new section
            if( line[len - 1] != ']' )
            {
                _capsule_set_error( code, message, EINVAL,
                                    "%s:%d: Invalid section heading \"%s\"",
                                    name, line_number, line );
                goto out;
            }

            // Wipe out the ']'
            line[--len] = '\0';

            if( strstarts( line, "[Library " ) )
            {
                current = xcalloc( 1, sizeof( library_details ) );
                current->name = xstrdup( line + strlen( "[Library " ) );
                current->comparators = NULL;

                node = tsearch( current, &self->tree, library_details_cmp );

                if( node == NULL )
                {
                    oom();
                }
                else if( *node != current )
                {
                    // we have seen this one before: keep the existing version
                    library_details_free( current );
                    current = *node;
                }
                // else self->tree takes ownership of current
            }
            // TODO: Future expansion: we could have glob matches if we
            // want them, for example [Match libGLX_*.so.0]
            else
            {
                DEBUG( DEBUG_TOOL, "Ignoring unknown section heading \"%s\"", line + 1 );
                current = NULL;
            }

            in_a_section = true;
        }
        else if( !in_a_section )
        {
            _capsule_set_error( code, message, EINVAL,
                                "%s:%d: Unexpected line not in a section: \"%s\"",
                                name, line_number, line );
           goto out;
        }
        else if( current != NULL && strstarts( line, "CompareBy=" ) )
        {
            char *values = line + strlen( "CompareBy=" );

            free( current->comparators );
            current->comparators = library_cmp_list_from_string( values, ";",
                                                                 code, message );

            if( current->comparators == NULL )
            {
                if( message != NULL )
                {
                    // Prepend file:line: before failing
                    char *tmp = *message;

                    *message = NULL;

                    if( asprintf( message, "%s:%d: %s", name, line_number, tmp ) < 0 )
                        oom();

                    free( tmp );
                }

                goto out;
            }
        }
        else if( current != NULL && strstarts( line, "PublicSymbolVersions=" ) )
        {
            char *values = line + strlen( "PublicSymbolVersions=" );
            free_strv_full( current->public_symbol_versions );

            current->public_symbol_versions = library_cmp_split_string_by_delimiters( values,
                                                                                      ";" );
        }
        else if( current != NULL && strstarts( line, "PublicSymbols=" ) )
        {
            char *values = line + strlen( "PublicSymbols=" );
            free_strv_full( current->public_symbols );

            current->public_symbols = library_cmp_split_string_by_delimiters( values,
                                                                              ";" );
        }
        else if( strchr( line, '=' ) != NULL )
        {
            DEBUG( DEBUG_TOOL, "%s:%d: Ignoring unknown key/value pair \"%s\"",
                   name, line_number, line );
        }
        else
        {
            _capsule_set_error( code, message, EINVAL,
                                "%s:%d: Unexpected line not a key/value pair: \"%s\"",
                                name, line_number, line );
            goto out;
        }
    }

    ok = true;

out:
    free( line );

    if( ok )
    {
        DEBUG( DEBUG_TOOL, "Loaded library knowledge from \"%s\"", name );
    }
    else
    {
        DEBUG( DEBUG_TOOL, "Failed to load library knowledge from \"%s\"", name );
    }

    return ok;
}

/*
 * get_versions:
 * @elf: The object's elf of which we want to get the versions
 * @versions_number: (out) (not optional): The number of versions found
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: (transfer full): The list of versions that the
 *  shared object has, on failure %NULL.
 */
static char **
get_versions( Elf *elf, size_t *versions_number, int *code, char **message )
{
    char **versions = NULL;
    Elf_Scn *scn = NULL;
    Elf_Data *data;
    GElf_Shdr shdr_mem;
    GElf_Shdr *shdr = NULL;
    GElf_Verdef def_mem;
    GElf_Verdef *def;
    bool found_verdef = false;
    uintptr_t verdef_ptr = 0;
    size_t auxoffset;
    size_t offset = 0;
    size_t phnum;
    size_t sh_entsize;
    ptr_list *versions_list = NULL;

    assert( versions_number != NULL );

    *versions_number = 0;

    if( elf_getphdrnum( elf, &phnum ) < 0 )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to determine the number of program headers: %s",
                            elf_errmsg( elf_errno() ) );
        return versions;
    }

    /* Get the dynamic section */
    for( size_t i = 0; i < phnum; i++ )
    {
        GElf_Phdr phdr_mem;
        GElf_Phdr *phdr = gelf_getphdr( elf, i, &phdr_mem );
        if( phdr != NULL && phdr->p_type == PT_DYNAMIC )
        {
            scn = gelf_offscn( elf, phdr->p_offset );
            shdr = gelf_getshdr( scn, &shdr_mem );
            break;
        }
    }

    if( shdr == NULL )
    {
        int err = elf_errno();
        if( err == 0 )
        {
            _capsule_set_error( code, message, EINVAL,
                                "Unable to find the section header from the dynamic section" );
        }
        else
        {
            _capsule_set_error( code, message, EINVAL,
                                "Unable to get the section header: %s",
                                elf_errmsg( err ) );
        }

        return versions;
    }

    data = elf_getdata( scn, NULL );
    if( data == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to get the dynamic section data: %s",
                            elf_errmsg( elf_errno() ) );
        return versions;
    }

    sh_entsize = gelf_fsize( elf, ELF_T_DYN, 1, EV_CURRENT );
    for( size_t i = 0; i < shdr->sh_size / sh_entsize; i++ )
    {
        GElf_Dyn dyn_mem;
        GElf_Dyn *dyn = gelf_getdyn( data, i, &dyn_mem );
        if( dyn == NULL )
            break;

        if( dyn->d_tag == DT_VERDEF )
        {
            verdef_ptr = dyn->d_un.d_ptr;
            found_verdef = true;
            break;
        }
    }

    if( !found_verdef )
    {
        DEBUG( DEBUG_ELF, "The version definition table is not available" );
        versions = calloc( 1, sizeof(char *) );
        versions[0] = NULL;
        return versions;
    }
    scn = gelf_offscn( elf, verdef_ptr );
    data = elf_getdata( scn, NULL );
    if( data == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to get symbols data: %s", elf_errmsg( elf_errno() ) );
        return versions;
    }

    def = gelf_getverdef( data, 0, &def_mem );
    if( def == NULL )
    {
        DEBUG( DEBUG_ELF, "Verdef is not available: %s", elf_errmsg( elf_errno() ) );
        versions = calloc( 1, sizeof(char *) );
        versions[0] = NULL;
        return versions;
    }

    /* Arbitrarily start the list with 8 elements */
    versions_list = ptr_list_alloc( 8 );
    while( def != NULL )
    {
        GElf_Verdaux aux_mem, *aux;
        const char *version;

        auxoffset = offset + def->vd_aux;
        offset += def->vd_next;

        /* The first Verdaux array must exist and it points to the version
         * definition string that Verdef defines. Every possible additional
         * Verdaux arrays are the dependencies of said version definition.
         * In our case we don't need to list the dependencies, so we just
         * get the first Verdaux of every Verdef. */
        aux = gelf_getverdaux( data, auxoffset, &aux_mem );
        if( aux == NULL )
            continue;
        version = elf_strptr( elf, shdr->sh_link, aux->vda_name );
        if( version == NULL )
            continue;

        if( ( def->vd_flags & VER_FLG_BASE ) == 0 )
            ptr_list_push_ptr( versions_list, strdup( version ) );

        if( def->vd_next == 0 )
            def = NULL;
        else
            def = gelf_getverdef( data, offset, &def_mem );

    }

    versions = (char **) ptr_list_free_to_array ( versions_list, versions_number );
    qsort( versions, *versions_number, sizeof(char *), qsort_strcmp_cb );
    return versions;
}

static const char * const ignore_symbols[] =
{
    /* Libraries on at least SteamOS 2 'brewmaster' sometimes have
     * symbols that appear to have an empty name. */
    "",

    /* These symbols can appear in libraries witout actually being part
     * of anyone's ABI. List taken from dpkg-gensymbols. */
    "__bss_end__",
    "__bss_end",
    "_bss_end__",
    "__bss_start",
    "__bss_start__",
    "__data_start",
    "__do_global_ctors_aux",
    "__do_global_dtors_aux",
    "__do_jv_register_classes",
    "_DYNAMIC",
    "_edata",
    "_end",
    "__end__",
    "__exidx_end",
    "__exidx_start",
    "_fbss",
    "_fdata",
    "_fini",
    "_ftext",
    "_GLOBAL_OFFSET_TABLE_",
    "__gmon_start__",
    "__gnu_local_gp",
    "_gp",
    "_init",
    "_PROCEDURE_LINKAGE_TABLE_",
    "_SDA2_BASE_",
    "_SDA_BASE_",
};

/*
 * get_symbols:
 * @elf: The object's elf of which we want to get the symbols
 * @symbols_number: (out) (not optional): The number of symbols found
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: (transfer full): The list of symbols that the
 *  shared object has, on failure %NULL.
 */
static char **
get_symbols ( Elf *elf, size_t *symbols_number, int *code, char **message )
{
    char **symbols = NULL;
    Elf_Scn *scn = NULL;
    Elf_Scn *scn_sym = NULL;
    Elf_Scn *scn_ver = NULL;
    Elf_Scn *scn_verdef = NULL;
    Elf_Data *data;
    Elf_Data *sym_data;
    Elf_Data *versym_data = NULL;
    Elf_Data *verdef_data = NULL;
    GElf_Ehdr ehdr;
    GElf_Shdr shdr_mem;
    GElf_Shdr *shdr = NULL;
    bool found_symtab = false;
    bool found_versym = false;
    bool found_verdef = false;
    uintptr_t symtab_ptr = 0;
    uintptr_t versym_ptr = 0;
    uintptr_t verdef_ptr = 0;
    size_t elsize = 0;
    size_t phnum;
    size_t sh_entsize;
    ptr_list *symbols_list = NULL;

    assert( symbols_number != NULL );

    *symbols_number = 0;

    if( elf_getphdrnum( elf, &phnum ) < 0 )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to determine the number of program headers: %s",
                            elf_errmsg( elf_errno() ) );
        return symbols;
    }

    /* Get the dynamic section */
    for( size_t i = 0; i < phnum; i++ )
    {
        GElf_Phdr phdr_mem;
        GElf_Phdr *phdr = gelf_getphdr( elf, i, &phdr_mem );
        if( phdr != NULL && phdr->p_type == PT_DYNAMIC )
        {
            scn = gelf_offscn( elf, phdr->p_offset );
            shdr = gelf_getshdr( scn, &shdr_mem );
            break;
        }
    }

    if( shdr == NULL )
    {
        int err = elf_errno();
        if( err == 0 )
        {
            _capsule_set_error( code, message, EINVAL,
                                "Unable to find the section header from the dynamic section" );
        }
        else
        {
            _capsule_set_error( code, message, EINVAL,
                                "Unable to get the section header: %s",
                                elf_errmsg( err ) );
        }

        return symbols;
    }

    data = elf_getdata( scn, NULL );
    if( data == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to get dynamic section data: %s", elf_errmsg( elf_errno() ) );
        return symbols;
    }

    sh_entsize = gelf_fsize( elf, ELF_T_DYN, 1, EV_CURRENT );

    for( size_t i = 0; i < shdr->sh_size / sh_entsize; i++ )
    {
        GElf_Dyn dyn_mem;
        GElf_Dyn *dyn = gelf_getdyn (data, i, &dyn_mem);
        if( dyn == NULL )
            break;

        switch( dyn->d_tag )
        {
            case DT_SYMTAB:
                symtab_ptr = dyn->d_un.d_ptr;
                found_symtab = true;
                break;
            case DT_VERSYM:
                versym_ptr = dyn->d_un.d_ptr;
                found_versym = true;
                break;
            case DT_VERDEF:
                verdef_ptr = dyn->d_un.d_ptr;
                found_verdef = true;
                break;
            default:
                break;
        }
    }


    if( !found_symtab )
    {
        _capsule_set_error( code, message, EINVAL, "Unable to find the symbols table" );
        return symbols;
    }
    scn_sym = gelf_offscn( elf, symtab_ptr );
    sym_data = elf_getdata( scn_sym, NULL );
    if( sym_data == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to get symbols table data: %s", elf_errmsg( elf_errno() ) );
        return symbols;
    }

    if( found_versym )
    {
        scn_ver = gelf_offscn( elf, versym_ptr );
        versym_data = elf_getdata( scn_ver, NULL );
        if( versym_data == NULL )
        {
            _capsule_set_error( code, message, EINVAL,
                                "Unable to get symbols version information data: %s",
                                elf_errmsg( elf_errno() ) );
            return symbols;
        }
    }

    if( found_verdef )
    {
        scn_verdef = gelf_offscn( elf, verdef_ptr );
        verdef_data = elf_getdata( scn_verdef, NULL );
        if( verdef_data == NULL )
        {
            _capsule_set_error( code, message, EINVAL,
                                "Unable to get symbols version definition data: %s",
                                elf_errmsg( elf_errno() ) );
            return symbols;
        }
    }

    if( gelf_getehdr( elf, &ehdr ) == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Unable to retrieve Ehdr header: %s",
                            elf_errmsg( elf_errno() ) );
        return symbols;
    }

    elsize = gelf_fsize( elf, ELF_T_SYM, 1, ehdr.e_version );
    if( elsize == 0 )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Size of symbols in Ehdr array is zero: %s",
                            elf_errmsg( elf_errno() ) );
        return symbols;
    }

    /* Arbitrarily start the list with 8 elements */
    symbols_list = ptr_list_alloc( 8 );

    for( size_t index = 0; index < sym_data->d_size / elsize; index++ )
    {
        GElf_Sym *sym;
        GElf_Sym sym_mem;
        const char *symbol;
        GElf_Versym versym_mem;
        GElf_Versym *versym;
        GElf_Verdaux aux_mem;
        GElf_Verdaux *aux = NULL;
        GElf_Verdef def_mem;
        GElf_Verdef *def = NULL;
        bool interesting = true;

        sym = gelf_getsymshndx( sym_data, NULL, index, &sym_mem, NULL );

        /* If the symbol info is not available, or undefined, we skip it */
        if( sym == NULL || sym->st_shndx == SHN_UNDEF )
            continue;

        symbol = elf_strptr( elf, shdr->sh_link, sym->st_name );
        if( symbol == NULL )
            break;

        for( size_t i = 0; i < N_ELEMENTS( ignore_symbols ); i++ )
        {
            if( strcmp( symbol, ignore_symbols[i] ) == 0 )
            {
                interesting = false;
                break;
            }
        }

        if ( !interesting )
        {
            DEBUG( DEBUG_ELF, "Symbol '%s' is uninteresting", symbol );
            continue;
        }

        /* Search the version of the symbol */
        if( found_versym && found_verdef )
        {
            versym = gelf_getversym( versym_data, index, &versym_mem );
            def = gelf_getverdef( verdef_data, 0, &def_mem );

            size_t auxoffset;
            size_t offset = 0;
            while( def != NULL )
            {
                auxoffset = offset + def->vd_aux;
                offset += def->vd_next;

                /* The first Verdaux array must exist and it points to the version
                * definition string that Verdef defines. Every possible additional
                * Verdaux arrays are the dependencies of said version definition.
                * In our case we don't need to list the dependencies, so we just
                * get the first Verdaux of every Verdef. */
                aux = gelf_getverdaux( verdef_data, auxoffset, &aux_mem );
                if( aux == NULL )
                    continue;

                /* Break if we have found the symbol's version */
                if( def->vd_ndx == ( *versym & VERSYM_VERSION ) )
                    break;

                if( def->vd_next == 0 )
                    def = NULL;
                else
                    def = gelf_getverdef( verdef_data, offset, &def_mem );
            }
        }

        /* If the symbol is versioned */
        if( def != NULL && aux != NULL )
        {
            const char *version = elf_strptr( elf, shdr->sh_link, aux->vda_name );
            char *symbol_versioned;
            xasprintf( &symbol_versioned, "%s@%s", symbol, version );
            ptr_list_push_ptr( symbols_list, symbol_versioned );
        }
        else
        {
            ptr_list_push_ptr( symbols_list, strdup( symbol ) );
        }
    }

    symbols = (char **) ptr_list_free_to_array ( symbols_list, symbols_number );
    qsort( symbols, *symbols_number, sizeof(char *), qsort_strcmp_cb );
    return symbols;
}

/*
 * library_cmp_by_name:
 * @details: The library we are interested in and how to compare it
 * @left_path: The path to the "left" instance of the library
 * @left_from: Arbitrary description of the container/provider/sysroot
 *  where we found @left_path, used in debug logging
 * @right_path: The path to the "right" instance of the library
 * @right_from: Arbitrary description of the container/provider/sysroot
 *  where we found @right_path, used in debug logging
 *
 * Attempt to determine whether @left_path is older than, newer than or
 * the same as than @right_path by inspecting their filenames.
 *
 * Return a strcmp-style result: negative if left < right,
 * positive if left > right, zero if left == right or if left and right
 * are non-comparable.
 */
static int
library_cmp_by_name( const library_details *details,
                     const char *left_path,
                     const char *left_from,
                     const char *right_path,
                     const char *right_from )
{
    _capsule_autofree char *left_realpath = NULL;
    _capsule_autofree char *right_realpath = NULL;
    const char *left_basename;
    const char *right_basename;

    // This might look redundant when our arguments come from the ld_libs,
    // but resolve_symlink_prefixed() doesn't chase symlinks if the
    // prefix is '/' or empty.
    left_realpath = realpath( left_path, NULL );
    right_realpath = realpath( right_path, NULL );
    left_basename = _capsule_basename( left_realpath );
    right_basename = _capsule_basename( right_realpath );

    DEBUG( DEBUG_TOOL,
           "Comparing %s \"%s\" from \"%s\" with "
           "\"%s\" from \"%s\"",
           details->name, left_basename, left_from, right_basename, right_from );

    if( strcmp( left_basename, right_basename ) == 0 )
    {
        DEBUG( DEBUG_TOOL,
               "Name of %s \"%s\" from \"%s\" compares the same as "
               "\"%s\" from \"%s\"",
               details->name, left_basename, left_from, right_basename, right_from );
        return 0;
    }

    if( strcmp( details->name, left_basename ) == 0 )
    {
        /* In some distributions (Debian, Ubuntu, Manjaro)
         * libgcc_s.so.1 is a plain file, not a symlink to a
         * version-suffixed version. We cannot know just from the name
         * whether that's older or newer, so assume equal. The caller is
         * responsible for figuring out which one to prefer. */
        DEBUG( DEBUG_TOOL,
               "Unversioned %s \"%s\" from \"%s\" cannot be compared with "
               "\"%s\" from \"%s\"",
               details->name, left_basename, left_from,
               right_basename, right_from );
        return 0;
    }

    if( strcmp( details->name, right_basename ) == 0 )
    {
        /* The same, but the other way round */
        DEBUG( DEBUG_TOOL,
               "%s \"%s\" from \"%s\" cannot be compared with "
               "unversioned \"%s\" from \"%s\"",
               details->name, left_basename, left_from,
               right_basename, right_from );
        return 0;
    }

    return ( strverscmp( left_basename, right_basename ) );
}

/*
 * library_cmp_filter_list:
 * @filters: The list of patterns that will be checked against @list.
 *  Patterns that start with '!' are considered negated (privates),
 *  i.e. the elements in @list, that matches said pattern, will be removed.
 *  A pattern that is just '!' is used to separate what's known to the
 *  guessing.
 * @list: (array zero-terminated=1): The list to filter
 * @list_number: The number of elements in @list excluding the %NULL
 *  terminator
 * @filtered_list_number_out: (out): Used to return the number of elements in
 *  the newly created filtered list excluding the %NULL terminator
 *
 * Creates a new filtered list starting from the given @list and applying the
 * patterns in @filters.
 * The patterns are evaluated in order.
 * If there are elements that don't match any of the provided filters, or they
 * match a filter after the special '!' pattern, a warning will be printed.
 * The default filter behavior for elements that don't match any patterns
 * is to exclude them (treat as private). However it is higly recommended
 * to be explicit and end @filters with a wildcard allow everything "*",
 * or reject everything "!*".
 *
 * Returns: (array zero-terminated=1) (transfer full): An array with
 *  the elements that matches the provided @filters.
 */
static char **
library_cmp_filter_list( char ** const filters,
                         char ** const list,
                         size_t list_number,
                         size_t *filtered_list_number_out )
{
    ptr_list *filtered_list;
    size_t i = 0;
    size_t j = 0;
    bool guessing = false;
    char *buf = NULL;
    char *token;
    filtered_list = ptr_list_alloc( list_number );

    assert( filters != NULL );
    assert( list != NULL );
    assert( filtered_list_number_out != NULL );

    *filtered_list_number_out = 0;

    for( i = 0; list[i] != NULL; i++ )
    {
        char *saveptr = NULL;
        guessing = false;

        free( buf );
        buf = xstrdup( list[i] );
        /* If we have a versioned symbol, like "symbol@version", remove the
         * version part because the filters are just for the symbols name. */
        token = strtok_r( buf, "@", &saveptr );

        for( j = 0; filters[j] != NULL; j++ )
        {
            if( strcmp( filters[j], "!" ) == 0 )
            {
                DEBUG( DEBUG_TOOL, "After this point we are just guessing" );
                guessing = true;
                continue;
            }

            if( filters[j][0] == '!' )
            {
                if( fnmatch( filters[j] + 1, token, 0 ) == 0 )
                {
                    if( guessing )
                        warnx( "warning: we are assuming \"%s\" to be private, but it's just a guess",
                               token );
                    else
                        DEBUG( DEBUG_TOOL,
                               "Ignoring \"%s\" because it has been declared as private",
                               token );
                    break;
                }
            }
            else
            {
                if( fnmatch( filters[j], token, 0 ) == 0 )
                {
                    if( guessing )
                        warnx( "warning: we are assuming \"%s\" to be public, but it's just a guess",
                               token );

                    ptr_list_push_ptr( filtered_list, xstrdup( list[i] ) );
                    break;
                }
            }
        }
        /* If we checked all the patterns and didn't have a match */
        if( filters[j] == NULL )
            warnx( "warning: \"%s\" does not have a match in the given filters, treating it as private",
                   token );
    }

    free( buf );

    return (char **) ptr_list_free_to_array( _capsule_steal_pointer( &filtered_list ),
                                            filtered_list_number_out );
}

/*
 * library_cmp_by_symbols:
 * @details: The library we are interested in and how to compare it
 * @container_path: (type filename): The path where the container's soname is
 *  located
 * @provider_path: (type filename): The path where the provider's soname is
 *  located
 *
 * Attempt to determine whether soname is older, newer or
 * the same in the container or the provider inspecting their
 * symbols.
 *
 * Return a strcmp-style result: negative if container < provider,
 * positive if container > provider, zero if container == provider
 * or if container and provider are non-comparable.
 */
static int
library_cmp_by_symbols( const library_details *details,
                        const char *container_path,
                        const char *container_root,
                        const char *provider_path,
                        const char *provider_root )
{
    string_set_diff_flags symbol_result = STRING_SET_DIFF_NONE;
    int cmp_result = 0;
    int container_fd = -1;
    int provider_fd = -1;
    Elf *container_elf = NULL;
    Elf *provider_elf = NULL;
    char **container_symbols = NULL;
    char **provider_symbols = NULL;
    size_t container_symbols_number = 0;
    size_t provider_symbols_number = 0;
    int code = 0;
    char *message = NULL;
    char *symbol_message = NULL;

    if( !open_elf_library( container_path, &container_fd, &container_elf,
                           &code, &message ) )
    {
        DEBUG( DEBUG_TOOL,
               "an error occurred while opening %s (%d): %s",
               container_path, code, message );
        goto out;
    }

    container_symbols = get_symbols( container_elf, &container_symbols_number,
                                     &code, &message );

    if( container_symbols == NULL )
    {
        warnx( "failed to get container versions for %s (%d): %s",
               details->name, code, message );
        goto out;
    }

    if( details->public_symbols != NULL )
    {
        char **container_symbols_filtered;
        size_t container_symbols_number_filtered = 0;
        container_symbols_filtered = library_cmp_filter_list( details->public_symbols,
                                                              container_symbols,
                                                              container_symbols_number,
                                                              &container_symbols_number_filtered );
        free_strv_full( container_symbols );

        container_symbols = container_symbols_filtered;
        container_symbols_number = container_symbols_number_filtered;
    }

    xasprintf( &symbol_message, "Container Symbols of %s:", details->name );
    print_debug_string_list( container_symbols, symbol_message );
    _capsule_clear( &symbol_message );

    if( !open_elf_library( provider_path, &provider_fd, &provider_elf,
                           &code, &message ) )
    {
        DEBUG( DEBUG_TOOL,
               "an error occurred while opening %s (%d): %s",
               container_path, code, message );
        goto out;
    }

    provider_symbols = get_symbols( provider_elf, &provider_symbols_number,
                                       &code, &message );

    if( provider_symbols == NULL )
    {
        warnx( "failed to get provider versions for %s (%d): %s",
               details->name, code, message );
        goto out;
    }

    if( details->public_symbols != NULL )
    {
        char **provider_symbols_filtered;
        size_t provider_symbols_number_filtered = 0;
        provider_symbols_filtered = library_cmp_filter_list( details->public_symbols,
                                                             provider_symbols,
                                                             provider_symbols_number,
                                                             &provider_symbols_number_filtered );
        free_strv_full( provider_symbols );

        provider_symbols = provider_symbols_filtered;
        provider_symbols_number = provider_symbols_number_filtered;
    }

    xasprintf( &symbol_message, "Provider Symbols of %s:", details->name );
    print_debug_string_list( provider_symbols, symbol_message );
    _capsule_clear( &symbol_message );

    symbol_result = compare_string_sets( container_symbols, container_symbols_number,
                                         provider_symbols, provider_symbols_number );

    /* In container we have strictly more symbols: don't symlink the one
     * from the provider */
    if( symbol_result == STRING_SET_DIFF_ONLY_IN_FIRST )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container is newer because its symbols are a strict superset",
               details->name );
        cmp_result = 1;
    }
    /* In provider we have strictly more symbols: create the symlink */
    else if( symbol_result == STRING_SET_DIFF_ONLY_IN_SECOND )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the provider is newer because its symbols are a strict superset",
               details->name );
        cmp_result = -1;
    }
    /* With the following two cases we are still unsure which library is newer, so we
     * will choose the provider */
    else if( symbol_result == STRING_SET_DIFF_NONE )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have the same symbols",
               details->name );
    }
    else
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have different symbols and neither is a superset of the other",
               details->name );
    }

out:
    _capsule_clear( &message );
    free_strv_full( container_symbols );
    free_strv_full( provider_symbols );
    close_elf( &container_elf, &container_fd );
    close_elf( &provider_elf, &provider_fd );
    return cmp_result;

}

/*
 * library_cmp_by_versions:
 * @details: The library we are interested in and how to compare it
 * @container_path: (type filename): The path where the container's soname is
 *  located
 * @provider_path: (type filename): The path where the provider's soname is
 *  located
 *
 * Attempt to determine whether soname is older, newer or
 * the same in the container or the provider inspecting their
 * symbols versions.
 *
 * Return a strcmp-style result: negative if container < provider,
 * positive if container > provider, zero if container == provider
 * or if container and provider are non-comparable.
 */
static int
library_cmp_by_versions( const library_details *details,
                         const char *container_path,
                         const char *container_root,
                         const char *provider_path,
                         const char *provider_root )
{
    string_set_diff_flags version_result = STRING_SET_DIFF_NONE;
    int cmp_result = 0;
    int container_fd = -1;
    int provider_fd = -1;
    Elf *container_elf = NULL;
    Elf *provider_elf = NULL;
    char **container_versions = NULL;
    char **provider_versions = NULL;
    size_t container_versions_number = 0;
    size_t provider_versions_number = 0;
    int code = 0;
    char *message = NULL;
    char *version_message;

    if( !open_elf_library( container_path, &container_fd, &container_elf,
                           &code, &message ) )
    {
        DEBUG( DEBUG_TOOL,
               "an error occurred while opening %s (%d): %s",
               container_path, code, message );
        goto out;
    }

    container_versions = get_versions( container_elf, &container_versions_number,
                                       &code, &message );

    if( container_versions == NULL )
    {
        warnx( "failed to get container versions for %s (%d): %s",
               details->name, code, message );
        goto out;
    }

    if( details->public_symbol_versions != NULL )
    {
        char **container_versions_filtered;
        size_t container_versions_number_filtered = 0;
        container_versions_filtered = library_cmp_filter_list( details->public_symbol_versions,
                                                               container_versions,
                                                               container_versions_number,
                                                               &container_versions_number_filtered );
        free_strv_full( container_versions );

        container_versions = container_versions_filtered;
        container_versions_number = container_versions_number_filtered;
    }

    xasprintf( &version_message, "Container versions of %s:", details->name);
    print_debug_string_list( container_versions, version_message );
    _capsule_clear( &version_message );

    if( !open_elf_library( provider_path, &provider_fd, &provider_elf,
                           &code, &message ) )
    {
        DEBUG( DEBUG_TOOL,
               "an error occurred while opening %s (%d): %s",
               container_path, code, message );
        goto out;
    }

    provider_versions = get_versions( provider_elf, &provider_versions_number,
                                      &code, &message );

    if( provider_versions == NULL )
    {
        warnx( "failed to get provider versions for %s (%d): %s",
               details->name, code, message );
        goto out;
    }

    if( details->public_symbol_versions != NULL )
    {
        char **provider_versions_filtered;
        size_t provider_versions_number_filtered = 0;
        provider_versions_filtered = library_cmp_filter_list( details->public_symbol_versions,
                                                              provider_versions,
                                                              provider_versions_number,
                                                              &provider_versions_number_filtered );
        free_strv_full( provider_versions );

        provider_versions = provider_versions_filtered;
        provider_versions_number = provider_versions_number_filtered;
    }

    xasprintf( &version_message, "Provider versions of %s:", details->name);
    print_debug_string_list( provider_versions, version_message );
    _capsule_clear( &version_message );

    version_result = compare_string_sets( container_versions, container_versions_number,
                                          provider_versions, provider_versions_number );

    /* Version in container is strictly newer: don't symlink the one
     * from the provider */
    if( version_result == STRING_SET_DIFF_ONLY_IN_FIRST )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container is newer because its version definitions are a strict superset",
               details->name );
        cmp_result = 1;
    }
    /* Version in the provider is strictly newer: create the symlink */
    else if( version_result == STRING_SET_DIFF_ONLY_IN_SECOND )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the provider is newer because its version definitions are a strict superset",
               details->name );
        cmp_result = -1;
    }
    /* With the following two cases we are still unsure which library is newer */
    else if( version_result == STRING_SET_DIFF_NONE )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have the same symbol versions",
               details->name );
    }
    else
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have different symbol versions and neither is a superset of the other",
               details->name );
    }

out:
    _capsule_clear( &message );
    free_strv_full( container_versions );
    free_strv_full( provider_versions );
    close_elf( &container_elf, &container_fd );
    close_elf( &provider_elf, &provider_fd );
    return cmp_result;
}

static int
library_cmp_choose_container( const library_details *details,
                              const char *container_path,
                              const char *container_root,
                              const char *provider_path,
                              const char *provider_root )
{
    DEBUG( DEBUG_TOOL,
           "Choosing %s from container \"%s\", ignoring provider \"%s\"",
           details->name, container_root, provider_root );
    return 1;
}

static int
library_cmp_choose_provider( const library_details *details,
                             const char *container_path,
                             const char *container_root,
                             const char *provider_path,
                             const char *provider_root )
{
    DEBUG( DEBUG_TOOL,
           "Choosing %s from provider \"%s\", ignoring container \"%s\"",
           details->name, provider_root, container_root );
    return -1;
}

typedef struct
{
    const char *name;
    library_cmp_function comparator;
} named_comparator;

static const named_comparator named_comparators[] =
{
    { "name", library_cmp_by_name },
    { "symbols", library_cmp_by_symbols },
    { "versions", library_cmp_by_versions },
    { "container", library_cmp_choose_container },
    { "provider", library_cmp_choose_provider },
};

/*
 * library_cmp_list_from_string:
 * @spec: A string of tokens separated by @delimiters
 * @delimiters: Any character from this set separates tokens
 * @code: (out) (optional): Set to an errno value on error
 * @message: (out) (optional): Set to a string error message on error
 *
 * Parse a list of comparators into an array of functions.
 *
 * Returns: (free-function free): An array of comparators
 */
library_cmp_function *
library_cmp_list_from_string( const char *spec,
                              const char *delimiters,
                              int *code,
                              char **message )
{
    library_cmp_function *ret = NULL;
    char *buf = xstrdup( spec );
    char *saveptr = NULL;
    char *token;
    ptr_list *list;

    static_assert( sizeof( void * ) == sizeof( library_cmp_function ),
                   "Function pointer assumed to be same size as void *");
    static_assert( alignof( void * ) == alignof( library_cmp_function ),
                   "Function pointer assumed to be same alignment as void *");

    list = ptr_list_alloc( N_ELEMENTS( named_comparators ) );

    for( token = strtok_r( buf, delimiters, &saveptr );
         token != NULL;
         token = strtok_r( NULL, delimiters, &saveptr ) )
    {
        library_cmp_function comparator = NULL;
        size_t i;

        if( token[0] == '\0' )
            continue;

        for( i = 0; i < N_ELEMENTS( named_comparators ); i++ )
        {
            if( strcmp( token, named_comparators[i].name ) == 0 )
            {
                comparator = named_comparators[i].comparator;
                break;
            }
        }

        if( comparator == NULL )
        {
            _capsule_set_error( code, message,
                                EINVAL, "Unknown library comparison mode \"%s\"",
                                token );
            goto out;
        }

        ptr_list_push_ptr( list, (void *) comparator );
    }

    ret = (library_cmp_function *) ptr_list_free_to_array( _capsule_steal_pointer( &list ), NULL );

out:
    if( list != NULL )
        ptr_list_free( list );

    free( buf );
    return ret;
}

/*
 * library_cmp_list_iterate:
 * @details: The library we are interersted in and how to compare it
 * @container_path: The path to the library in the container
 * @container_root: The path to the top-level directory of the container
 * @provider_path: The path to the library in the provider
 * @provider_root: The path to the top-level directory of the provider
 *
 * Iterate through a %NULL-terminated array of comparators from the given
 * @details, highest-precedence first, calling each one in turn until one of
 * them returns a nonzero value. If none of them return nonzero, return 0.
 *
 * Returns: Negative if the container version appears newer, zero if they
 *  appear the same or we cannot tell, or positive if the provider version
 *  appears newer.
 */
int
library_cmp_list_iterate( const library_details *details,
                          const char *container_path,
                          const char *container_root,
                          const char *provider_path,
                          const char *provider_root )
{
    const library_cmp_function *iter;

    assert( details != NULL );

    for( iter = details->comparators; iter != NULL && *iter != NULL; iter++ )
    {
        int decision = (*iter)( details,
                                container_path, container_root,
                                provider_path, provider_root );

        if( decision != 0 )
            return decision;
    }

    return 0;
}
