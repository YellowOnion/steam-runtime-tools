// Copyright © 2017-2021 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later
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
#include "library-cmp.h"
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
// microblaze is also /lib/ld.so.1
// mips classic NaN, o32 is also /lib/ld.so.1
# define LD_SO "/lib/ld.so.1"
#elif defined(__powerpc64__) && __BYTE_ORDER == __LITTLE_ENDIAN
# define LD_SO "/lib/ld64.so.2"
#elif defined(__s390x__) || defined(__powerpc64__)
# define LD_SO "/lib/ld64.so.1"
// Others not supported here because we don't know which predefined macros
// can be used to detect them:
// C-SKY hardfloat: /lib/ld-linux-cskyv2-hf.so.1
// C-SKY softfloat: /lib/ld-linux-cskyv2.so.1
// ia64: /lib/ld-linux-ia64.so.2
// mips classic NaN n32: /lib32/ld.so.1
// mips classic NaN n64: /lib64/ld.so.1
// mips NaN2008: as for classic NaN but basename is ld-linux-mipsn8.so.1
// nios2: /lib/ld-linux-nios2.so.1
// riscv64 softfloat: /lib/ld-linux-riscv64-lp64.so.1
// riscv64 hardfloat: /lib/ld-linux-riscv64-lp64d.so.1
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
  OPTION_COMPARE_BY,
  OPTION_CONTAINER,
  OPTION_DEST,
  OPTION_LIBRARY_KNOWLEDGE,
  OPTION_LINK_TARGET,
  OPTION_NO_GLIBC,
  OPTION_PRINT_LD_SO,
  OPTION_PROVIDER,
  OPTION_REMAP_LINK_PREFIX,
  OPTION_RESOLVE_LD_SO,
  OPTION_VERSION,
};

typedef struct {
    char *from;
    const char *to;
} remap_tuple;

static const char * const *arg_patterns = NULL;
static const char *option_container = "/";
static const char *option_dest = ".";
static const char *option_provider = "/";
static const char *option_link_target = NULL;
static remap_tuple *remap_prefix = NULL;
static bool option_glibc = true;

static struct option long_options[] =
{
    { "compare-by", required_argument, NULL, OPTION_COMPARE_BY },
    { "container", required_argument, NULL, OPTION_CONTAINER },
    { "dest", required_argument, NULL, OPTION_DEST },
    { "help", no_argument, NULL, 'h' },
    { "library-knowledge", required_argument, NULL, OPTION_LIBRARY_KNOWLEDGE },
    { "link-target", required_argument, NULL, OPTION_LINK_TARGET },
    { "no-glibc", no_argument, NULL, OPTION_NO_GLIBC },
    { "print-ld.so", no_argument, NULL, OPTION_PRINT_LD_SO },
    { "provider", required_argument, NULL, OPTION_PROVIDER },
    { "remap-link-prefix", required_argument, NULL, OPTION_REMAP_LINK_PREFIX },
    { "resolve-ld.so", required_argument, NULL, OPTION_RESOLVE_LD_SO },
    { "version", no_argument, NULL, OPTION_VERSION },
    { NULL }
};

static int dest_fd = -1;

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
  fprintf( fh, "--compare-by=METHOD[,METHOD2...]\n"
               "\tUse METHOD by default to decide which library is newer.\n"
               "\tIf unable to decide, use METHOD2, and so on.\n"
               "\tIf unable to decide by any method, choose PROVIDER.\n"
               "\tThe default is 'name,provider'.\n"
               "\t\tname: Use library name: libfoo.so.1.0 < libfoo.so.1.2\n"
               "\t\tversions: The one with a superset of DT_VERDEF is newer\n"
               "\t\tsymbols: The one with a superset of symbols is newer\n"
               "\t\tcontainer: The one in CONTAINER is newer\n"
               "\t\tprovider: The one in PROVIDER is newer\n" );
  fprintf( fh, "--container=CONTAINER\n"
               "\tAssume the container will look like CONTAINER when\n"
               "\tdeciding which libraries are needed [default: /]\n" );
  fprintf( fh, "--dest=LIBDIR\n"
               "\tCreate symlinks in LIBDIR [default: .]\n" );
  fprintf( fh, "--library-knowledge=FILE\n"
               "\tLoad information about known libraries from a"
               "\t.desktop-style file at FILE, overriding --compare-by.\n" );
  fprintf( fh, "--link-target=PATH\n"
               "\tAssume PROVIDER will be mounted at PATH when the\n"
               "\tcontainer is used [default: PROVIDER]\n" );
  fprintf( fh, "--provider=PROVIDER\n"
               "\tFind libraries in PROVIDER [default: /]\n" );
  fprintf( fh, "--remap-link-prefix=FROM=TO\n"
               "\tWhile in the process of creating symlinks, if their prefix\n"
               "\twas supposed to be FROM, they will instead be changed with\n"
               "\tTO\n" );
  fprintf( fh, "--no-glibc\n"
               "\tDon't capture libraries that are part of glibc\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "Each PATTERN is one of:\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "from:FILE\n"
               "\tRead PATTERNs from FILE, one per line.\n" );
  fprintf( fh, "soname:SONAME\n"
               "\tCapture the library in ld.so.cache whose name is\n"
               "\texactly SONAME\n" );
  fprintf( fh, "exact-soname:SONAME\n"
               "\tStricter version of \"soname:\" that capture the library\n"
               "\tin ld.so.cache only if the DT_SONAME is an exact match\n"
               "\tcompared to what was initially requested\n" );
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
  CAPTURE_FLAG_IF_EXACT_SONAME = ( 1 << 5 ),
} capture_flags;

typedef struct
{
    capture_flags flags;
    library_cmp_function *comparators;
    library_knowledge knowledge;
} capture_options;

static bool
init_with_target( ld_libs *ldlibs, const char *tree, const char *target,
                  int *code, char **message )
{
    if( !ld_libs_init( ldlibs, NULL, tree, debug_flags, code, message ) )
    {
        goto fail;
    }

    if( !ld_libs_load_cache( ldlibs, code, message ) )
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

static bool
library_belongs_to_glibc( const char *soname )
{
    unsigned int i;

    for( i = 0; i < N_ELEMENTS( libc_patterns ); i++ )
    {
        if( libc_patterns[i] == NULL )
            break;

        assert( strstarts( libc_patterns[i], "soname:" ) );

        if( strcmp( libc_patterns[i] + strlen( "soname:" ), soname ) == 0 )
            return true;
    }

    return false;
}

static bool capture_pattern( const char *pattern,
                             const capture_options *options,
                             int *code, char **message );

static bool capture_patterns( const char * const *patterns,
                              const capture_options *options,
                              int *code, char **message );

static bool
capture_one( const char *soname, const capture_options *options,
             int *code, char **message )
{
    unsigned int i;
    unsigned int j;
    _capsule_cleanup(ld_libs_finish) ld_libs provider = {};
    int local_code = 0;
    _capsule_autofree char *local_message = NULL;

    if( !init_with_target( &provider, option_provider, soname,
                           &local_code, &local_message ) )
    {
        if( ( options->flags & CAPTURE_FLAG_IF_EXISTS ) && local_code == ENOENT )
        {
            DEBUG( DEBUG_TOOL, "%s not found, ignoring", soname );
            _capsule_clear( &local_message );
            return true;
        }

        if( ( options->flags & CAPTURE_FLAG_IF_SAME_ABI ) && local_code == ENOEXEC )
        {
            DEBUG( DEBUG_TOOL, "%s is a different ABI: %s", soname, local_message );
            _capsule_clear( &local_message );
            return true;
        }

        _capsule_propagate_error( code, message, local_code,
                                  _capsule_steal_pointer( &local_message ) );
        return false;
    }

    if( options->flags & CAPTURE_FLAG_IF_EXACT_SONAME )
    {
        const char *dt_soname = NULL;
        Elf_Scn *scn = NULL;

        while( ( scn = elf_nextscn( provider.needed[0].dso, scn ) ) != NULL &&
               dt_soname == NULL )
        {
            Elf_Data *edata = NULL;
            GElf_Dyn dyn = {};
            GElf_Shdr shdr = {};
            i = 0;
            gelf_getshdr( scn, &shdr );

            if( shdr.sh_type != SHT_DYNAMIC )
                continue;

            edata = elf_getdata( scn, edata );

            while( gelf_getdyn( edata, i++, &dyn ) &&
                   dyn.d_tag != DT_NULL )
            {
                if( dyn.d_tag == DT_SONAME )
                {
                    dt_soname = elf_strptr( provider.needed[0].dso, shdr.sh_link,
                                            dyn.d_un.d_val );
                    break;
                }
            }
        }

        if( dt_soname == NULL )
        {
            if( ( options->flags & CAPTURE_FLAG_IF_EXISTS ) )
            {
                DEBUG( DEBUG_TOOL,
                       "Unable to obtain the library %s DT_SONAME, ignoring",
                       soname );
                return true;
            }

            xasprintf( &local_message, "Unable to obtain the library %s DT_SONAME",
                       soname );
            _capsule_propagate_error( code, message, EIO,
                                      _capsule_steal_pointer( &local_message ) );
            return false;
        }

        if( strcmp( dt_soname, soname ) != 0 )
        {
            if( ( options->flags & CAPTURE_FLAG_IF_EXISTS ) )
            {
                DEBUG( DEBUG_TOOL, "%s has a different DT_SONAME: %s",
                       soname, dt_soname );
                return true;
            }

            xasprintf( &local_message, "%s has an unexpected DT_SONAME: %s",
                       soname, dt_soname );
            _capsule_propagate_error( code, message, EIO,
                                      _capsule_steal_pointer( &local_message ) );
            return false;
        }
    }

    if( !ld_libs_find_dependencies( &provider, &local_code, &local_message ) )
    {
        if( ( options->flags & CAPTURE_FLAG_IF_EXISTS ) && local_code == ENOENT )
        {
            DEBUG( DEBUG_TOOL,
                   "Some of the dependencies for %s have not been found, ignoring",
                   soname );
            _capsule_clear( &local_message );
            return true;
        }

        _capsule_propagate_error( code, message, local_code,
                                  _capsule_steal_pointer( &local_message ) );
        return false;
    }

    for( i = 0; i < N_ELEMENTS( provider.needed ); i++ )
    {
        _capsule_autofree char *target = NULL;
        struct stat statbuf;
        const char *needed_name = provider.needed[i].name;
        const char *needed_path_in_provider = provider.needed[i].path;
        const char *needed_basename;
        bool remapped_prefix = false;

        if( !needed_name )
        {
            continue;
        }

        if( i == 0 && !( options->flags & CAPTURE_FLAG_LIBRARY_ITSELF ) )
        {
            DEBUG( DEBUG_TOOL, "Not capturing \"%s\" itself as requested",
                   needed_name );
            continue;
        }

        if( i > 0 && !( options->flags & CAPTURE_FLAG_DEPENDENCIES ) )
        {
            DEBUG( DEBUG_TOOL,
                   "Not capturing dependencies of \"%s\" as requested",
                   soname );
            break;
        }

        needed_basename = _capsule_basename( needed_name );

        if( !option_glibc
            && library_belongs_to_glibc( needed_basename ) )
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

        if( option_glibc
            /* CAPTURE_FLAG_EVEN_IF_OLDER only applies to the library itself,
             * not its dependencies */
            && ( i != 0 || ( options->flags & CAPTURE_FLAG_EVEN_IF_OLDER ) == 0 )
            && strcmp( needed_basename, "libc.so.6" ) != 0
            && library_belongs_to_glibc( needed_basename ) )
        {
            /* Don't do anything with glibc sub-libraries: when glibc
             * is version 2.34 or later, they might be stubs that are
             * difficult to compare. Instead, wait until we process their
             * glibc dependency later. If we choose to use the glibc from the
             * provider, then we'll capture the rest of the glibc family,
             * including needed_basename, as a side-effect (this time
             * with CAPTURE_FLAG_EVEN_IF_OLDER, so this block will
             * be skipped). */
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
        else if( i == 0 && ( options->flags & CAPTURE_FLAG_EVEN_IF_OLDER ) )
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
                int decision;
                library_details details = {};
                const library_details *known = NULL;

                if( strcmp( needed_basename, "libc.so.6" ) == 0 )
                {
                    /* Starting from glibc 2.34, libc.so.6 is a regular file instead of a
                     * symbolic link. For this reason a comparison by name is not
                     * enough anymore. Force the hard-coded glibc comparator instead
                     * of the provided knowledge values */
                    details = library_details_for_glibc;
                }
                else
                {
                    known = library_knowledge_lookup( &options->knowledge,
                                                      needed_name );

                    if( known != NULL )
                    {
                        DEBUG( DEBUG_TOOL, "Found library-specific details for \"%s\"",
                               needed_name );
                        details = *known;
                    }

                    /* Not const-correct, but we don't actually modify or free it */
                    details.name = (char *) needed_name;

                    if( details.comparators == NULL )
                        details.comparators = options->comparators;
                }

                decision = library_cmp_list_iterate( &details,
                                                     needed_path_in_container,
                                                     option_container,
                                                     needed_path_in_provider,
                                                     option_provider );

                if( decision > 0 )
                {
                    /* Version in container is strictly newer: don't
                     * symlink in the one from the provider */
                    DEBUG( DEBUG_TOOL,
                           "Choosing %s from container",
                           needed_name );
                    continue;
                }
                else if( decision < 0 )
                {
                    DEBUG( DEBUG_TOOL,
                           "Choosing %s from provider",
                           needed_name );
                }
                else
                {
                    /* If equal, we prefer the provider over the container
                     * (this is equivalent to having "...,provider" at the
                     * end of the comparison specification) */
                    DEBUG( DEBUG_TOOL,
                           "Falling back to choosing %s from provider",
                           needed_name );
                }
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

        if( option_link_target != NULL || remap_prefix != NULL )
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

            target = build_filename_alloc( option_link_target ? option_link_target : "/",
                                           path + prefix_len,
                                           NULL );

            for( j = 0; remap_prefix != NULL && remap_prefix[j].from != NULL; j++ )
            {
                if( strstarts( target, remap_prefix[j].from ) )
                {
                    char *tmp;
                    assert( remap_prefix[j].to != NULL );
                    DEBUG( DEBUG_TOOL, "Remapping \"%s\" to \"%s\" in \"%s\"",
                           remap_prefix[j].from, remap_prefix[j].to, target );
                    xasprintf( &tmp, "%s%s", remap_prefix[j].to,
                               target + strlen( remap_prefix[j].from ) );
                    free( target );
                    target = tmp;
                    remapped_prefix = true;
                }
            }
        }

        // If we don't have the link target option and we didn't remap the
        // prefix, we just set the target to the needed path in provider
        // without following the eventual link chain
        if( !remapped_prefix && option_link_target == NULL )
        {
            if( target != NULL )
                free( target );
            target = xstrdup( needed_path_in_provider );
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
            capture_options new_options;

            /* Having captured libc, we need to capture the rest of
             * the related libraries from the same place */
            DEBUG( DEBUG_TOOL,
                   "Capturing the rest of glibc to go with %s",
                   needed_name );

            new_options = *options;
            new_options.flags |= CAPTURE_FLAG_IF_EXISTS;
            new_options.flags |= CAPTURE_FLAG_EVEN_IF_OLDER;

            /* Exact SONAME is not expected to be used for the dependencies */
            new_options.flags &= ~CAPTURE_FLAG_IF_EXACT_SONAME;

            if( !capture_patterns( libc_patterns, &new_options,
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
    const capture_options *options;
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
      capture_options new_options;

      DEBUG( DEBUG_TOOL, "%s matches %s", name, ctx->pattern );

      ctx->found = true;

      new_options = *ctx->options;
      new_options.flags |= CAPTURE_FLAG_IF_EXISTS;

      if( !capture_one( name, &new_options, ctx->code, ctx->message ) )
          return 1;
  }

  return 0;   // continue iteration
}

static bool
capture_soname_match( const char *pattern, const capture_options *options,
                      int *code, char **message )
{
    cache_foreach_context ctx = {
        .pattern = pattern,
        .options = options,
        .cache = { .is_open = 0 },
        .found = false,
        .code = code,
        .message = message,
    };
    bool ret = false;
    size_t i;

    DEBUG( DEBUG_TOOL, "%s", pattern );

    for( i = 0; ld_cache_filenames[i] != NULL; i++ )
    {
        _capsule_autofree char *cache_path = NULL;

        if( message != NULL )
            _capsule_clear( message );

        cache_path = build_filename_alloc( option_provider,
                                           ld_cache_filenames[i],
                                           NULL );

        if( ld_cache_open( &ctx.cache, cache_path, code, message ) )
            break;
        else
            assert( !ctx.cache.is_open );
    }

    if( !ctx.cache.is_open )
        goto out;

    if( ld_cache_foreach( &ctx.cache, cache_foreach_cb, &ctx ) != 0 )
        goto out;

    if( !ctx.found && !( options->flags & CAPTURE_FLAG_IF_EXISTS ) )
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
capture_path_match( const char *pattern, const capture_options *options,
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
                capture_options new_options;

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

                new_options = *options;
                new_options.flags |= CAPTURE_FLAG_IF_SAME_ABI;

                if( !capture_one( path + strlen( option_provider ),
                                  &new_options, code, message ) )
                {
                    ret = false;
                    break;
                }
            }
            globfree( &buffer );
            break;

        case GLOB_NOMATCH:
            if( options->flags & CAPTURE_FLAG_IF_EXISTS )
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
capture_lines( const char *filename, const capture_options *options,
               int *code, char **message )
{
  FILE *fp = NULL;
  bool ret = true;
  char *line = NULL;
  size_t len = 0;
  ssize_t chars;

  if( strcmp( filename, "-" ) != 0 )
  {
      fp = fopen( filename, "re" );

      if( fp == NULL )
      {
          _capsule_set_error( code, message, errno,
                              "Unable to open \"%s\": %s",
                              filename, strerror( errno ) );
          return false;
      }
  }

  while( ( chars = getline( &line, &len, fp ? fp : stdin ) ) > -1 )
  {
      /* Ignore blank lines and shell-style comments (which must
       * currently be at the beginning of the line) */
      if( chars == 0 || line[0] == '\0' || line[0] == '\n' || line[0] == '#' )
          continue;

      if( line[chars - 1] == '\n' )
          line[chars - 1] = '\0';

      if( !capture_pattern( line, options, code, message ) )
      {
          ret = false;
          break;
      }
  }

  free( line );

  if( fp != NULL )
      fclose( fp );

  return ret;
}

static bool
capture_pattern( const char *pattern, const capture_options *options,
                 int *code, char **message )
{
    DEBUG( DEBUG_TOOL, "%s", pattern );

    if ( ( options->flags & ( CAPTURE_FLAG_LIBRARY_ITSELF |
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
                            options, code, message );
    }

    if( strstarts( pattern, "soname:" ) )
    {
        return capture_one( pattern + strlen( "soname:" ),
                            options, code, message );
    }

    if( strstarts( pattern, "exact-soname:" ) )
    {
        capture_options new_options = *options;

        new_options.flags |= CAPTURE_FLAG_IF_EXACT_SONAME;
        return capture_one( pattern + strlen( "exact-soname:" ),
                            &new_options, code, message );
    }

    if( strstarts( pattern, "soname-match:" ) )
    {
        return capture_soname_match( pattern + strlen( "soname-match:" ),
                                     options, code, message );
    }

    if( strstarts( pattern, "path-match:" ) )
    {
        return capture_path_match( pattern + strlen( "path-match:" ),
                                   options, code, message );
    }

    if( strstarts( pattern, "if-exists:" ) )
    {
        capture_options new_options = *options;

        new_options.flags |= CAPTURE_FLAG_IF_EXISTS;
        return capture_pattern( pattern + strlen( "if-exists:" ),
                                &new_options, code, message );
    }

    if( strstarts( pattern, "if-same-abi:" ) )
    {
        capture_options new_options = *options;

        new_options.flags |= CAPTURE_FLAG_IF_SAME_ABI;
        return capture_pattern( pattern + strlen( "if-same-abi:" ),
                                &new_options, code, message );
    }

    if( strstarts( pattern, "even-if-older:" ) )
    {
        capture_options new_options = *options;

        new_options.flags |= CAPTURE_FLAG_EVEN_IF_OLDER;
        return capture_pattern( pattern + strlen( "even-if-older:" ),
                                &new_options, code, message );
    }

    if( strstarts( pattern, "only-dependencies:" ) )
    {
        capture_options new_options = *options;

        new_options.flags &= ~CAPTURE_FLAG_LIBRARY_ITSELF;
        return capture_pattern( pattern + strlen( "only-dependencies:" ),
                                &new_options, code, message );
    }

    if( strstarts( pattern, "no-dependencies:" ) )
    {
        capture_options new_options = *options;

        new_options.flags &= ~CAPTURE_FLAG_DEPENDENCIES;
        return capture_pattern( pattern + strlen( "no-dependencies:" ),
                                &new_options, code, message );
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
            "soname-match:libGLESv2_*.so.*",

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
        capture_options new_options = *options;

        new_options.flags |= CAPTURE_FLAG_IF_EXISTS;
        /* We usually want to capture the host GL stack even if it
         * appears older than what's in the container. */
        new_options.flags |= CAPTURE_FLAG_EVEN_IF_OLDER;

        return capture_patterns( gl_patterns, &new_options, code, message );
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
        capture_options new_options = *options;

        new_options.flags |= CAPTURE_FLAG_IF_EXISTS;
        /* We certainly want to capture the host GL stack even if it
         * appears older than what's in the container: the NVIDIA
         * proprietary drivers have to be in lockstep with the kernel. */
        new_options.flags |= CAPTURE_FLAG_EVEN_IF_OLDER;

        return capture_patterns( gl_patterns, &new_options, code, message );
    }

    if( strstarts( pattern, "from:" ) )
    {
        return capture_lines( pattern + strlen( "from:" ),
                              options, code, message );
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
            return capture_path_match( pattern, options, code, message );
        }
        else
        {
            return capture_one( pattern, options, code, message );
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
        return capture_soname_match( pattern, options, code, message );
    }

    // Default: interpret as if soname:
    return capture_one( pattern, options, code, message );
}

static bool
capture_patterns( const char * const *patterns,
                  const capture_options *options,
                  int *code, char **message )
{
    unsigned int i;

    for( i = 0; patterns[i] != NULL; i++ )
    {
        if( !capture_pattern( patterns[i], options, code, message ) )
            return false;
    }

    return true;
}

int
main (int argc, char **argv)
{
    capture_options options = {
        .comparators = NULL,
        .flags = (CAPTURE_FLAG_LIBRARY_ITSELF |
                  CAPTURE_FLAG_DEPENDENCIES ),
        .knowledge = LIBRARY_KNOWLEDGE_INIT,
    };
    const char *option_compare_by = "name,provider";
    const char *option_library_knowledge = NULL;
    int code = 0;
    char *message = NULL;
    // Arbitrary initialization size
    ptr_list *remap_list = ptr_list_alloc( 4 );

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

            case OPTION_COMPARE_BY:
                option_compare_by = optarg;
                break;

            case OPTION_CONTAINER:
                option_container = optarg;
                break;

            case OPTION_DEST:
                option_dest = optarg;
                break;

            case OPTION_LIBRARY_KNOWLEDGE:
                if( option_library_knowledge != NULL )
                    errx( 1, "--library-knowledge can only be used once" );

                option_library_knowledge = optarg;
                break;

            case OPTION_LINK_TARGET:
                option_link_target = optarg;
                break;

            case OPTION_PRINT_LD_SO:
                puts( LD_SO );
                goto out;

            case OPTION_PROVIDER:
                option_provider = optarg;
                break;

            case OPTION_NO_GLIBC:
                option_glibc = false;
                break;

            case OPTION_VERSION:
                _capsule_tools_print_version( "capsule-capture-libs" );
                goto out;

            case OPTION_REMAP_LINK_PREFIX:
                if( strchr( optarg, '=' ) == NULL )
                    errx( 1, "--remap-link-prefix value must follow the FROM=TO pattern" );

                ptr_list_push_ptr( remap_list, xstrdup( optarg ) );
                break;

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
                    goto out;
                }
                break;
        }
    }

    if( optind >= argc )
    {
        warnx( "One or more patterns must be provided" );
        usage( 2 );
    }

    if (remap_list->next > 0)
    {
        size_t n = 0;
        char **remap_array = (char **) ptr_list_free_to_array( _capsule_steal_pointer( &remap_list ), &n );
        remap_prefix = xcalloc( n + 1, sizeof( remap_tuple ) );
        for( size_t i = 0; remap_array[i] != NULL; i++ )
        {
            char *buf = xstrdup( remap_array[i] );
            char *equals = strchr( buf, '=' );
            if( equals == NULL || equals == buf || *(equals + 1) == '\0' )
            {
                free( buf );
                free_strv_full( remap_array );
                errx( 1, "--remap-link-prefix value must follow the FROM=TO pattern" );
            }
            *equals = '\0';
            remap_prefix[i].from = buf;
            remap_prefix[i].to = equals + 1;
        }
        free_strv_full( remap_array );
    }

    arg_patterns = (const char * const *) argv + optind;
    assert( arg_patterns[argc - optind] == NULL );

    if( strcmp( option_dest, "." ) != 0 &&
        mkdir( option_dest, 0755 ) < 0 &&
        errno != EEXIST )
    {
        err( 1, "creating \"%s\"", option_dest );
    }

    options.comparators = library_cmp_list_from_string( option_compare_by, ",",
                                                        &code, &message );

    if( options.comparators == NULL )
    {
        errx( 1, "code %d: %s", code, message );
    }

    if( option_library_knowledge != NULL )
    {
        FILE *fh = fopen( option_library_knowledge, "re" );

        if( fh == NULL)
        {
            err( 1, "opening \"%s\"", option_library_knowledge );
        }

        if( !library_knowledge_load_from_stream( &options.knowledge,
                                                 fh, option_library_knowledge,
                                                 &code, &message ) )
        {
            errx( 1, "code %d: %s", code, message );
        }

        fclose( fh );
    }

    dest_fd = open( option_dest, O_RDWR|O_DIRECTORY|O_CLOEXEC|O_PATH );

    if( dest_fd < 0 )
    {
        err( 1, "opening \"%s\"", option_dest );
    }

    if( !capture_patterns( arg_patterns, &options, &code, &message ) )
    {
        errx( 1, "code %d: %s", code, message );
    }

    close( dest_fd );
    free( options.comparators );
    library_knowledge_clear( &options.knowledge );

    for( size_t i = 0; remap_prefix != NULL && remap_prefix[i].from != NULL; i++ )
    {
        free( remap_prefix[i].from );
        remap_prefix[i].from = NULL;
        remap_prefix[i].to = NULL;
    }

out:
    if( remap_list != NULL )
    {
        char **tmp_to_free = (char **) ptr_list_free_to_array( _capsule_steal_pointer( &remap_list ), NULL );
        free_strv_full( tmp_to_free );
    }

    return 0;
}
