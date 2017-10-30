#pragma once

#include <link.h>
#include "utils/utils.h"

typedef void * (*dlsymfunc) (void *handle, const char *symbol);
typedef void * (*dlopnfunc) (const char *file, int flags);

struct _capsule
{
    void  *dl_handle;
    const char *prefix;
    capsule_item *relocations;
    struct { ptr_list *all; ptr_list *some; } seen;
    capsule_metadata *meta;
};

extern ptr_list *capsule_manifest;
extern dlsymfunc capsule_dl_symbol;
extern dlopnfunc capsule_dl_open;
