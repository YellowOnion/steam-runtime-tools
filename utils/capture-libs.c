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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "ld-libs.h"
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
#elif defined(__aarch64__)
# define LD_SO "/lib/ld-linux-aarch64.so.1"
#elif defined(__arm__) && defined(__ARM_EABI__) && defined(_ARM_PCS_VFP)
# define LD_SO "/lib/ld-linux-armhf.so.3"
#elif defined(__arm__) && defined(__ARM_EABI__)
# define LD_SO "/lib/ld-linux.so.3"
#elif defined(__hppa__) || defined(__m68k__) || defined(__powerpc__) || \
      defined(__s390__)
# define LD_SO "/lib64/ld.so.1"
#elif defined(__powerpc64__) && __BYTE_ORDER == __LITTLE_ENDIAN
# define LD_SO "/lib/ld64.so.2"
#elif defined(__s390x__) || defined(__powerpc64__)
# define LD_SO "/lib/ld64.so.1"
#else
# error Unsupported architecture: we do not know where ld.so is
#endif

enum
{
  OPTION_CONTAINER,
  OPTION_DEST,
  OPTION_LINK_TARGET,
  OPTION_PRINT_LD_SO,
  OPTION_PROVIDER,
  OPTION_RESOLVE_LD_SO,
};

static const char * const *arg_patterns = NULL;
static const char *option_container = "/";
static const char *option_dest = ".";
static const char *option_provider = "/";
static const char *option_link_target = NULL;

static struct option long_options[] =
{
    { "container", required_argument, NULL, OPTION_CONTAINER },
    { "dest", required_argument, NULL, OPTION_DEST },
    { "help", no_argument, NULL, 'h' },
    { "link-target", required_argument, NULL, OPTION_LINK_TARGET },
    { "print-ld.so", no_argument, NULL, OPTION_PRINT_LD_SO },
    { "provider", required_argument, NULL, OPTION_PROVIDER },
    { "resolve-ld.so", required_argument, NULL, OPTION_RESOLVE_LD_SO },
    { NULL }
};

static int dest_fd = -1;

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
  fprintf( fh, "\n" );
  fprintf( fh, "Each PATTERN is a SONAME, or a shell-style glob matching\n"
               "SONAMEs (which will usually need to be quoted when using\n"
               "a shell), or one of the following special strings:\n" );
  fprintf( fh, "\n" );
  fprintf( fh, "soname:SONAME\n"
               "\tCapture the library in ld.so.cache whose name is\n"
               "\texactly SONAME\n" );
  fprintf( fh, "soname-match:GLOB\n"
               "\tCapture every library in ld.so.cache that matches\n"
               "\ta shell-style glob (which will usually need to be\n"
               "\tquoted when using a shell)\n" );
  fprintf( fh, "if-exists:PATTERN\n"
               "\tCapture PATTERN, but don't fail if nothing matches\n" );
  fprintf( fh, "even-if-older:PATTERN\n"
               "\tCapture PATTERN, even if the version in CONTAINER\n"
               "\tappears newer\n" );
  fprintf( fh, "gl:\n"
               "\tShortcut for even-if-older:if-exists:soname:libGL.so.1,\n"
               "\teven-if-older:if-exists:soname-match:libGLX_*.so.0, and\n"
               "\tvarious other GL-related libraries\n" );
  exit( code );
}

typedef enum
{
  CAPTURE_FLAG_NONE = 0,
  CAPTURE_FLAG_EVEN_IF_OLDER = (1 << 0),
  CAPTURE_FLAG_IF_EXISTS = (1 << 1),
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

static bool
capture_one( const char *soname, capture_flags flags,
             int *code, char **message )
{
    unsigned int i;
    _capsule_cleanup(ld_libs_finish) ld_libs provider = {};
    int local_code = 0;
    _capsule_autofree char *local_message = NULL;

    if( !init_with_target( &provider, option_provider, soname,
                           &local_code, &local_message ) )
    {
        if( ( flags & CAPTURE_FLAG_IF_EXISTS ) && local_code == ENOENT )
        {
            DEBUG( DEBUG_TOOL, "%s not found, ignoring", soname );
            _capsule_clear( &local_message );
            return true;
        }
        else
        {
            _capsule_propagate_error( code, message, local_code,
                                      _capsule_steal_pointer( &local_message ) );
            return false;
        }
    }

    if( !ld_libs_find_dependencies( &provider, code, message ) )
    {
        return false;
    }

    for( i = 0; i < N_ELEMENTS( provider.needed ); i++ )
    {
        _capsule_autofree char *target = NULL;
        struct stat statbuf;
        const char *its_basename;

        if( !provider.needed[i].name )
        {
            continue;
        }

        its_basename = my_basename( provider.needed[i].name );

        if( fstatat( dest_fd, its_basename, &statbuf,
                     AT_SYMLINK_NOFOLLOW ) == 0 )
        {
            /* We already created a symlink for this library. No further
             * action required (but keep going through its dependencies
             * in case we need to symlink those into place) */
            DEBUG( DEBUG_TOOL, "We already have a symlink for %s",
                   provider.needed[i].name );
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
                   provider.needed[i].path );
        }
        else if( i == 0 && ( flags & CAPTURE_FLAG_EVEN_IF_OLDER ) )
        {
            DEBUG( DEBUG_TOOL,
                   "Explicitly requested %s from %s even if older: \"%s\"",
                   provider.needed[i].name, option_provider,
                   provider.needed[i].path );
        }
        else
        {
            _capsule_cleanup(ld_libs_finish) ld_libs container = {};
            if( init_with_target( &container, option_container,
                                  provider.needed[i].name,
                                  &local_code, &local_message ) )
            {
                _capsule_autofree char *p_realpath = NULL;
                _capsule_autofree char *c_realpath = NULL;
                const char *p_basename;
                const char *c_basename;

                // This might look redundant, but resolve_symlink_prefixed()
                // doesn't chase symlinks if the prefix is '/' or empty.
                p_realpath = realpath( provider.needed[i].path, NULL );
                c_realpath = realpath( container.needed[0].path, NULL );
                p_basename = my_basename( p_realpath );
                c_basename = my_basename( c_realpath );

                DEBUG( DEBUG_TOOL,
                       "Comparing %s \"%s\" from \"%s\" with "
                       "\"%s\" from \"%s\"",
                       provider.needed[i].name, p_basename, option_provider,
                       c_basename, option_container );

                /* If equal, we prefer the provider over the container */
                if( strverscmp( c_basename, p_basename ) > 0 )
                {
                    /* Version in container is strictly newer: don't
                     * symlink in the one from the provider */
                    DEBUG( DEBUG_TOOL,
                           "%s is strictly newer in the container",
                           provider.needed[i].name );
                    continue;
                }
                else
                {
                    DEBUG( DEBUG_TOOL,
                           "%s is newer or equal in the provider",
                           provider.needed[i].name );
                }
            }
            else if( local_code == ENOENT )
            {
                /* else assume it's absent from the container, which is
                 * just like it being newer in the provider */
                DEBUG( DEBUG_TOOL, "%s is not in the container",
                       provider.needed[i].name );
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
            target = xstrdup( provider.needed[i].path );
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

            safe_strncpy( path, provider.needed[i].path, sizeof(path) );

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
               option_dest, provider.needed[i].name, target );

        if( symlinkat( target, dest_fd, provider.needed[i].name ) < 0 )
        {
            warn( "warning: cannot create symlink %s/%s",
                  option_dest, provider.needed[i].name );
        }

        if( strcmp( provider.needed[i].name, "libc.so.6" ) == 0 )
        {
            /* Having captured libc, we need to capture the rest of
             * the related libraries from the same place */
            static const char * const libc_patterns[] = {
                "soname:libBrokenLocale.so.1",
                "soname:libanl.so.1",
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

            DEBUG( DEBUG_TOOL,
                   "Capturing the rest of glibc to go with %s",
                   provider.needed[i].name );

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
                            "no matches found for glob pattern \"%s\"",
                            pattern );
        goto out;
    }

    ret = true;

out:
    if( ctx.cache.is_open )
        ld_cache_close( &ctx.cache );

    return ret;
}

#define strstarts(str, start) \
  (strncmp( str, start, strlen( start ) ) == 0)

static bool
capture_pattern( const char *pattern, capture_flags flags,
                 int *code, char **message )
{
    DEBUG( DEBUG_TOOL, "%s", pattern );

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

    if( strstarts( pattern, "if-exists:" ) )
    {
        return capture_pattern( pattern + strlen( "if-exists:" ),
                                flags | CAPTURE_FLAG_IF_EXISTS,
                                code, message );
    }

    if( strstarts( pattern, "even-if-older:" ) )
    {
        return capture_pattern( pattern + strlen( "even-if-older:" ),
                                flags | CAPTURE_FLAG_EVEN_IF_OLDER,
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
    capture_flags flags = CAPTURE_FLAG_NONE;
    int code = 0;
    char *message = NULL;

    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );

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

            case OPTION_RESOLVE_LD_SO:
                {
                    char path[PATH_MAX] = { 0 };
                    size_t prefix_len = strlen( optarg );

                    if( build_filename( path, sizeof(path), optarg,
                                        LD_SO, NULL ) >= sizeof(path) )
                    {
                        errx( 1, "\"%s\" too long", optarg );
                    }

                    DEBUG( DEBUG_TOOL, "Starting with %s", path );

                    while( resolve_link( optarg, path ) )
                    {
                        DEBUG( DEBUG_TOOL, "-> %s", path );
                    }

                    if( strcmp( optarg, "/" ) == 0 )
                    {
                        prefix_len = 0;
                    }

                    if( ( prefix_len > 0 &&
                          strncmp( path, optarg, prefix_len ) != 0 ) ||
                        path[prefix_len] != '/' )
                    {
                        errx( 1, "\"%s\" is not within prefix \"%s\"",
                              path, optarg );
                    }

                    puts( path + prefix_len );
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
