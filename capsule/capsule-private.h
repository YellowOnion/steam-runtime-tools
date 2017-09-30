#pragma once

#include <link.h>

typedef void * (*dlsymfunc) (void *handle, const char *symbol);
typedef void * (*dlopnfunc) (const char *file, int flags);

struct _capsule
{
    Lmid_t namespace;
    void  *dl_handle;
    const char *prefix;
    const char **exclude;
    const char **exported;
    capsule_item *relocations;
    dlsymfunc get_symbol;
    dlopnfunc load_dso;
};
