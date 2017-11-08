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
    char  **combined_exclude;
    char  **combined_export;
} capsule_namespace;

struct _capsule
{
    void  *dl_handle;
    struct { ptr_list *all; ptr_list *some; } seen;
    capsule_metadata *meta;
    capsule_namespace *ns;
    capsule_item int_dlopen_wrappers[2];
};

extern ptr_list *_capsule_list;
extern dlsymfunc _capsule_original_dlsym;
extern dlopnfunc _capsule_original_dlopen;

/*
 * _capsule_load:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @dso: The name of the DSO to open (cf dlopen()) - eg libGL.so.1
 * @namespace: (out) Address of an #Lmid_t value.
 * @wrappers: Array of #capsule_item used to replace symbols in the namespace
 * @errcode: (out): location in which to store the error code (errno) on failure
 * @error: (out) (transfer full) (optional): location in which to store
 *         an error message on failure, or %NULL to ignore.
 *         Free with free().
 *
 * Returns: A (void *) DSO handle, as per dlopen(), or %NULL on error
 *
 * Opens @dso (a library) from a filesystem mounted at @prefix into a
 * symbol namespace specified by @namespace, using dlmopen().
 *
 * Any symbols specified in @wrappers will be replaced with the
 * corresponding address from @wrappers (allowing you to replace
 * function definitions inside the namespace with your own).
 *
 * This is normally used to replace calls inside the namespace to
 * certain functions (such as dlopen()) which need to be overridden
 * to operate correctly inside a private namespace associated with
 * a nonstandard filsystem tree.
 *
 * The #Lmid_t value stored in @namespace gives the symbol namespace
 * into which the target DSO was loaded. It should normally be #LM_ID_NEWLM.
 *
 * In addition to a bare libFOO.so.X style name, @dso may be an
 * absolute path (or even a relative one) and in those cases should
 * have the same effect as passing those values to dlopen(). This is
 * not a normal use case and has not been heavily tested.
 *
 */
void *_capsule_load (const capsule capsule,
                     capsule_item *wrappers,
                     int *errcode,
                     char **error);

/*
 * _capsule_relocate:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @error: (out) (transfer full) (optional): location in which to store
 *         an error message on failure, or %NULL to ignore.
 *         Free with free().
 *
 * Returns: 0 on success, non-zero on failure.
 *
 * This function updates the GOT entries in all DSOs outside the capsule
 * so that when they call any function listed in the @items of the
 * #capsule_metadata, they invoke the copy of that function inside the capsule.
 *
 * In the unlikely event that an error message is returned in @error it is the
 * caller's responsibility to free() it.
 */
int _capsule_relocate (const capsule capsule,
                       char **error);

/*
 * _capsule_relocate_dlopen:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @error: (out) (transfer full) (optional): location in which to store
 *         an error message on failure, or %NULL to ignore.
 *         Free with free().
 *
 * Returns: 0 on success, non-zero on failure.
 *
 * This function updates the GOT entries in all DSOs outside the capsule
 * _except_ those listed in @except: When they call dlopen(), instead
 * they invoke the copy of that function provided by libcapsule.
 *
 * In the unlikely event that an error message is returned in @error it is the
 * caller's responsibility to free() it.
 */
int _capsule_relocate_dlopen (const capsule capsule,
                              char **error);
