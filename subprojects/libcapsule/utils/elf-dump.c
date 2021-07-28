// Copyright © 2017-2019 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

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
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "dump.h"
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

  fprintf( fh, "Usage: %s SONAME\n",
           program_invocation_short_name );
  fprintf( fh, "SONAME is the machine-readable name of a shared library,\n"
               "for example 'libz.so.1'.\n" );
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
    void *handle;
    const char *libname;

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
                _capsule_tools_print_version( "capsule-elf-dump" );
                return 0;
        }
    }

    if( argc != optind + 1 )
        usage( 1 );

    handle = dlopen( argv[optind], RTLD_LAZY );

    if( !handle )
    {
        int e = errno;
        const char *err = dlerror();
        fprintf( stderr, "%s: dlopen failed (%s)\n", program_invocation_short_name, err );
        return e ? e : ENOENT;
    }

    if( (libname = strrchr( argv[optind], '/' )) )
        libname = libname + 1;
    else
        libname = argv[optind];

    dump_elf_data( libname );

    return 0;
}
