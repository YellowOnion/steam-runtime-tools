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
 * @soname: The library we are interested in, used in debug logging
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
library_cmp_by_name( const char *soname,
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
           soname, left_basename, left_from, right_basename, right_from );

    if( strcmp( left_basename, right_basename ) == 0 )
    {
        DEBUG( DEBUG_TOOL,
               "Name of %s \"%s\" from \"%s\" compares the same as "
               "\"%s\" from \"%s\"",
               soname, left_basename, left_from, right_basename, right_from );
        return 0;
    }

    if( strcmp( soname, left_basename ) == 0 )
    {
        /* In some distributions (Debian, Ubuntu, Manjaro)
         * libgcc_s.so.1 is a plain file, not a symlink to a
         * version-suffixed version. We cannot know just from the name
         * whether that's older or newer, so assume equal. The caller is
         * responsible for figuring out which one to prefer. */
        DEBUG( DEBUG_TOOL,
               "Unversioned %s \"%s\" from \"%s\" cannot be compared with "
               "\"%s\" from \"%s\"",
               soname, left_basename, left_from,
               right_basename, right_from );
        return 0;
    }

    if( strcmp( soname, right_basename ) == 0 )
    {
        /* The same, but the other way round */
        DEBUG( DEBUG_TOOL,
               "%s \"%s\" from \"%s\" cannot be compared with "
               "unversioned \"%s\" from \"%s\"",
               soname, left_basename, left_from,
               right_basename, right_from );
        return 0;
    }

    return ( strverscmp( left_basename, right_basename ) );
}

/*
 * library_cmp_by_symbols:
 * @soname: (type filename): The library we are interested in, used for debug
 * @container_path: (type filename): The path where the container's @soname is
 *  located
 * @provider_path: (type filename): The path where the provider's @soname is
 *  located
 *
 * Attempt to determine whether @soname is older, newer or
 * the same in the container or the provider inspecting their
 * symbols.
 *
 * Return a strcmp-style result: negative if container < provider,
 * positive if container > provider, zero if container == provider
 * or if container and provider are non-comparable.
 */
static int
library_cmp_by_symbols( const char *soname,
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
               soname, code, message );
        goto out;
    }

    xasprintf( &symbol_message, "Container Symbols of %s:", soname );
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
               soname, code, message );
        goto out;
    }

    xasprintf( &symbol_message, "Provider Symbols of %s:", soname );
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
               soname );
        cmp_result = 1;
    }
    /* In provider we have strictly more symbols: create the symlink */
    else if( symbol_result == STRING_SET_DIFF_ONLY_IN_SECOND )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the provider is newer because its symbols are a strict superset",
               soname );
        cmp_result = -1;
    }
    /* With the following two cases we are still unsure which library is newer, so we
     * will choose the provider */
    else if( symbol_result == STRING_SET_DIFF_NONE )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have the same symbols",
               soname );
    }
    else
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have different symbols and neither is a superset of the other",
               soname );
    }

out:
    _capsule_clear( &message );
    if( container_symbols )
    {
        for( size_t i = 0; container_symbols[i] != NULL; i++ )
            free( container_symbols[i] );
        free( container_symbols );
    }
    if( provider_symbols )
    {
        for( size_t i = 0; provider_symbols[i] != NULL; i++ )
            free( provider_symbols[i] );
        free( provider_symbols );
    }
    close_elf( &container_elf, &container_fd );
    close_elf( &provider_elf, &provider_fd );
    return cmp_result;

}

/*
 * library_cmp_by_versions:
 * @soname: (type filename): The library we are interested in, used for debug
 * @container_path: (type filename): The path where the container's @soname is
 *  located
 * @provider_path: (type filename): The path where the provider's @soname is
 *  located
 *
 * Attempt to determine whether @soname is older, newer or
 * the same in the container or the provider inspecting their
 * symbols versions.
 *
 * Return a strcmp-style result: negative if container < provider,
 * positive if container > provider, zero if container == provider
 * or if container and provider are non-comparable.
 */
static int
library_cmp_by_versions( const char *soname,
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
               soname, code, message );
        goto out;
    }

    xasprintf( &version_message, "Container versions of %s:", soname);
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
               soname, code, message );
        goto out;
    }

    xasprintf( &version_message, "Provider versions of %s:", soname);
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
               soname );
        cmp_result = 1;
    }
    /* Version in the provider is strictly newer: create the symlink */
    else if( version_result == STRING_SET_DIFF_ONLY_IN_SECOND )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the provider is newer because its version definitions are a strict superset",
               soname );
        cmp_result = -1;
    }
    /* With the following two cases we are still unsure which library is newer */
    else if( version_result == STRING_SET_DIFF_NONE )
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have the same symbol versions",
               soname );
    }
    else
    {
        DEBUG( DEBUG_TOOL,
               "%s in the container and the provider have different symbol versions and neither is a superset of the other",
               soname );
    }

out:
    _capsule_clear( &message );
    if( container_versions )
    {
        for( size_t i = 0; container_versions[i] != NULL; i++ )
            free( container_versions[i] );
        free( container_versions );
    }
    if( provider_versions )
    {
        for( size_t i = 0; provider_versions[i] != NULL; i++ )
            free( provider_versions[i] );
        free( provider_versions );
    }
    close_elf( &container_elf, &container_fd );
    close_elf( &provider_elf, &provider_fd );
    return cmp_result;
}

static int
library_cmp_choose_container( const char *soname,
                              const char *container_path,
                              const char *container_root,
                              const char *provider_path,
                              const char *provider_root )
{
    DEBUG( DEBUG_TOOL,
           "Choosing %s from container \"%s\", ignoring provider \"%s\"",
           soname, container_root, provider_root );
    return 1;
}

static int
library_cmp_choose_provider( const char *soname,
                             const char *container_path,
                             const char *container_root,
                             const char *provider_path,
                             const char *provider_root )
{
    DEBUG( DEBUG_TOOL,
           "Choosing %s from provider \"%s\", ignoring container \"%s\"",
           soname, provider_root, container_root );
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
 * @comparators: (array zero-terminated=1): Functions to compare the libraries
 * @soname: The name under which we searched for the library
 * @container_path: The path to the library in the container
 * @container_root: The path to the top-level directory of the container
 * @provider_path: The path to the library in the provider
 * @provider_root: The path to the top-level directory of the provider
 *
 * Iterate through a %NULL-terminated array of comparators,
 * highest-precedence first, calling each one in turn until one of them
 * returns a nonzero value. If none of them return nonzero, return 0.
 *
 * Returns: Negative if the container version appears newer, zero if they
 *  appear the same or we cannot tell, or positive if the provider version
 *  appears newer.
 */
int
library_cmp_list_iterate( const library_cmp_function *comparators,
                          const char *soname,
                          const char *container_path,
                          const char *container_root,
                          const char *provider_path,
                          const char *provider_root )
{
    const library_cmp_function *iter;

    for( iter = comparators; iter != NULL && *iter != NULL; iter++ )
    {
        int decision = (*iter)( soname,
                                container_path, container_root,
                                provider_path, provider_root );

        if( decision != 0 )
            return decision;
    }

    return 0;
}
