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
 * @real: address of the ‘real’ symbol in the target library
 * @shim: address of the ‘fake’ symbol in the proxy library
 *
 * @real and @shim may typically be left empty by the shim library.
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
 * capsule_metadata:
 * @capsule_abi: Version of the libcapsule ABI implmented by this struct
 * @soname: The soname of the encapsulated library
 * @default_prefix: The default root location of the filesystem from which the
 *                  encapsulated library should be loaded
 * @exclude: (array zero-terminated=1): an array of char *, each
 *           specifying a DSO not to load, terminated by a %NULL entry
 * @export: (array zero-terminated=1): an array of char *, each
 *          specifying a DSO whose symbols should be exported from this
 *          capsule
 * @items: (array zero-terminated=1): Array of capsule_item
 *          specifying which symbols to export, terminated by a
 *          #capsule_item whose @name is %NULL
 * @int_dlopen: Implementation of the same API as `dlopen`
 *
 * This struct allows libcapsule proxy libraries to statically declare
 * metadata about themselves that libcapsule needs at link time in order
 * to function properly.
 *
 * The #capsule_item entries in @items need only specify the symbol
 * name: The shim and real fields will be populated automatically if they
 * are not pre-filled (this is the normal use case, as it would be unusual
 * to know these value in advance).
 */
typedef struct _capsule_metadata capsule_metadata;

struct _capsule_metadata
{
    /*< public >*/
    const int     capsule_abi;
    const char   *soname;
    const char   *default_prefix;
    const char  **exclude;
    const char  **export;
    capsule_item *items;
    void *(*int_dlopen) (const char *filename, int flag);
    void  (*int_free) (void *ptr);
    void *(*int_realloc) (void *ptr, size_t size);
    /*< private >*/
    capsule handle;
};

/**
 * capsule_init:
 * @soname: the soname of the target library
 *
 * Returns: a #capsule handle.
 *
 * Does any initialisation necessary to use libcapsule’s functions.
 *
 * Initialises internal accounting structures within the capsule
 * and triggers the metadata setup if this caspsule has been
 * acquired via dlopen(), and finishes registering the capsule
 * proxy with libcapsule itself.
 */
_CAPSULE_PUBLIC
capsule capsule_init (const char *soname);

////////////////////////////////////////////////////////////////////////////
// do not move the Returns declaration in this next doc-comment:
// gtk-doc does not tolerate any non-whitespace text between the
// last parameter and the Returns line if the name of the function
// is capsule_shim_dlopen. This was discovered empirically.
// As this is clearly insane, it must be a bad interaction with something
// else which shares some element of that name, but I have no idea what.
////////////////////////////////////////////////////////////////////////////

/**
 * capsule_shim_dlopen:
 * @cap: The capsule from which `dlopen` was called
 * @file: SONAME or filename to be opened
 * @flag: `dlopen` flags to pass to the real `dlopen` call
 *
 * Returns: A handle as if for `dlopen`
 *
 * An implementation of dlopen suitable to be called from inside a
 * namespace. Load @file into @cap namespace.
 *
 * If @cap has a non-trivial prefix, load @file and its recursive
 * dependencies from @cap prefix instead of from the root filesystem.
 *
 * This helper function exists because dlopen() cannot safely be called
 * by a DSO opened into a private namespace. It takes @file and @flag
 * arguments cf dlopen() and a @cap handle,
 * and performs a safe dlmopen() call instead.
 *
 * Typically this function is used to implement a safe wrapper for dlopen()
 * which is assigned to the int_dlopen member of the #capsule_metadata.
 * This * replaces calls to dlopen() by all DSOs in the capsule,
 * allowing libraries which use dlopen() to work inside the capsule.
 *
 * Limitations: RTLD_GLOBAL is not supported in @flag. This is a glibc
 * limitation in the dlmopen() implementation.
 */
_CAPSULE_PUBLIC
void *capsule_shim_dlopen (const capsule cap, const char *file, int flag);

/**
 * capsule_external_dlsym:
 * @handle: A dl handle, as passed to dlsym()
 * @symbol: A symbol name, as passed to dlsym()
 *
 * An implementation of `dlsym`, used when it is called by the executable
 * or by a library outside the capsule.
 *
 * Some libraries have a use pattern in which their caller/user
 * uses dlsym() to obtain symbols rather than using those symbols
 * directly in its own code (libGL is an example of this).
 *
 * Since the target library may have a different symbol set than the
 * one the libcapsule proxy shim was generated from we can’t rely on
 * dlsym() finding those symbols in the shim’s symbol table.
 *
 * Instead we must intercept dlsym() calls made outside the capsule
 * and attempt to look for the required symbol in the namespace defined
 * by the active capsules first - If the required symbol is found there
 * AND is from one of the DSO names present in the exported list then that
 * symbol is returned. If either of those conditions is not met then
 * a normal dlsym call with the passed handle is made.
 *
 * This function provides the functionality described above, and is
 * normally used automatically by libcapsule. It is exposed as API in
 * case a libcapsule proxy library needs to provide its own specialised
 * symbol lookup mechanism.
 *
 * Returns: The address associated with @symbol (as if for `dlsym`), or %NULL
 */
_CAPSULE_PUBLIC
void *capsule_external_dlsym (void *handle, const char *symbol);

/**
 * capsule_external_dlopen:
 * @file: A soname, filename or path as passed to dlopen()
 * @flag: The dl flags, as per dlopen()
 *
 * An implementation of `dlopen`, used when it is called by the executable
 * or by a library outside the capsule.
 *
 * This wrapper is meant to be replace normal calls to dlopen() made by the
 * main program or a non-capsule library - it is necessary because en ELF
 * object loaded by dlopen() may need us to trigger the _capsule_relocate()
 * operation in order to make sure its GOT entries are correctly updated.
 *
 * This wrapper carries out a normal dlopen() and then re-triggers the
 * initial _capsule_relocate() call immediately, before returning
 * the same value that dlopen() would have, given the same @file and @flag
 * arguments.
 *
 * Returns: The handle returned by `dlopen`
 */
_CAPSULE_PUBLIC
void *capsule_external_dlopen(const char *file, int flag);

/**
 * capsule_close:
 * @cap: a #capsule handle as returned by capsule_init()
 *
 * This function should be called from a capsule proxy library’s destructor:
 * Its job is to clean up capsule-specific allocated memory and metadata when
 * a capsule proxy is discarded via dlclose(), and ensure that libproxy itself
 * won’t try to access any related invalidated memory afterwards.
 */
_CAPSULE_PUBLIC
void capsule_close (capsule cap);

/**
 * capsule_get_prefix:
 * @dflt: A default capsule prefix path
 * @soname: The soname of the library we are encapsulating
 *
 * Returns: A newly allocated char * pointing to the prefix path
 *
 * libcapsule provides a proxy to a library, potentially from a foreign
 * filesystem tree (found at, for example, ‘/host’).
 *
 * Since it is useful for this location to be overrideable at startup
 * on a per-target-library basis this function standardises the prefix
 * selection algorithm as follows:
 *
 * - An environment variable based on @soname:
 *   libGL.so.1 would map to CAPSULE_LIBGL_SO_1_PREFIX
 * - If that is unset, the CAPSULE_PREFIX environment variable
 * - Next: The default to the value passed in @dflt
 * - And if all that failed, NULL (which is internally equivalent to "/")
 *
 * Although the value is newly allocated it will typically be cached
 * in a structure that needs to survive the entire lifespan of the
 * running program, so freeing it is unlikely to be a concern.
 **/
_CAPSULE_PUBLIC
char *capsule_get_prefix(const char *dflt, const char *soname);

/**
 * capsule_shim_free:
 * @cap: The capsule from which `free` was called
 * @ptr: The pointer to be freed
 *
 * Tries to safely route an allocated pointer to the correct free()
 * implementation.
 *
 */
_CAPSULE_PUBLIC
void capsule_shim_free (const capsule cap, void *ptr);

_CAPSULE_PUBLIC
void * capsule_shim_realloc (const capsule cap, void *ptr, size_t size);

