#include <capsule/capsule.h>
#include "utils/utils.h"
#include <stdlib.h>
#include <dlfcn.h>

typedef void * (*dlsymfunc) (void *handle, const char *symbol);
static dlsymfunc symbol_lookup = NULL;

static int
dso_is_exported (const char *dsopath, const char **exported)
{
    for( const char **ex = exported; ex && *ex; ex++ )
        if( soname_matches_path( *ex, dsopath ) )
            return 1;

    return 0;
}

void *
capsule_shim_dlsym (void *capsule,
                    void *handle,
                    const char *symbol,
                    const char **exported)
{
    void *addr = NULL;

    if( (addr = symbol_lookup( capsule, symbol )) )
    {
        Dl_info dso = { 0 };

        // only keep addr from the capsule if it's from an exported DSO:
        // or if we are unable to determine where it came from (what?)
        if( dladdr( addr, &dso ) )
            if( !dso_is_exported( dso.dli_fname, exported ) )
                addr = NULL;
    }

    if( addr == NULL )
        addr = symbol_lookup( handle, symbol );

    return addr;
}


void
capsule_init (void)
{
    symbol_lookup = dlsym( RTLD_DEFAULT, "dlsym" );
    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );
}

