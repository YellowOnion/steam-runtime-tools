// Copyright © 2017 Collabora Ltd

// This file is part of libcapsule.

// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.

// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <link.h>

#define _CAPSULE_PUBLIC __attribute__((visibility("default"))) extern

/**
 * capsule_addr:
 *
 * Identical to an ElfW(Addr) from libelf. You may treat this as
 * equivalent to a void * when assigning to it.
 */
typedef ElfW(Addr) capsule_addr;

/**
 * capsule
 *
 * A handle returned by capsule_init: A required parameter for all
 * other capsule calls.
 */
typedef struct _capsule *capsule;

/**
 * capsule_item:
 * @name: The name of the symbol to be relocated
 * @shim: address of the ‘fake’ symbol in the proxy library
 * @real: address of the ‘real’ symbol in the target library
 *
 * @shim may typically be left empty in calls to capsule_dlmopen()
 * and capsule_relocate().
 *
 * @real may also be left empty in calls to capsule_relocate()
 *
 * Both slots will typically hold the correct values after a successful
 * capsule… call. While this is sometimes important internally it is
 * not usually of interest to the caller (except maybe for debugging)
 */
typedef struct _capsule_item capsule_item;

struct _capsule_item
{
    const char *name;
    capsule_addr real;
    capsule_addr shim;

    /*< private >*/
    void *unused0;
    void *unused1;
    void *unused2;
    void *unused3;
};

/**
 * capsule_init:
 * @namespace: An #Lmid_t value. (usually %LM_ID_NEWLM)
 * @prefix: The mount point of the foreign tree in wich to find DSOs
 * @exclude: (array zero-terminated=1): an array of char *, each
 *           specifying a DSO not to load, terminated by a %NULL entry
 * @exported: An array of DSO names considered to ba valid symbol sources
 *
 * Returns a #capsule handle.
 *
 * Does any initialisation necessary to use libcapsule's functions.
 * Currently just initialises the debug flags from the CAPSULE_DEBUG
 * environment variable.
 *
 * The #Lmid_t value @namespace should normally be %LM_ID_NEWLM
 * to create a new namespace.
 *
 * An empty ("") or void (%NULL) @prefix is equivalent to "/".
 */
_CAPSULE_PUBLIC
capsule capsule_init (Lmid_t namespace,
                      const char *prefix,
                      const char **exclude,
                      const char **exported);

/**
 * capsule_relocate:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @relocations: (array zero-terminated=1): Array of capsule_item
 *               specifying which symbols to export, terminated by a
 *               #capsule_item whose @name is %NULL
 * @error: (out) (transfer full) (optional): location in which to store
 *         an error message on failure, or %NULL to ignore.
 *         Free with free().
 *
 * Returns: 0 on success, non-zero on failure.
 *
 * @source is typically the value returned by a successful capsule_dlmopen()
 * call (although a handle returned by dlmopen() would also be reasonable).
 *
 * The #capsule_item entries in @relocations need only specify the symbol
 * name: The shim and real fields will be populated automatically if they
 * are not pre-filled (this is the normal use case, as it would be unusual
 * to know these value in advance).
 *
 * In the unlikely event that an error message is returned in @error it is the
 * caller's responsibility to free() it.
 */
_CAPSULE_PUBLIC
int capsule_relocate (const capsule capsule,
                      capsule_item *relocations,
                      char **error);


/**
 * capsule_relocate_restricted:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @relocations: (array zero-terminated=1): Array of capsule_item
 *               specifying which symbols to export, terminated by a
 *               #capsule_item whose @name is %NULL
 * @dso_blacklist: (array zero-terminated=1) (optional): Array of sonames
 *                 which should not have their GOT entries updated.
 * @error: (out) (transfer full) (optional): location in which to store
 *         an error message on failure, or %NULL to ignore.
 *         Free with free().
 *
 * Returns: 0 on success, non-zero on failure.
 *
 * @source is typically the value returned by a successful capsule_load()
 * call (although a handle returned by dlmopen() would also be reasonable).
 *
 * The #capsule_item entries in @relocations need only specify the symbol
 * name: The shim and real fields will be populated automatically if they
 * are not pre-filled (this is the normal use case, as it would be unusual
 * to know these value in advance).
 *
 * This function updates the GOT entries in all DSOs outside the capsule
 * _except_ those listed in @dso_blacklist: When they call any function
 * listed in @relocations they invoke the copy of that function inside
 * the capsule. These sonames should be of the form "libfoo.so.X"
 * or "libfoo.so". You may specify further minor version numbers in the
 * usual "libfoo.so.X.Y" format if you wish.
 *
 * In the unlikely event that an error message is returned in @error it is the
 * caller's responsibility to free() it.
 */
_CAPSULE_PUBLIC
int capsule_relocate_restricted (const capsule cap,
                                 capsule_item *relocations,
                                 const char **dso_blacklist,
                                 char **error);

/**
 * capsule_load:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @dso: The name of the DSO to open (cf dlopen()) - eg libGL.so.1
 * @namespace: (out) Address of an #Lmid_t value.
 * @wrappers: Array of #capsule_item used to replace symbols in the namespace
 * @errcode: (out): location in which to store the error code (errno) on failure
 * @error: (out) (transfer full) (optional): location in which to store
 *         an error message on failure, or %NULL to ignore.
 *         Free with free().
 *
 * Returns: A (void *) DSO handle, as per dlopen(3), or %NULL on error
 *
 * Opens @dso (a library) from a filesystem mounted at @prefix into a
 * symbol namespace specified by @namespace, using dlmopen().
 *
 * Any symbols specified in @wrappers will be replaced with the
 * corresponding address from @wrappers (allowing you to replace
 * function definitions inside the namespace with your own).
 * This is normally used to replace calls from inside the namespace to
 * dlopen() (which would cause a segfault) with calls to dlmopen().
 *
 * The #Lmid_t value stored in @namespace gives the symbol namespace
 * into which the target DSO was loaded.
 *
 * In addition to a bare libFOO.so.X style name, @dso may be an
 * absolute path (or even a relative one) and in those cases should
 * have the same effect as passing those values to dlopen(). This is
 * not a normal use case and has not been heavily tested.
 *
 */
_CAPSULE_PUBLIC
void *capsule_load (const capsule capsule,
                    const char *dso,
                    Lmid_t *namespace,
                    capsule_item *wrappers,
                    int *errcode,
                    char **error);

/**
 * capsule_shim_dlopen:
 * @capsule: a #capsule handle as returned by capsule_init()
 * @file: base name of the target DSO (eg libz.so.1)
 * @flag: dlopen() flags to pass to the real dlmopen() call
 *
 * Returns: a void * dl handle (cf dlopen())
 *
 * This helper function exists because dlopen() cannot safely be called
 * by a DSO opened into a private namespace. It takes @file and @flag
 * arguments (cf dlopen()) and a @capsule handle (cf capsule_load())
 * and performs a safe dlmopen() call instead, respecting the same
 * restrictions as capsule_load().
 *
 * Typically this function is used to implement a safe wrapper for dlopen()
 * which is passed via the wrappers argument to capsule_load(). This
 * replaces calls to dlopen() by all DSOs in the capsule produced by
 * capsule_load(), allowing libraries which use dlopen() to work inside
 * the capsule.
 *
 * Limitations: RTLD_GLOBAL is not supported in @flag. This is a glibc
 * limitation in the dlmopen() implementation.
 */
_CAPSULE_PUBLIC
void *capsule_shim_dlopen(const capsule capsule, const char *file, int flag);

/**
 * capsule_external_dlsym:
 * @capsule: A dl handle as returned by capsule_load()
 * @handle: A dl handle, as passed to dlsym()
 * @symbol: A symbol name, as passed to dlsym()
 *
 * Returns: a void * symbol address (cf dlsym())
 *
 * Some libraries have a use pattern in which their caller/user
 * uses dlsym() to obtain symbols rather than using those symbols
 * directly in its own code (libGL is an example of this).
 *
 * Since the target library may have a different symbol set than the
 * one the libcapsule proxy shim was generated from we can't rely on
 * dlsym() finding those symbols in the shim's symbol table.
 *
 * Instead we must intercept dlsym() calls made outside the capsule
 * and attempt to look for the required symbol in the namespace defined
 * by @capsule first - If the required symbol is found there AND is
 * from one of the DSO names present in @exported then that symbol is
 * returned. If either of those conditions is not met then a normal
 * dlsym call with the passed handle is made.
 *
 * This function provides the functionality described above, and is
 * intended for use in a suitable wrapper implemented in the the shim
 * library.
 */
_CAPSULE_PUBLIC
void *capsule_external_dlsym (capsule cap, void *handle, const char *symbol);
