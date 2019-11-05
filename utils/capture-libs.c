// Copyright © 2017 Collabora Ltd
//
// This file is part of libcapsule.
//
// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <glob.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "ld-libs.h"
#include "tools.h"
#include "utils.h"

// We only really care about x86 here because that's the only thing
// libcapsule supports, but we might as well be a bit more complete.
// See https://sourceware.org/glibc/wiki/ABIList
#if defined(__x86_64__) && defined(__ILP32__)
# define LD_SO "/libx32/ld-linux-x32.so.2"
#elif defined(__x86_64__)
# define LD_SO "/lib64/ld-linux-x86-64.so.2"
#elif defined(__sparc__) && defined(__arch64__)
# define LD_SO "/lib64/ld-linux.so.2"
#elif defined(__i386__) || defined(__alpha__) || defined(__sh__) || \
    defined(__sparc__)
# define LD_SO "/lib/ld-linux.so.2"
#elif defined(__aarch64__) && __BYTE_ORDER == __BIG_ENDIAN
# define LD_SO "/lib/ld-linux-aarch64_be.so.1"
#elif defined(__aarch64__)
# define LD_SO "/lib/ld-linux-aarch64.so.1"
#elif defined(__arm__) && defined(__ARM_EABI__) && defined(_ARM_PCS_VFP)
# define LD_SO "/lib/ld-linux-armhf.so.3"
#elif defined(__arm__) && defined(__ARM_EABI__)
# define LD_SO "/lib/ld-linux.so.3"
#elif defined(__hppa__) || defined(__m68k__) || defined(__powerpc__) || \
      defined(__s390__)
# define LD_SO "/lib/ld.so.1"
#elif defined(__powerpc64__) && __BYTE_ORDER == __LITTLE_ENDIAN
# define LD_SO "/lib/ld64.so.2"
#elif defined(__s390x__) || defined(__powerpc64__)
# define LD_SO "/lib/ld64.so.1"
#else
# error Unsupported architecture: we do not know where ld.so is
#endif

static const char * const libc_patterns[] = {
    "soname:libBrokenLocale.so.1",
    "soname:libanl.so.1",
    "soname:libc.so.6",
    "soname:libcidn.so.1",
    "soname:libcrypt.so.1",
    "soname:libdl.so.2",
    "soname:libm.so.6",
    "soname:libmemusage.so",
    "soname:libmvec.so.1",
    "soname:libnsl.so.1",
    "soname:libpcprofile.so",
    "soname:libpthread.so.0",
    "soname:libresolv.so.2",
    "soname:librt.so.1",
    "soname:libthread_db.so.1",
    "soname:libutil.so.1",
    NULL
};

enum
{
  OPTION_CONTAINER,
  OPTION_DEST,
  OPTION_LINK_TARGET,
  OPTION_NO_GLIBC,
  OPTION_PRINT_LD_SO,
  OPTION_PROVIDER,
  OPTION_RESOLVE_LD_SO,
  OPTION_VERSION,
};

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

static const char * const *arg_patterns = NULL;
static const char *option_container = "/";
static const char *option_dest = ".";
static const char *option_provider = "/";
static const char *option_link_target = NULL;
static bool option_glibc = true;

static struct option long_options[] =
{
    { "container", required_argument, NULL, OPTION_CONTAINER },
    { "dest", required_argument, NULL, OPTION_DEST },
    { "help", no_argument, NULL, 'h' },
    { "link-target", required_argument, NULL, OPTION_LINK_TARGET },
    { "no-glibc", no_argument, NULL, OPTION_NO_GLIBC },
    { "print-ld.so", no_argument, NULL, OPTION_PRINT_LD_SO },
    { "provider", required_argument, NULL, OPTION_PROVIDER },
    { "resolve-ld.so", required_argument, NULL, OPTION_RESOLVE_LD_SO },
    { "version", no_argument, NULL, OPTION_VERSION },
    { NULL }
};

static int dest_fd = -1;

#define strstarts(str, start) \
  (strncmp( str, start, strlen( start ) ) == 0)

/* Equivalent to GNU basename(3) from string.h, but not POSIX
 * basename(3) from libgen.h. */
static const char *my_basename (const char *path)
{
  const char *ret = strrchr( path, '/' );

  if( ret == NULL )
      return path;

  assert( ret[0] == '/' );
  return ret + 1;
}

static bool resolve_ld_so ( const char *prefix,
                            char path[PATH_MAX],
                            const char **within_prefix,
                            int *code,
                            char **message )
{
    size_t prefix_len = strlen( prefix );

    if( build_filename( path, PATH_MAX, prefix, LD_SO,
                        NULL ) >= PATH_MAX )
    {
        _capsule_set_error( code, message, E2BIG,
                            "prefix \"%s\" is too long",
                            prefix );
        return false;
    }

    DEBUG( DEBUG_TOOL, "Starting with %s", path );

    while( resolve_link( prefix, path ) )
    {
        DEBUG( DEBUG_TOOL, "-> %s", path );
    }

    if( strcmp( prefix, "/" ) == 0 )
    {
        prefix_len = 0;
    }

    if( ( prefix_len > 0 &&
          strncmp( path, prefix, prefix_len ) != 0 ) ||
        path[prefix_len] != '/' )
    {
        _capsule_set_error( code, message, EXDEV,
                            "\"%s\" is not within prefix \"%s\"",
                            path, prefix );
        return false;
    }

    if( within_prefix != NULL)
        *within_prefix = path + prefix_len;

    return true;
}

static void usage (int code) __attribute__((noreturn));
static void usage (int code)
{
  FILE *fh;

  if( code == 0 )
  {
      fh = stdout;
  }
  else
  {
      fh = stderr;
      // Assume we already printed a warning; make it stand out more
      fprintf( fh, "\n" );
  }

  fprintf( fh, "Usage:\n"
               "%s [OPTIONS] PATTERN...\n",
           program_invocation_short_name );
  fprintf( fh, "\tCreate symbolic links in LIBDIR that will make the\n"
               "\tPATTERNs from PROVIDER available, assuming LIBDIR\n"
               "\twill be added to the container's LD_LIBRARY_PATH.\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "%s --print-ld.so\n",
           program_invocation_short_name );
  fprintf( fh, "\tPrint the ld.so filename for this architecture and exit.\n" );
  fprintf( fh, "%s --resolve-ld.so=TREE\n",
           program_invocation_short_name );
  fprintf( fh, "\tPrint the absolute path of the file that implements ld.so\n"
               "\tin TREE.\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "%s --help\n",
           program_invocation_short_name );
  fprintf( fh, "\tShow this help.\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "Options:\n" );
  fprintf( fh, "--container=CONTAINER\n"
               "\tAssume the container will look like CONTAINER when\n"
               "\tdeciding which libraries are needed [default: /]\n" );
  fprintf( fh, "--dest=LIBDIR\n"
               "\tCreate symlinks in LIBDIR [default: .]\n" );
  fprintf( fh, "--link-target=PATH\n"
               "\tAssume PROVIDER will be mounted at PATH when the\n"
               "\tcontainer is used [default: PROVIDER]\n" );
  fprintf( fh, "--provider=PROVIDER\n"
               "\tFind libraries in PROVIDER [default: /]\n" );
  fprintf( fh, "--no-glibc\n"
               "\tDon't capture libraries that are part of glibc\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "Each PATTERN is one of:\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "soname:SONAME\n"
               "\tCapture the library in ld.so.cache whose name is\n"
               "\texactly SONAME\n" );
  fprintf( fh, "soname-match:GLOB\n"
               "\tCapture every library in ld.so.cache that matches\n"
               "\ta shell-style glob (which will usually need to be\n"
               "\tquoted when using a shell)\n" );
  fprintf( fh, "only-dependencies:PATTERN\n"
               "\tCapture the dependencies of each library matched by\n"
               "\tPATTERN, but not the library matched by PATTERN itself\n"
               "\t(unless a match for PATTERN depends on another match)\n" );
  fprintf( fh, "no-dependencies:PATTERN\n"
               "\tCapture each library matched by PATTERN, but not\n"
               "\ttheir dependencies\n" );
  fprintf( fh, "if-exists:PATTERN\n"
               "\tCapture PATTERN, but don't fail if nothing matches\n" );
  fprintf( fh, "if-same-abi:PATTERN\n"
               "\tCapture PATTERN, but don't fail if it points to a\n"
               "\tlibrary with mismatched word size or architecture\n" );
  fprintf( fh, "even-if-older:PATTERN\n"
               "\tCapture PATTERN, even if the version in CONTAINER\n"
               "\tappears newer\n" );
  fprintf( fh, "gl:\n"
               "\tShortcut for even-if-older:if-exists:soname:libGL.so.1,\n"
               "\teven-if-older:if-exists:soname-match:libGLX_*.so.0, and\n"
               "\tvarious other GL-related libraries\n" );
  fprintf( fh, "path:ABS-PATH\n"
               "\tResolve ABS-PATH as though chrooted into PROVIDER\n"
               "\tand capture the result\n" );
  fprintf( fh, "path-match:GLOB\n"
               "\tResolve GLOB as though chrooted into PROVIDER\n"
               "\tand capture any results that are of the right ABI\n" );
  fprintf( fh, "an absolute path with no '?', '*', '['\n"
               "\tSame as path:PATTERN\n" );
  fprintf( fh, "a glob pattern starting with '/'\n"
               "\tSame as path-match:PATTERN\n" );
  fprintf( fh, "a glob pattern with no '/'\n"
               "\tSame as soname-match:PATTERN\n" );
  fprintf( fh, "a bare SONAME with no '/', '?', '*', '['\n"
               "\tSame as soname:PATTERN\n" );
  exit( code );
}

typedef enum
{
  CAPTURE_FLAG_NONE = 0,
  CAPTURE_FLAG_EVEN_IF_OLDER = (1 << 0),
  CAPTURE_FLAG_IF_EXISTS = (1 << 1),
  CAPTURE_FLAG_LIBRARY_ITSELF = ( 1 << 2 ),
  CAPTURE_FLAG_DEPENDENCIES = ( 1 << 3 ),
  CAPTURE_FLAG_IF_SAME_ABI = ( 1 << 4 ),
} capture_flags;

static bool
init_with_target( ld_libs *ldlibs, const char *tree, const char *target,
                  int *code, char **message )
{
    if( !ld_libs_init( ldlibs, NULL, tree, debug_flags, code, message ) )
    {
        goto fail;
    }

    if( !ld_libs_load_cache( ldlibs, "/etc/ld.so.cache", code, message ) )
    {
        goto fail;
    }

    if( !ld_libs_set_target( ldlibs, target, code, message ) )
    {
        goto fail;
    }

    return true;

fail:
    ld_libs_finish( ldlibs );
    return false;
}

static bool capture_patterns( const char * const *patterns,
                              capture_flags flags,
                              int *code, char **message );

/* Exclude empty symbols */
static int
symbol_excluded( const char *name )
{
    if( name == NULL ||
        !strcmp(name, "") )
        return 1;

    return 0;
}

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
 * parse_map_symbols:
 * @base: (type filename): A `link_map` base address
 * @dyn: (type filename): The dynamic section of the shared object
 * @symbols_number: (out) (not optional): The number of symbols found
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: (transfer container): The list of symbols that the
 *  shared object has, on failure %NULL.
 */
static const char **
parse_map_symbols( ElfW(Addr) base,
                   ElfW(Dyn) *dyn,
                   size_t* symbols_number,
                   int *code, char **message )
{
    size_t x = 0;
    const char *strtab = NULL;
    ElfW(Sym) *symtab = NULL;
    const ElfW(Dyn) *entry = NULL;
    const ElfW(Sym) *entry_sym = NULL;
    /* The array is calloc'd, but the members point into the string table */
    const char **symbols = NULL;

    assert( symbols_number != NULL );

    strtab = dynamic_section_find_strtab( dyn, (const void *) base, NULL );

    if ( strtab == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "String table is unexpectedly missing or inaccessible" );
        return NULL;
    }

    for( entry = dyn; entry->d_tag != DT_NULL; entry++ )
    {
        switch( entry->d_tag )
        {
          case DT_SYMTAB:
            symtab = (ElfW(Sym) *) entry->d_un.d_ptr;
            break;

          default:
            break;
        }
    }

    if ( symtab == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "DT_SYMTAB is unexpectedly missing or inaccessible" );
        return NULL;
    }

    x = 0;
    /* We perform two times the for cycle in order to get the number of items.
     * Skip all the excluded symbols. */
    for( entry_sym = symtab;
         ( (ELFW_ST_TYPE(entry_sym->st_info) < STT_NUM) &&
           (ELFW_ST_BIND(entry_sym->st_info) < STB_NUM) );
         entry_sym++ )
    {
        if ( !symbol_excluded( strtab + entry_sym->st_name ) )
            x++;
    }

    *symbols_number = x;
    symbols = calloc( *symbols_number + 1, sizeof(char *) );

    x = 0;
    for( entry_sym = symtab;
         ( (ELFW_ST_TYPE(entry_sym->st_info) < STT_NUM) &&
           (ELFW_ST_BIND(entry_sym->st_info) < STB_NUM) );
         entry_sym++ )
    {
        if ( !symbol_excluded( strtab + entry_sym->st_name ) )
        {
            symbols[x] = strtab + entry_sym->st_name;
            x++;
        }
    }

    symbols[x] = NULL;
    qsort( symbols, *symbols_number, sizeof(char *), qsort_strcmp_cb );
    return symbols;
}

/*
 * parse_map_versions:
 * @base: (type filename): A `link_map` base address
 * @dyn: (type filename): The dynamic section of the shared object
 * @versions_number: (out) (optional): The number of versions found
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: (transfer container): The list of versions that the
 *  shared object has, on failure %NULL.
 */
static const char **
parse_map_versions( ElfW(Addr) base, ElfW(Dyn) *dyn,
                    size_t* versions_number,
                    int *code, char **message )
{
    size_t verdefnum = (size_t) -1;
    void *start = NULL;
    const ElfW(Verdef) *verdef = NULL;
    const char *strtab = NULL;
    const char *version = NULL;
    ElfW(Verdaux) *aux = NULL;
    const char *vd = NULL;
    const char **versions = NULL;

    start  = (void *) ((ElfW(Addr)) dyn - base );
    strtab = dynamic_section_find_strtab( dyn, (const void *) base, NULL );
    verdefnum = find_value( base, start, DT_VERDEFNUM );
    verdef = (const ElfW(Verdef) *) find_ptr( base, start, DT_VERDEF );

    /* The library doesn't have versions */
    if( verdefnum == (size_t) -1 && verdef == NULL )
    {
        versions = malloc( sizeof(char *) );
        versions[0] = NULL;
        if( versions_number != NULL)
            *versions_number = 0;

        return versions;
    }

    if( verdefnum == (size_t) -1 || verdef == NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "Found one of DT_VERDEF or DT_VERDEFNUM, but not the other" );
        return NULL;
    }

    vd = (const char *) verdef;

    versions = calloc( verdefnum + 1, sizeof(char *) );

    for( size_t x = 0; x < verdefnum; x++ )
    {
        ElfW(Verdef) *the_entry = (ElfW(Verdef) *) vd;
        aux = (ElfW(Verdaux) *) ( vd + the_entry->vd_aux );
        version = strtab + aux->vda_name;
        versions[x] = version;
        vd = vd + the_entry->vd_next;
    }

    versions[verdefnum] = NULL;
    qsort( versions, verdefnum, sizeof(char *), qsort_strcmp_cb );
    if( versions_number != NULL)
        *versions_number = verdefnum;

    return versions;
}

/*
 * get_link_map:
 * @soname: (type filename): The library name to load
 * @path: (type filename): If not %NULL, load libraries as though
 *  this base path was the root directory
 * @ns: (inout) (not optional): The namespace that will be used
 *  to search for @soname
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: A link_map of the loaded shared library @soname,
 *  on failure %NULL.
 */
static struct link_map *
get_link_map ( const char *soname, const char *path,
               Lmid_t *ns, int *code, char **message )
{
    const char *libname;
    ld_libs ldlibs = {};
    void *handle;
    int dlcode = 0;
    struct link_map *map = NULL;

    assert( ns != NULL );

    if( !ld_libs_init( &ldlibs, NULL, path, 0, code, message ) )
        return NULL;

    if( !ld_libs_set_target( &ldlibs, soname, code, message ) )
        return NULL;

    if( ( handle = ld_libs_load( &ldlibs, ns, 0, code, message ) ) )
    {
        if( (libname = strrchr( soname, '/' ) ) )
            libname = libname + 1;
        else
            libname = soname;

        // dl_iterate_phdr won't work with private dlmopen namespaces:
        if( ( dlcode = dlinfo( handle, RTLD_DI_LINKMAP, &map ) ) )
        {
            _capsule_set_error( code, message, EINVAL,
                                "cannot access symbols for %s via handle %p [%d]: %s",
                                libname, handle, dlcode, dlerror() );
            return NULL;
        }
    }
    else
    {
        return NULL;
    }
    return map;
}

/*
 * get_symbols:
 * @soname: (type filename): The library name to load
 * @path: (type filename): If not %NULL, load libraries as though
 *  this base path was the root directory
 * @symbols_number: (out) (not optional): The number of symbols found
 * @ns: (inout) (not optional): The namespace that will be used
 *  to search for @soname
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: (transfer container): The list of symbols that the
 *  shared object has, on failure %NULL.
 */
static const char **
get_symbols ( const char *soname, const char *path, size_t *symbols_number,
              Lmid_t *ns, int *code, char **message )
{
    struct link_map *map;
    const char **symbols = NULL;

    map = get_link_map( soname, path, ns, code, message );

    // We expect to have a pointer directly to the library we are interested in.
    if( map != NULL )
        symbols = parse_map_symbols( map->l_addr, map->l_ld, symbols_number,
                                     code, message );

    return symbols;
}

/*
 * get_versions:
 * @soname: (type filename): The library name to load
 * @path: (type filename): If not %NULL, load libraries as though
 *  this base path was the root directory
 * @versions_number: (out) (optional): The number of versions found
 * @ns: (inout) (not optional): The namespace that will be used
 *  to search for @soname
 * @code: (out) (optional): Used to return an error code on
 *  failure
 * @message: (out) (optional) (nullable): Used to return an error message
 *  on failure
 *
 * Returns: (transfer container): The list of versions that the
 *  shared object has, on failure %NULL.
 */
static const char **
get_versions ( const char *soname, const char *path, size_t* versions_number,
               Lmid_t *ns, int *code, char **message )
{
    struct link_map *map;
    const char **versions = NULL;

    map = get_link_map( soname, path, ns, code, message );

    // We expect to have a pointer directly to the library we are interested in.
    if( map != NULL )
        versions = parse_map_versions( map->l_addr, map->l_ld, versions_number,
                                       code, message );

    return versions;
}

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
compare_string_sets ( const char **first, size_t first_length,
                      const char **second, size_t second_length )
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

/*
 * library_cmp_by_symbols:
 * @soname: The library we are interested in
 * @container_namespace: (inout): The namespace that will be used
 *  to search @soname in the container
 * @provider_namespace: (inout): The namespace that will be used
 *  to search @soname in the provider
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
library_cmp_by_symbols( const char *soname, Lmid_t *container_namespace,
                        Lmid_t *provider_namespace )
{
    const char **container_symbols = NULL;
    const char **provider_symbols = NULL;
    size_t container_symbols_number = 0;
    size_t provider_symbols_number = 0;
    string_set_diff_flags symbol_result = STRING_SET_DIFF_NONE;
    int cmp_result = 0;
    int code = 0;
    char *message = NULL;

    container_symbols = get_symbols( soname, option_container,
                                     &container_symbols_number,
                                     container_namespace, &code, &message );

    if( container_symbols == NULL )
    {
        warnx( "failed to get container symbols for %s (%d): %s",
               soname, code, message );
        goto out;
    }

    provider_symbols = get_symbols( soname, option_provider,
                                    &provider_symbols_number,
                                    provider_namespace, &code, &message );

    if( provider_symbols == NULL )
    {
        warnx( "failed to get provider symbols for %s (%d): %s",
               soname, code, message );
        goto out;
    }

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
        free( container_symbols );
        free( provider_symbols );
        return cmp_result;
}

/*
 * library_cmp_by_versions:
 * @soname: The library we are interested in
 * @container_namespace: (inout): The namespace that will be used
 *  to search @soname in the container
 * @provider_namespace: (inout): The namespace that will be used
 *  to search @soname in the provider
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
library_cmp_by_versions( const char *soname, Lmid_t *container_namespace,
                         Lmid_t *provider_namespace )
{
    const char **container_versions = NULL;
    const char **provider_versions = NULL;
    size_t container_versions_number = 0;
    size_t provider_versions_number = 0;
    string_set_diff_flags version_result = STRING_SET_DIFF_NONE;
    int cmp_result = 0;
    int code = 0;
    char *message = NULL;

    container_versions = get_versions( soname, option_container,
                                       &container_versions_number,
                                       container_namespace, &code, &message );

    if( container_versions == NULL )
    {
        warnx( "failed to get container versions for %s (%d): %s",
               soname, code, message );
        goto out;
    }

    provider_versions = get_versions( soname, option_provider,
                                      &provider_versions_number,
                                      provider_namespace, &code, &message );

    if( provider_versions == NULL )
    {
        warnx( "failed to get provider versions for %s (%d): %s",
               soname, code, message );
        goto out;
    }

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
        code = 0;
        _capsule_clear( &message );
        free( container_versions );
        free( provider_versions );
        return cmp_result;
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
  left_basename = my_basename( left_realpath );
  right_basename = my_basename( right_realpath );

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

static bool
capture_one( const char *soname, capture_flags flags,
             int *code, char **message )
{
    unsigned int i;
    _capsule_cleanup(ld_libs_finish) ld_libs provider = {};
    int local_code = 0;
    _capsule_autofree char *local_message = NULL;
    Lmid_t container_namespace = LM_ID_NEWLM;
    Lmid_t provider_namespace = LM_ID_NEWLM;

    if( !init_with_target( &provider, option_provider, soname,
                           &local_code, &local_message ) )
    {
        if( ( flags & CAPTURE_FLAG_IF_EXISTS ) && local_code == ENOENT )
        {
            DEBUG( DEBUG_TOOL, "%s not found, ignoring", soname );
            _capsule_clear( &local_message );
            return true;
        }

        if( ( flags & CAPTURE_FLAG_IF_SAME_ABI ) && local_code == ENOEXEC )
        {
            DEBUG( DEBUG_TOOL, "%s is a different ABI: %s", soname, local_message );
            _capsule_clear( &local_message );
            return true;
        }

        _capsule_propagate_error( code, message, local_code,
                                  _capsule_steal_pointer( &local_message ) );
        return false;
    }

    if( !ld_libs_find_dependencies( &provider, code, message ) )
    {
        return false;
    }

    for( i = 0; i < N_ELEMENTS( provider.needed ); i++ )
    {
        _capsule_autofree char *target = NULL;
        struct stat statbuf;
        const char *needed_name = provider.needed[i].name;
        const char *needed_path_in_provider = provider.needed[i].path;
        const char *needed_basename;

        if( !needed_name )
        {
            continue;
        }

        if( i == 0 && !( flags & CAPTURE_FLAG_LIBRARY_ITSELF ) )
        {
            DEBUG( DEBUG_TOOL, "Not capturing \"%s\" itself as requested",
                   needed_name );
            continue;
        }

        if( i > 0 && !( flags & CAPTURE_FLAG_DEPENDENCIES ) )
        {
            DEBUG( DEBUG_TOOL,
                   "Not capturing dependencies of \"%s\" as requested",
                   soname );
            break;
        }

        needed_basename = my_basename( needed_name );

        size_t j;
        bool libc_lib = false;
        for( j = 0; j < N_ELEMENTS( libc_patterns ); j++ )
        {
            if( libc_patterns[j] == NULL )
                break;

            assert( strstarts( libc_patterns[j], "soname:" ) );

            if( strcmp( libc_patterns[j] + strlen( "soname:" ),
                        needed_basename ) == 0 )
            {
                libc_lib = true;
                break;
            }
        }

        if( !option_glibc && libc_lib )
        {
            DEBUG( DEBUG_TOOL,
                   "Not capturing \"%s\" because it is part of glibc",
                   needed_name );
            continue;
        }

        if( fstatat( dest_fd, needed_basename, &statbuf,
                     AT_SYMLINK_NOFOLLOW ) == 0 )
        {
            /* We already created a symlink for this library. No further
             * action required (but keep going through its dependencies
             * in case we need to symlink those into place) */
            DEBUG( DEBUG_TOOL, "We already have a symlink for %s",
                   needed_name );
            continue;
        }

        /* For the library we were originally looking for, we don't
         * compare with the container if we have EVEN_IF_OLDER flag.
         * For its dependencies, we ignore that flag. */
        if( option_container == NULL )
        {
            DEBUG( DEBUG_TOOL,
                   "Container unknown, cannot compare version with "
                   "\"%s\": assuming provider version is newer",
                   needed_path_in_provider );
        }
        else if( i == 0 && ( flags & CAPTURE_FLAG_EVEN_IF_OLDER ) )
        {
            DEBUG( DEBUG_TOOL,
                   "Explicitly requested %s from %s even if older: \"%s\"",
                   needed_name, option_provider,
                   needed_path_in_provider );
        }
        else
        {
            _capsule_cleanup(ld_libs_finish) ld_libs container = {};
            if( init_with_target( &container, option_container,
                                  needed_name,
                                  &local_code, &local_message ) )
            {
                const char *needed_path_in_container = container.needed[0].path;
                int decision = 0;

                /* Compare the version definitions.
                 * We skip all libc related libraries to avoid problems with dlmopen() */
                if( !libc_lib )
                {
                    decision = library_cmp_by_versions( needed_name, &container_namespace,
                                                        &provider_namespace);
                }

                /* Compare the numeric tail */
                if( decision == 0 )
                {
                    decision = library_cmp_by_name( needed_name,
                                                    needed_path_in_container,
                                                    option_container,
                                                    needed_path_in_provider,
                                                    option_provider );
                }

                /* Compare the symbols.
                 * We skip all libc related libraries to avoid problems with dlmopen() */
                if( decision == 0 && !libc_lib )
                {
                    decision = library_cmp_by_symbols( needed_name, &container_namespace,
                                                       &provider_namespace);
                }

                /* If the container library is newer (decision > 0) we skip the link creation.
                 * In all the other cases, even if we were unable to determine which library
                 * was newer, we use the one from the provider. */
                if ( decision > 0 )
                    continue;
            }
            else if( local_code == ENOENT )
            {
                /* else assume it's absent from the container, which is
                 * just like it being newer in the provider */
                DEBUG( DEBUG_TOOL, "%s is not in the container",
                       needed_name );
                _capsule_clear( &local_message );
            }
            else
            {
                _capsule_propagate_error( code, message, local_code,
                                          _capsule_steal_pointer( &local_message ) );
                return false;
            }
        }

        /* By this point we've decided we want the version from the
         * provider, not the version from the container */

        if( option_link_target == NULL )
        {
            target = xstrdup( needed_path_in_provider );
        }
        else
        {
            char path[PATH_MAX];
            size_t prefix_len = strlen( option_provider );

            // We need to take the realpath() inside the container,
            // because if we're using LD_LIBRARY_PATH rather than
            // libcapsule, we have to follow the chain of
            // $libdir/libGL.so.1 -> /etc/alternatives/whatever -> ...
            // within that prefix.

            safe_strncpy( path, needed_path_in_provider, sizeof(path) );

            DEBUG( DEBUG_TOOL, "Link target initially: \"%s\"", path );

            while( resolve_link( option_provider, path ) )
            {
                DEBUG( DEBUG_TOOL, "Link target pursued to: \"%s\"", path );
            }

            if( strcmp( option_provider, "/" ) == 0 )
            {
                prefix_len = 0;
            }

            if( strncmp( path, option_provider, prefix_len ) != 0 ||
                path[prefix_len] != '/' )
            {
                warnx( "warning: \"%s\" is not within prefix \"%s\"",
                       path, option_provider );
                continue;
            }

            target = build_filename_alloc( option_link_target,
                                           path + prefix_len,
                                           NULL );
        }

        assert( target != NULL );

        DEBUG( DEBUG_TOOL, "Creating symlink %s/%s -> %s",
               option_dest, needed_basename, target );

        if( symlinkat( target, dest_fd, needed_basename ) < 0 )
        {
            warn( "warning: cannot create symlink %s/%s",
                  option_dest, needed_basename );
        }

        if( strcmp( needed_basename, "libc.so.6" ) == 0 )
        {
            /* Having captured libc, we need to capture the rest of
             * the related libraries from the same place */
            DEBUG( DEBUG_TOOL,
                   "Capturing the rest of glibc to go with %s",
                   needed_name );

            if( !capture_patterns( libc_patterns,
                                  ( flags | CAPTURE_FLAG_IF_EXISTS |
                                    CAPTURE_FLAG_EVEN_IF_OLDER ),
                                  code, message ) )
            {
                return false;
            }
        }
    }

    return true;
}

typedef struct
{
    const char *pattern;
    capture_flags flags;
    bool found;
    ld_cache cache;
    int *code;
    char **message;
} cache_foreach_context;

static intptr_t
cache_foreach_cb (const char *name, int flag, unsigned int osv,
                  uint64_t hwcap, const char *path, void *data)
{
  cache_foreach_context *ctx = data;

  if( !name || !*name )
  {
      warnx( "warning: empty name found in ld.so.cache" );
      return 0;
  }

  // We don't really care about whether the library matches our class,
  // machine, hwcaps etc. - if we can't dlopen a library of this name,
  // we'll just skip it.
  if( fnmatch( ctx->pattern, name, 0 ) == 0 )
  {
      DEBUG( DEBUG_TOOL, "%s matches %s", name, ctx->pattern );

      ctx->found = true;

      if( !capture_one( name, ctx->flags | CAPTURE_FLAG_IF_EXISTS,
                        ctx->code, ctx->message ) )
          return 1;
  }

  return 0;   // continue iteration
}

static bool
capture_soname_match( const char *pattern, capture_flags flags,
                      int *code, char **message )
{
    _capsule_autofree char *cache_path = NULL;
    cache_foreach_context ctx = {
        .pattern = pattern,
        .flags = flags,
        .cache = { .is_open = 0 },
        .found = false,
        .code = code,
        .message = message,
    };
    bool ret = false;

    DEBUG( DEBUG_TOOL, "%s", pattern );

    cache_path = build_filename_alloc( option_provider, "/etc/ld.so.cache",
                                       NULL );

    if( !ld_cache_open( &ctx.cache, cache_path, code, message ) )
        goto out;

    if( ld_cache_foreach( &ctx.cache, cache_foreach_cb, &ctx ) != 0 )
        goto out;

    if( !ctx.found && !( flags & CAPTURE_FLAG_IF_EXISTS ) )
    {
        _capsule_set_error( code, message, ENOENT,
                            "no matches found for glob pattern \"%s\" "
                            "in ld.so.cache",
                            pattern );
        goto out;
    }

    ret = true;

out:
    if( ctx.cache.is_open )
        ld_cache_close( &ctx.cache );

    return ret;
}

static bool
capture_path_match( const char *pattern, capture_flags flags,
                    int *code, char **message )
{
    char *abs_path = NULL;
    int res;
    bool ret = false;
    glob_t buffer;
    size_t i;

    DEBUG( DEBUG_TOOL, "%s", pattern );

    abs_path = build_filename_alloc( option_provider, pattern, NULL );
    res = glob( abs_path, 0, NULL, &buffer );

    switch( res )
    {
        case 0:
            ret = true;
            for( i = 0; i < buffer.gl_pathc; i++)
            {
                const char *path = buffer.gl_pathv[i];

                if( option_provider != NULL &&
                    strcmp( option_provider, "/" ) != 0 &&
                    ( !strstarts( path, option_provider ) ||
                      path[strlen( option_provider )] != '/' ) )
                {
                  _capsule_set_error( code, message, EXDEV,
                                      "path pattern \"%s\" matches \"%s\""
                                      "which is not in \"%s\"",
                                      pattern, path, option_provider );
                  globfree( &buffer );
                  return false;
                }

                if( !capture_one( path + strlen( option_provider ),
                                  flags | CAPTURE_FLAG_IF_SAME_ABI,
                                  code, message ) )
                {
                    ret = false;
                    break;
                }
            }
            globfree( &buffer );
            break;

        case GLOB_NOMATCH:
            if( flags & CAPTURE_FLAG_IF_EXISTS )
            {
                ret = true;
            }
            else
            {
                _capsule_set_error( code, message, ENOENT,
                                    "no matches found for glob pattern \"%s\" "
                                    "in \"%s\"",
                                    pattern, option_provider );
            }
            break;

        case GLOB_NOSPACE:
            _capsule_set_error( code, message, ENOMEM,
                                "unable to match glob pattern \"%s\" "
                                "in \"%s\"",
                                pattern, option_provider );
            break;

        default:
        case GLOB_ABORTED:
            _capsule_set_error( code, message, EIO,
                                "unable to match glob pattern \"%s\" "
                                "in \"%s\"",
                                pattern, option_provider );
            break;
    }

    free( abs_path );
    return ret;
}

static bool
capture_pattern( const char *pattern, capture_flags flags,
                 int *code, char **message )
{
    DEBUG( DEBUG_TOOL, "%s", pattern );

    if ( ( flags & ( CAPTURE_FLAG_LIBRARY_ITSELF |
                     CAPTURE_FLAG_DEPENDENCIES ) ) == 0 )
    {
        _capsule_set_error( code, message, EINVAL,
                            "combining no-dependencies: with "
                            "only-dependencies: is meaningless, "
                            "so \"%s\" is invalid",
                            pattern );
        return false;
    }

    if( strstarts( pattern, "path:" ) )
    {
        if( !strstarts( pattern, "path:/" ) )
        {
            _capsule_set_error( code, message, EINVAL,
                                "path: requires an absolute path as "
                                "argument, not \"%s\"",
                                pattern );
            return false;
        }

        return capture_one( pattern + strlen( "path:" ),
                            flags, code, message );
    }

    if( strstarts( pattern, "soname:" ) )
    {
        return capture_one( pattern + strlen( "soname:" ),
                            flags, code, message );
    }

    if( strstarts( pattern, "soname-match:" ) )
    {
        return capture_soname_match( pattern + strlen( "soname-match:" ),
                                     flags, code, message );
    }

    if( strstarts( pattern, "path-match:" ) )
    {
        return capture_path_match( pattern + strlen( "path-match:" ),
                                   flags, code, message );
    }

    if( strstarts( pattern, "if-exists:" ) )
    {
        return capture_pattern( pattern + strlen( "if-exists:" ),
                                flags | CAPTURE_FLAG_IF_EXISTS,
                                code, message );
    }

    if( strstarts( pattern, "if-same-abi:" ) )
    {
        return capture_pattern( pattern + strlen( "if-same-abi:" ),
                                flags | CAPTURE_FLAG_IF_SAME_ABI,
                                code, message );
    }

    if( strstarts( pattern, "even-if-older:" ) )
    {
        return capture_pattern( pattern + strlen( "even-if-older:" ),
                                flags | CAPTURE_FLAG_EVEN_IF_OLDER,
                                code, message );
    }

    if( strstarts( pattern, "only-dependencies:" ) )
    {
        return capture_pattern( pattern + strlen( "only-dependencies:" ),
                                flags & ~CAPTURE_FLAG_LIBRARY_ITSELF,
                                code, message );
    }

    if( strstarts( pattern, "no-dependencies:" ) )
    {
        return capture_pattern( pattern + strlen( "no-dependencies:" ),
                                flags & ~CAPTURE_FLAG_DEPENDENCIES,
                                code, message );
    }

    if( strcmp( pattern, "gl:" ) == 0 )
    {
        // Useful information:
        // https://devtalk.nvidia.com/default/topic/915640/multiple-glx-client-libraries-in-the-nvidia-linux-driver-installer-package/
        static const char * const gl_patterns[] = {
            "soname:libEGL.so.1",
            // Vendor ICDs for libEGL.so.1
            // (Registered via JSON in /usr/share/glvnd/egl_vendor.d)
            "soname-match:libEGL_*.so.*",

            "soname:libGL.so.1",

            "soname:libGLESv1_CM.so.1",
            // Vendor ICDs for libGLESv1_CM.so.1
            "soname-match:libGLESv1_CM_*.so.*",

            "soname:libGLESv2.so.2",
            // Vendor ICDs for libGLESv2.so.2
            "soname:libGLESv2_*.so.*",

            "soname:libGLX.so.0",
            // Vendor ICDs for libGL.so.1 and/or libGLX.so.0
            "soname-match:libGLX_*.so.*",
            // This one looks redundant, but because it's usually a
            // symlink to someone else's implementation, we can't find
            // it in the ld.so cache under its own name: its SONAME is
            // libGLX_mesa.so.0 or libGLX_nvidia.so.0. So we can't find
            // it by wildcard-matching and have to look it up explicitly
            // instead.
            "soname:libGLX_indirect.so.0",

            // This is an implementation detail of GLVND, but it had
            // better match the GLVND dispatchers or bad things will
            // happen
            "soname-match:libGLdispatch.so.*",

            "soname:libOpenGL.so.0",

            // Mostly used by Mesa, but apps/games are also allowed to
            // use it directly
            "soname:libgbm.so.1",

            // Mesa libraries should have DT_NEEDED for this, but some
            // historical versions didn't, so it wouldn't be picked up
            // by recursive dependency resolution
            "soname:libglapi.so.0",

            // Some libraries are not explicitly mentioned here:
            // For NVIDIA, we also need libnvidia-glcore.so.$VERSION,
            // but it will be pulled in by dependencies, so we don't
            // need to list it explicitly.
            // For NVIDIA, we also need libnvidia-tls.so.$VERSION,
            // either the TLS or non-TLS version as appropriate; but
            // again it will be pulled in via dependencies.
            NULL
        };

        /* We usually want to capture the host GL stack even if it
         * appears older than what's in the container. */
        return capture_patterns( gl_patterns,
                                ( flags | CAPTURE_FLAG_IF_EXISTS |
                                  CAPTURE_FLAG_EVEN_IF_OLDER ),
                                code, message );
    }

    if( strcmp( pattern, "nvidia:" ) == 0 )
    {
        static const char * const gl_patterns[] = {
            "soname:libEGL.so.1",
            "soname-match:libEGL_nvidia.so.*",

            "soname:libGL.so.1",

            "soname:libGLESv1_CM.so.1",
            "soname-match:libGLESv1_CM_nvidia.so.*",

            "soname:libGLESv2.so.2",
            "soname-match:libGLESv2_nvidia.so.*",

            "soname:libGLX.so.0",
            "soname-match:libGLX_nvidia.so.*",
            "soname:libGLX_indirect.so.0",

            "soname-match:libGLdispatch.so.*",

            "soname:libOpenGL.so.0",

            "soname-match:libcuda.so.*",
            "soname-match:libglx.so.*",
            "soname-match:libnvcuvid.so.*",
            "soname-match:libnvidia-*.so.*",
            "soname-match:libOpenCL.so.*",
            "soname-match:libvdpau_nvidia.so.*",
            NULL
        };

        /* We certainly want to capture the host GL stack even if it
         * appears older than what's in the container: the NVIDIA
         * proprietary drivers have to be in lockstep with the kernel. */
        return capture_patterns( gl_patterns,
                                ( flags | CAPTURE_FLAG_IF_EXISTS |
                                  CAPTURE_FLAG_EVEN_IF_OLDER ),
                                code, message );
    }

    if( strchr( pattern, ':' ) != NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "patterns containing ':' must match a known "
                            "mode, not \"%s\" (use soname: or path: to "
                            "take patterns containing ':' literally, if "
                            "necessary)",
                            pattern );
        return false;
    }

    if( pattern[0] == '/' )
    {
        if( strchr( pattern, '*' ) != NULL ||
            strchr( pattern, '?' ) != NULL ||
            strchr( pattern, '[' ) != NULL )
        {
            // Interpret as if path-match:
            return capture_path_match( pattern, flags, code, message );
        }
        else
        {
            return capture_one( pattern, flags, code, message );
        }
    }

    if( strchr( pattern, '/' ) != NULL )
    {
        _capsule_set_error( code, message, EINVAL,
                            "path arguments must be absolute, not \"%s\"",
                            pattern );
        return false;
    }

    if( strchr( pattern, '*' ) != NULL ||
        strchr( pattern, '?' ) != NULL ||
        strchr( pattern, '[' ) != NULL )
    {
        // Interpret as if soname-match:
        return capture_soname_match( pattern, flags, code, message );
    }

    // Default: interpret as if soname:
    return capture_one( pattern, flags, code, message );
}

static bool
capture_patterns( const char * const *patterns, capture_flags flags,
                  int *code, char **message )
{
    unsigned int i;

    for( i = 0; patterns[i] != NULL; i++ )
    {
        if( !capture_pattern( patterns[i], flags, code, message ) )
            return false;
    }

    return true;
}

int
main (int argc, char **argv)
{
    capture_flags flags = (CAPTURE_FLAG_LIBRARY_ITSELF |
                           CAPTURE_FLAG_DEPENDENCIES );
    int code = 0;
    char *message = NULL;

    set_debug_flags( getenv("CAPSULE_DEBUG") );

    while( 1 )
    {
        int opt = getopt_long( argc, argv, "h", long_options, NULL );

        if( opt == -1 )
        {
            break;
        }

        switch( opt )
        {
            case '?':
            default:
                usage( 2 );
                break;  // not reached

            case 'h':
                usage( 0 );
                break;  // not reached

            case OPTION_CONTAINER:
                option_container = optarg;
                break;

            case OPTION_DEST:
                option_dest = optarg;
                break;

            case OPTION_LINK_TARGET:
                option_link_target = optarg;
                break;

            case OPTION_PRINT_LD_SO:
                puts( LD_SO );
                return 0;

            case OPTION_PROVIDER:
                option_provider = optarg;
                break;

            case OPTION_NO_GLIBC:
                option_glibc = false;
                break;

            case OPTION_VERSION:
                _capsule_tools_print_version( "capsule-capture-libs" );
                return 0;

            case OPTION_RESOLVE_LD_SO:
                {
                    char path[PATH_MAX] = { 0 };
                    const char *within_prefix = NULL;

                    if( !resolve_ld_so( optarg, path, &within_prefix,
                                        &code, &message ) )
                    {
                        errx( 1, "code %d: %s", code, message );
                    }

                    puts( within_prefix );
                    return 0;
                }
                break;
        }
    }

    if( optind >= argc )
    {
        warnx( "One or more patterns must be provided" );
        usage( 2 );
    }

    arg_patterns = (const char * const *) argv + optind;
    assert( arg_patterns[argc - optind] == NULL );

    if( strcmp( option_dest, "." ) != 0 &&
        mkdir( option_dest, 0755 ) < 0 &&
        errno != EEXIST )
    {
        err( 1, "creating \"%s\"", option_dest );
    }

    dest_fd = open( option_dest, O_RDWR|O_DIRECTORY|O_CLOEXEC|O_PATH );

    if( dest_fd < 0 )
    {
        err( 1, "opening \"%s\"", option_dest );
    }

    if( !capture_patterns( arg_patterns, flags, &code, &message ) )
    {
        errx( 1, "code %d: %s", code, message );
    }

    close( dest_fd );

    return 0;
}
