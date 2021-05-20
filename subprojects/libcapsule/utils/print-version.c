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

#include <dlfcn.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "debug.h"
#include "ld-cache.h"
#include "ld-libs.h"
#include "tools.h"

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
  }

  fprintf( fh, "Usage: %s SONAME [SYSROOT]\n",
           program_invocation_short_name );
  fprintf( fh, "SONAME is the machine-readable name of a shared library,\n"
               "for example 'libz.so.1'.\n" );
  fprintf( fh, "SYSROOT is the root directory where we look for SONAME.\n" );
  exit( code );
}

enum
{
  OPTION_HELP,
  OPTION_VERSION,
};

static struct option long_options[] =
{
    { "help", no_argument, NULL, OPTION_HELP },
    { "version", no_argument, NULL, OPTION_VERSION },
    { NULL }
};

int main (int argc, char **argv)
{
    const char *libname;
    const char *prefix = NULL;
    char *message = NULL;
    ld_libs ldlibs = {};
    int error = 0;
    int e = 0;

    set_debug_flags( getenv("CAPSULE_DEBUG") );

    while( 1 )
    {
        int opt = getopt_long( argc, argv, "", long_options, NULL );

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

            case OPTION_HELP:
                usage( 0 );
                break;  // not reached

            case OPTION_VERSION:
                _capsule_tools_print_version( "capsule-version" );
                return 0;
        }
    }

    if( argc < optind + 1 || argc > optind + 2)
        usage( 1 );

    if( argc > optind + 1 )
        prefix = argv[optind + 1];

    if( !ld_libs_init( &ldlibs, NULL, prefix, 0, &error, &message ) )
    {
        fprintf( stderr, "%s: failed to initialize for prefix %s (%d: %s)\n",
                 program_invocation_short_name, prefix, error, message );
        exit( error ? error : ENOENT );
    }

    if( ld_libs_set_target( &ldlibs, argv[optind], &error, &message ) )
    {
        const char *path;
        const char *buf;

        if( (libname = strrchr( argv[optind], '/' )) )
            libname = libname + 1;
        else
            libname = argv[optind];

        path = &ldlibs.needed[0].path[0];

        while( (buf = strstr( path + 1, libname )) )
            path = buf;

        if( path )
            path = strstr( path, ".so." );

        if( path )
            path += 4;

        if( !path || !*path )
            if( (path = strstr( libname, ".so." )) )
                path += 4;

        fprintf( stdout, "%s %s %s %s\n",
                 prefix, libname,
                 (path && *path) ?  path : "1", // wild guess if we failed
                 &ldlibs.needed[0].path[0] );
    }
    else
    {
        fprintf( stderr, "%s: failed to open [%s]%s (%d: %s)\n",
                 program_invocation_short_name, prefix, argv[optind], error, message );
        exit( error ? error : ENOENT );
    }

    ld_libs_finish( &ldlibs );
    exit(e);
}
