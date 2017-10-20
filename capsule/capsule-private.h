#pragma once

#include <link.h>
#include "utils/utils.h"

typedef void * (*dlsymfunc) (void *handle, const char *symbol);
typedef void * (*dlopnfunc) (const char *file, int flags);

struct _capsule
{
    Lmid_t namespace;
    void  *dl_handle;
    const char *prefix;
    const char **exclude;  // list of DSOs _not_ to install in the capsule
    const char **exported; // list of DSOs from which to export
    capsule_item *relocations;
    struct { ptr_list *all; ptr_list *some; } seen;
};

extern ptr_list *capsule_manifest;
extern dlsymfunc capsule_dl_symbol;
extern dlopnfunc capsule_dl_open;
