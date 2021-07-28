// Copyright © 2017 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

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
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "notgl.h"
#include "notgles.h"
#include "notgl-helper.h"

static void die( const char *format, ... )
    __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));

static void
die( const char *format, ... )
{
    va_list ap;

    va_start( ap, format );
    vfprintf( stderr, format, ap );
    va_end( ap );
    abort();
}

static void *
xdlopen( const char *filename, int flags )
{
    void *handle = dlopen( filename, flags );

    if( handle == NULL )
        die( "dlopen(\"%s\", %d): %s", filename, flags, dlerror() );

    return handle;
}

static void *
xdlsym( void *handle, const char *symbol )
{
    void *value = dlsym( handle, symbol );

    if( value == NULL )
        die( "dlsym(%p, \"%s\"): %s", handle, symbol, dlerror() );

    return value;
}

static void
xdlclose( void *handle )
{
    if( dlclose( handle ) != 0 )
        die( "dlclose(%p): %s", handle, dlerror() );
}

static notgl_extension_function
get_extension( void *handle,
               const char *name )
{
    return dlsym( handle, name );
}

int
main ( int argc,
       char **argv )
{
    void *gl;
    void *gles;
    notgl_extension_function f;

    // Select line-buffering for output, in case we crash
    setvbuf( stdout, NULL, _IOLBF, 0 );

    gl = xdlopen( "libnotgl.so.0", RTLD_LAZY|RTLD_GLOBAL );
    gles = xdlopen( "libnotgles.so.1", RTLD_NOW|RTLD_LOCAL );

    f = xdlsym( gl, "notgl_get_implementation" );
    printf( "NotGL implementation: %s\n", f() );
    f = xdlsym( gl, "notgl_use_helper" );
    printf( "NotGL helper implementation: %s\n", f() );

    f = get_extension( RTLD_DEFAULT, "notgl_extension_both" );

    if( f )
        printf( "notgl_extension_both: %s\n", f() );
    else
        printf( "notgl_extension_both: (not found)\n" );

    f = get_extension( RTLD_DEFAULT, "notgl_extension_red" );

    if( f )
        printf( "notgl_extension_red: %s\n", f() );
    else
        printf( "notgl_extension_red: (not found)\n" );

    f = get_extension( RTLD_DEFAULT, "notgl_extension_green" );

    if( f )
        printf( "notgl_extension_green: %s\n", f() );
    else
        printf( "notgl_extension_green: (not found)\n" );

    f = xdlsym( gles, "notgles_get_implementation" );
    printf( "NotGLES implementation: %s\n", f() );
    f = xdlsym( gles, "notgles_use_helper" );
    printf( "NotGLES helper implementation: %s\n", f() );

    f = get_extension( gles, "notgles_extension_both" );

    if( f )
        printf( "notgles_extension_both: %s\n", f() );
    else
        printf( "notgles_extension_both: (not found)\n" );

    f = get_extension( gles, "notgles_extension_red" );

    if( f )
        printf( "notgles_extension_red: %s\n", f() );
    else
        printf( "notgles_extension_red: (not found)\n" );

    f = get_extension( gles, "notgles_extension_green" );

    if( f )
        printf( "notgles_extension_green: %s\n", f() );
    else
        printf( "notgles_extension_green: (not found)\n" );

    xdlclose( gl );
    xdlclose( gles );

    // Check that we can dlopen and dlclose repeatedly without crashing

    for( int i = 1; i < 10; i++ )
    {
        printf( "dlopening and dlclosing %d times...\n", i );
        // dlopen'd handles are refcounted; take i references
        for( int j = 0; j < i; j++ )
        {
            // Arbitrary flags that happen to be oppositely paired
            // compared with how we opened the libraries above
            gl = xdlopen( "libnotgl.so.0", RTLD_LAZY|RTLD_LOCAL );
            gles = xdlopen( "libnotgles.so.1", RTLD_NOW|RTLD_GLOBAL );
            xdlsym( gl, "notgl_extension_both" );
            xdlsym( gles, "notgles_extension_both" );
            xdlsym( RTLD_DEFAULT, "notgles_extension_both" );
        }

        // Release all i references
        for( int j = 0; j < i; j++ )
        {
            xdlclose( gl );
            xdlclose( gles );
        }
    }

    return 0;
}
