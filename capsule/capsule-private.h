#pragma once

#include <link.h>
#include "utils/utils.h"

typedef void * (*dlsymfunc) (void *handle, const char *symbol);
typedef void * (*dlopnfunc) (const char *file, int flags);

typedef struct _capsule_namespace
{
    Lmid_t ns;
    const char *prefix;
    ptr_list *exclusions;
    ptr_list *exports;
    ptr_list *nowrap;
    char  **combined_exclude;
    char  **combined_export;
    char  **combined_nowrap;
} capsule_namespace;

struct _capsule
{
    void  *dl_handle;
    capsule_item *relocations;
    struct { ptr_list *all; ptr_list *some; } seen;
    capsule_metadata *meta;
    capsule_namespace *ns;
};

extern ptr_list *_capsule_list;
extern dlsymfunc _capsule_original_dlsym;
extern dlopnfunc _capsule_original_dlopen;
