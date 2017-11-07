#include "capsule/capsule.h"
#include "capsule/capsule-private.h"
#include "utils/utils.h"
#include "utils/ld-libs.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

capsule_item capsule_external_dl_relocs[] =
{
  { "dlopen",
    (capsule_addr) capsule_external_dlopen ,
    (capsule_addr) capsule_external_dlopen },
  { NULL }
};

static int
dso_is_exported (const char *dsopath, char **exported)
{
    for( char **ex = exported; ex && *ex; ex++ )
        if( soname_matches_path( *ex, dsopath ) )
            return 1;

    return 0;
}

void *
capsule_external_dlsym (void *handle, const char *symbol)
{
    DEBUG( DEBUG_DLFUNC|DEBUG_WRAPPERS, "dlsym(%s)", symbol );
    void *addr = NULL;

    for( size_t n = 0; n < capsule_manifest->next; n++ )
    {
        capsule_metadata *m = ptr_list_nth_ptr( capsule_manifest, n );
        addr = capsule_dl_symbol ( m->handle->dl_handle, symbol );

        if( addr )
        {
            Dl_info dso = { 0 };

            // only keep addr from the capsule if it's from an exported DSO:
            // or if we are unable to determine where it came from (what?)
            if( dladdr( addr, &dso ) )
            {
                if( !dso_is_exported( dso.dli_fname, m->combined_export ) )
                    addr = NULL;

                DEBUG( DEBUG_DLFUNC|DEBUG_WRAPPERS,
                       "symbol %s is from soname %s - %s",
                       symbol, dso.dli_fname, addr ? "OK" : "Ignored" );

                if( addr )
                    break;
            }
        }
    }

    if( addr == NULL )
    {
        DEBUG( DEBUG_DLFUNC|DEBUG_WRAPPERS,
               "symbol %s not found: fall back to default", symbol );
        addr = capsule_dl_symbol ( handle, symbol );
    }

    return addr;
}

void *
capsule_external_dlopen(const char *file, int flag)
{
    void *handle = NULL;
    char *error  = NULL;

    if( capsule_dl_open )
    {
        handle = capsule_dl_open ( file, flag );
    }
    else
    {
        fprintf( stderr,
                 "capsule_external_dlopen() has no dlopen() implementation\n" );
        abort();
    }

    if( handle != NULL )
    {
        unsigned long df = debug_flags;

        if( debug_flags & DEBUG_DLFUNC )
            debug_flags |= DEBUG_RELOCS;
        // This may not even be necessary, so it should not be fatal.
        // We do want to log it though as it might be an important clue:
        for( size_t n = 0; n < capsule_manifest->next; n++ )
        {
            capsule_metadata *m = ptr_list_nth_ptr( capsule_manifest, n );
            const capsule c = m->handle;

            if( capsule_relocate( c, NULL, &error ) != 0 )
            {
                fprintf( stderr,
                         "relocation from %s after dlopen(%s, …) failed: %s\n",
                         m->soname, file, error );
                free( error );
            }

            if( capsule_relocate_except( c,
                                         m->dl_wrappers,
                                         (const char **)m->combined_nowrap,
                                         &error ) != 0 )
            {
                fprintf( stderr,
                         "dl-wrapper relocation from %s after "
                         "dlopen(%s, …) failed: %s\n",
                         m->soname, file, error );
                free( error );
            }
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
    ld_libs ldlibs = {};

    DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
           "dlopen(%s, %x) wrapper: LMID: %ld; prefix: %s;",
           file, flag, cap->meta->namespace, cap->meta->active_prefix );

    if( cap->prefix && strcmp(cap->prefix, "/") )
    {
        if( !ld_libs_init( &ldlibs, (const char **)cap->meta->combined_exclude,
                           cap->prefix, debug_flags, &code, &errors ) )
        {
            DEBUG( DEBUG_LDCACHE|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "Initialising ld_libs data failed: error %d: %s",
                   code, errors);
              goto cleanup;
        }

        if( !ld_libs_load_cache( &ldlibs, "/etc/ld.so.cache", &code, &errors ) )
        {
            DEBUG( DEBUG_LDCACHE|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "Loading ld.so.cache from %s: error %d: %s", cap->prefix,
                   code, errors );
            goto cleanup;
        }

        // find the initial DSO (ie what the caller actually asked for):
        if( !ld_libs_set_target( &ldlibs, file, &code, &errors ) )
        {
            DEBUG( DEBUG_SEARCH|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                           "Not found: %s under %s: error %d: %s",
                           file, cap->prefix, code, errors );
            goto cleanup;
        }

        // harvest all the requested DSO's dependencies:
        if( !ld_libs_find_dependencies( &ldlibs, &code, &errors ) )
        {
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error %d: %s", code, errors );
            goto cleanup;
        }

        // load them up in reverse dependency order:
        res = ld_libs_load( &ldlibs, &cap->meta->namespace, flag, &code, &errors );

        if( !res )
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error %d: %s", code, errors );

        goto cleanup;
    }
    else // no prefix: straightforward dlmopen into our capsule namespace:
    {
        res = dlmopen( cap->meta->namespace, file, flag );

        if( !res )
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error %s: %s", file, dlerror() );
    }

    return res;

cleanup:
    ld_libs_finish( &ldlibs );
    free( errors );
    return res;
}
