#include "capsule/capsule.h"
#include "capsule/capsule-private.h"
#include "utils/utils.h"
#include "utils/ld-libs.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int
dso_is_exported (const char *dsopath, const char **exported)
{
    for( const char **ex = exported; ex && *ex; ex++ )
        if( soname_matches_path( *ex, dsopath ) )
            return 1;

    return 0;
}

void *
capsule_external_dlsym (capsule cap, void *handle, const char *symbol)
{
    DEBUG( DEBUG_DLFUNC|DEBUG_WRAPPERS, "dlsym(%s)", symbol );
    void *addr = cap->get_symbol( cap->dl_handle, symbol );

    if( addr )
    {
        Dl_info dso = { 0 };

        // only keep addr from the capsule if it's from an exported DSO:
        // or if we are unable to determine where it came from (what?)
        if( dladdr( addr, &dso ) )
            if( !dso_is_exported( dso.dli_fname, cap->exported ) )
                addr = NULL;
    }

    if( addr == NULL )
    {
        DEBUG( DEBUG_DLFUNC|DEBUG_WRAPPERS,
               "symbol %s not found or not exportable, fall back to default",
               symbol );
        addr = cap->get_symbol( handle, symbol );
    }

    return addr;
}

void *
capsule_external_dlopen(const capsule cap, const char *file, int flag)
{
    void *handle = NULL;
    char *error  = NULL;

    if( cap->load_dso )
    {
        fprintf( stderr, "dlopen<%p>( %s, %d )\n", cap->load_dso, file, flag );
        handle = cap->load_dso( file, flag );
    }
    else
    {
        fprintf( stderr,
                 "capsule_external_dlopen() has no dlopen() implementation\n" );
    }

    if( handle != NULL )
    {
        unsigned long df = debug_flags;

        if( debug_flags & DEBUG_DLFUNC )
            debug_flags = DEBUG_RELOCS|DEBUG_SEARCH;
        // This may not even be necessary, so it should not be fatal.
        // We do want to log it though as it might be an important clue:
        if( capsule_relocate( cap, NULL, &error ) != 0 )
        {
            fprintf( stderr, "relocation after dlopen(%s, …) failed: %s\n",
                     file, error );
            free( error );
        }

        debug_flags = df;
    }

    return handle;
}

void *
capsule_shim_dlopen(const capsule cap, const char *file, int flag)
{
    void *res = NULL;
    int code = 0;
    char *errors = NULL;
    ld_libs_t ldlibs = {};

    DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
           "dlopen(%s, %x) wrapper: LMID: %ld; prefix: %s;",
           file, flag, cap->namespace, cap->prefix );

    if( cap->prefix && strcmp(cap->prefix, "/") )
    {
        ld_libs_init( &ldlibs, cap->exclude, cap->prefix, debug_flags, &code );

        if( !ld_libs_load_cache( &ldlibs, "/etc/ld.so.cache" ) )
        {
            int rv = (errno == 0) ? EINVAL : errno;

            DEBUG( DEBUG_LDCACHE|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "Loading ld.so.cache from %s (error: %d)", cap->prefix, rv );
            goto cleanup;
        }

        // find the initial DSO (ie what the caller actually asked for):
        if( !ld_libs_set_target( &ldlibs, file ) )
        {
            int rv = (errno == 0) ? EINVAL : errno;

            DEBUG( DEBUG_SEARCH|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                           "Not found: %s under %s (error: %d)",
                           file, cap->prefix, rv );
            goto cleanup;
        }

        // harvest all the requested DSO's dependencies:
        ld_libs_find_dependencies( &ldlibs );

        if( ldlibs.error )
        {
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error: %s", ldlibs.error );
            goto cleanup;
        }

        // load them up in reverse dependency order:
        res = ld_libs_load( &ldlibs, &cap->namespace, flag, &code );

        if( !res )
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error %d: %s", code, errors );

        goto cleanup;
    }
    else // no prefix: straightforward dlmopen into our capsule namespace:
    {
        res = dlmopen( cap->namespace, file, flag );

        if( !res )
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error %s: %s", file, dlerror() );
    }

    return res;

cleanup:
    ld_libs_finish( &ldlibs );
    return res;
}
