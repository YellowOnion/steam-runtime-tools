#include <capsule/capsule.h>
#include "capsule/capsule-private.h"
#include "utils/utils.h"
#include <stdlib.h>
#include <dlfcn.h>

capsule
capsule_init (Lmid_t namespace,
              const char *prefix,
              const char **exclude,
              const char **exported)
{
    capsule handle = calloc( 1, sizeof(struct _capsule) );

    set_debug_flags( secure_getenv("CAPSULE_DEBUG") );

    handle->namespace  = namespace;
    handle->prefix     = prefix;
    handle->exclude    = exclude;
    handle->exported   = exported;
    handle->get_symbol = dlsym( RTLD_DEFAULT, "dlsym"  );
    handle->load_dso   = dlsym( RTLD_DEFAULT, "dlopen" );

    // in principle we should be able to make both reloc calls
    // efficient in the same do-not-redo-your-work way, but for
    // some reason the unrestricted relocation breaks if we turn
    // this one on. setting the tracker to NULL disables for now.
    handle->seen.all  = NULL;
    handle->seen.some = ptr_list_alloc( 32 );

    return handle;
}

