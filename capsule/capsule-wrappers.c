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

// TODO: Implement dlvsym()?

/**
 * capsule_external_dlsym:
 * @handle: A handle returned by `dlopen`, or %RTLD_DEFAULT or %RTLD_NEXT
 * @symbol: A symbol to be looked up
 *
 * An implementation of `dlsym`, used when it is called by the executable
 * or by a library outside the capsule.
 *
 * If @symbol is exported by a library that is part of the exported ABI
 * of a capsule, return that implementation.
 *
 * If not, return the implementation from the `LM_ID_BASE` namespace if
 * there is one, or %NULL.
 *
 * Returns: The address associated with @symbol, or %NULL
 */
void *
capsule_external_dlsym (void *handle, const char *symbol)
{
    DEBUG( DEBUG_DLFUNC|DEBUG_WRAPPERS, "dlsym(%s)", symbol );
    void *addr = NULL;

    for( size_t n = 0; n < _capsule_list->next; n++ )
    {
        capsule cap = ptr_list_nth_ptr( _capsule_list, n );
        // TODO: If handle != cap->dl_handle, should we skip it?
        // TODO: RTLD_NEXT isn't implemented (is it implementable?)
        addr = capsule_dl_symbol ( cap->dl_handle, symbol );

        if( addr )
        {
            Dl_info dso = { 0 };

            // only keep addr from the capsule if it's from an exported DSO:
            // or if we are unable to determine where it came from (what?)
            if( dladdr( addr, &dso ) )
            {
                if( !dso_is_exported( dso.dli_fname, cap->meta->combined_export ) )
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

/**
 * capsule_external_dlopen:
 * @file: A SONAME or filename to be opened
 * @flag: Flags affecting how we open the library
 *
 * An implementation of `dlopen`, used when it is called by the executable
 * or by a library outside the capsule.
 *
 * Load @file with the ordinary `dlopen`. If successful, adjust
 * relocations before returning the resulting handle.
 *
 * Returns: The handle returned by `dlopen`
 */
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
        for( size_t n = 0; n < _capsule_list->next; n++ )
        {
            const capsule c = ptr_list_nth_ptr( _capsule_list, n );

            if( capsule_relocate( c, NULL, &error ) != 0 )
            {
                fprintf( stderr,
                         "relocation from %s after dlopen(%s, …) failed: %s\n",
                         c->meta->soname, file, error );
                free( error );
            }

            if( capsule_relocate_except( c,
                                         c->meta->dl_wrappers,
                                         (const char **)c->meta->combined_nowrap,
                                         &error ) != 0 )
            {
                fprintf( stderr,
                         "dl-wrapper relocation from %s after "
                         "dlopen(%s, …) failed: %s\n",
                         c->meta->soname, file, error );
                free( error );
            }
        }

        debug_flags = df;
    }

    return handle;
}

/**
 * capsule_shim_dlopen:
 * @cap: The capsule from which `dlopen` was called
 * @file: SONAME or filename to be opened
 * @flag: Flags affecting how @file is opened
 *
 * An implementation of `dlopen` suitable to be called from inside a
 * namespace. Load @file into @cap's namespace.
 *
 * If @cap has a non-trivial prefix, load @file and its recursive
 * dependencies from @cap's prefix instead of from the root filesystem.
 *
 * Returns: A handle as if for `dlopen`
 */
void *
capsule_shim_dlopen(const capsule cap, const char *file, int flag)
{
    void *res = NULL;
    int code = 0;
    char *errors = NULL;
    ld_libs ldlibs = {};

    DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
           "dlopen(%s, %x) wrapper: LMID: %ld; prefix: %s;",
           file, flag, cap->ns->ns, cap->ns->prefix );

    if( cap->ns->prefix && strcmp(cap->ns->prefix, "/") )
    {
        if( !ld_libs_init( &ldlibs, (const char **)cap->meta->combined_exclude,
                           cap->ns->prefix, debug_flags, &code, &errors ) )
        {
            DEBUG( DEBUG_LDCACHE|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "Initialising ld_libs data failed: error %d: %s",
                   code, errors);
              goto cleanup;
        }

        if( !ld_libs_load_cache( &ldlibs, "/etc/ld.so.cache", &code, &errors ) )
        {
            DEBUG( DEBUG_LDCACHE|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "Loading ld.so.cache from %s: error %d: %s", cap->ns->prefix,
                   code, errors );
            goto cleanup;
        }

        // find the initial DSO (ie what the caller actually asked for):
        if( !ld_libs_set_target( &ldlibs, file, &code, &errors ) )
        {
            DEBUG( DEBUG_SEARCH|DEBUG_WRAPPERS|DEBUG_DLFUNC,
                           "Not found: %s under %s: error %d: %s",
                           file, cap->ns->prefix, code, errors );
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
        res = ld_libs_load( &ldlibs, &cap->ns->ns, flag, &code, &errors );

        if( !res )
            DEBUG( DEBUG_WRAPPERS|DEBUG_DLFUNC,
                   "capsule dlopen error %d: %s", code, errors );

        goto cleanup;
    }
    else // no prefix: straightforward dlmopen into our capsule namespace:
    {
        res = dlmopen( cap->ns->ns, file, flag );

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
