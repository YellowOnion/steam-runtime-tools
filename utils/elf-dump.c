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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "dump.h"

int main (int argc, char **argv)
{
    void *handle;
    const char *libname;

    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );

    if( argc < 2 )
    {
        fprintf( stderr, "usage: %s <ELF-DSO>\n", argv[0] );
        exit( 1 );
    }

    handle = dlopen( argv[1], RTLD_LAZY );

    if( !handle )
    {
        int e = errno;
        const char *err = dlerror();
        fprintf( stderr, "%s: dlopen failed (%s)\n", argv[0], err );
        return e ? e : ENOENT;
    }

    if( (libname = strrchr( argv[1], '/' )) )
        libname = libname + 1;
    else
        libname = argv[1];

    dump_elf_data( libname );

    return 0;
}
