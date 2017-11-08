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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <capsule/capsule.h>
#include "capsule/capsule-private.h"

#include "utils/dump.h"
#include "utils/utils.h"
#include "utils/process-pt-dynamic.h"

// ==========================================================================
// some entries require us to peer into others to make sense of them:
// can't make full sense of relocations without looking names up
// in the stringtab, which does not have to occur at any fixed point in
// in the PT_DYNAMIC entry.
// IOW PT_DYNAMIC contains both relocations (DT_RELA, DT_REL) and a stringtab
// (DT_STRTAB) in arbitrary order but the former do not make sense without
// the latter.


// =========================================================================

/*
 * process_phdr:
 * @info: struct dl_phdr_info describing the shared object
 * @size: sizeof(info)
 * @rdata: the closure that we passed to dl_iterate_phdr()
 *
 * Callback called for each shared object loaded into the program.
 */
static int
process_phdr (struct dl_phdr_info *info,
              size_t size,
              relocation_data *rdata)
{
    int ret = 0;

    for( int j = 0; !ret && (j < info->dlpi_phnum); j++ )
        if( info->dlpi_phdr[j].p_type == PT_DYNAMIC )
            ret = process_pt_dynamic( info->dlpi_phdr[j].p_vaddr,
                                      info->dlpi_phdr[j].p_memsz,
                                      (void *) info->dlpi_addr,
                                      process_dt_rela,
                                      process_dt_rel,
                                      rdata );

    if( ret == 0 && rdata->seen != NULL )
        ptr_list_push_addr( rdata->seen, info->dlpi_addr );

    return ret;
}

static int
dso_is_blacklisted (const char *path, relocation_flags flags)
{
    const char * const *soname;
    const char * const libc[] =
    {
        "libc.so",
        "libdl.so",
        "libpthread.so",
        NULL
    };
    static const char *never = "libcapsule.so";

    if( soname_matches_path( never, path ) )
        return 1;

    if( flags & RELOCATION_FLAGS_AVOID_LIBC )
        for( soname = libc; soname && *soname; soname++ )
            if( soname_matches_path( *soname, path ) )
                return 1;

    return 0;
}

static int
dso_has_been_relocated (ptr_list *seen, ElfW(Addr) base)
{
    if( seen == NULL )
        return 0;

    if( ptr_list_contains( seen, base ) )
        return 1;

    return 0;
}

// first level of the callback: all we're doing here is skipping over
// any program headers that (for whatever reason) we decide we're not
// interested in.
// In practice we have to handle all existing DSOs, as any of them may
// call into the library we are acting as a shim for.
static int
relocate_cb (struct dl_phdr_info *info, size_t size, void *data)
{
    relocation_data *rdata = data;
    const char *dso_path = *info->dlpi_name ? info->dlpi_name : "-elf-";

    if( dso_is_blacklisted( dso_path, rdata->flags ) )
    {
        DEBUG( DEBUG_RELOCS, "skipping %s %p (blacklisted)",
               dso_path, (void *) info->dlpi_addr );
        return 0;
    }

    if( dso_has_been_relocated( rdata->seen, info->dlpi_addr ) )
    {
        DEBUG( DEBUG_RELOCS, "skipping %s %p (already relocated)",
               dso_path, (void *) info->dlpi_addr );
        return 0;
    }

    DEBUG( DEBUG_RELOCS, "processing %s %p",
           dso_path, (void *) info->dlpi_addr );

    return process_phdr( info, size, rdata );
}

static int relocate (const capsule cap,
                     capsule_item *relocations,
                     relocation_flags flags,
                     ptr_list *seen,
                     char **error)
{
    relocation_data rdata = { 0 };
    capsule_item *map;
    int mmap_errno = 0;
    const char *mmap_error = NULL;
    int rval = 0;

    // load the relevant metadata into the callback argument:
    rdata.debug     = debug_flags;
    rdata.error     = NULL;
    rdata.flags     = flags;
    rdata.mmap_info = load_mmap_info( &mmap_errno, &mmap_error );
    rdata.relocs    = relocations;
    rdata.seen      = seen;

    if( mmap_errno || mmap_error )
    {
        DEBUG( DEBUG_RELOCS|DEBUG_MPROTECT,
               "mmap/mprotect flags information load error (errno: %d): %s\n",
               mmap_errno, mmap_error );
        DEBUG( DEBUG_RELOCS|DEBUG_MPROTECT,
               "relocation will be unable to handle relro linked libraries" );
    }

    // no source dl handle means we must have a pre-populated
    // map of shim-to-real function pointers in `relocations',
    // otherwise populate the map using [the real] dlsym():
    if( cap->dl_handle )
        for( map = cap->meta->items; map->name; map++ )
        {
            if( !map->shim )
                map->shim = (ElfW(Addr)) _capsule_original_dlsym( RTLD_DEFAULT, map->name );

            if( !map->real )
                map->real = (ElfW(Addr)) _capsule_original_dlsym( cap->dl_handle, map->name );
        }

    // time to enter some sort of ... dangerous... zone:
    // we need the mmap()ed DSO regions to have the PROT_WRITE
    // flag set, so that if they've been RELRO linked we can still
    // overwrite their GOT entries.
    for( int i = 0; rdata.mmap_info[i].start != MAP_FAILED; i++ )
        if( mmap_entry_should_be_writable( &rdata.mmap_info[i] ) )
            add_mmap_protection( &rdata.mmap_info[i], PROT_WRITE );

    dl_iterate_phdr( relocate_cb, &rdata );

    // and now we put those PROT_WRITE permissions back the way they were:
    for( int i = 0; rdata.mmap_info[i].start != MAP_FAILED; i++ )
        if( mmap_entry_should_be_writable( &rdata.mmap_info[i] ) )
            reset_mmap_protection( &rdata.mmap_info[i] );

    if( rdata.error )
    {
        if( error )
            *error = rdata.error;
        else
            free( rdata.error );

        rval = (rdata.count.failure == 0) ? -1 : rdata.count.failure;
    }

    free_mmap_info( rdata.mmap_info );
    rdata.mmap_info = NULL;

    return rval;
}

int
_capsule_relocate (const capsule cap, char **error)
{
    DEBUG( DEBUG_RELOCS, "beginning global symbol relocation:" );
    return relocate( cap, cap->meta->items, RELOCATION_FLAGS_NONE, cap->seen.all, error );
}

static capsule_item capsule_external_dl_relocs[] =
{
  { "dlopen",
    (capsule_addr) capsule_external_dlopen ,
    (capsule_addr) capsule_external_dlopen },
  { NULL }
};

int
_capsule_relocate_dlopen (const capsule cap,
                          char **error)
{
    unsigned long df = debug_flags;

    if( debug_flags & DEBUG_DLFUNC )
        debug_flags |= DEBUG_RELOCS;

    DEBUG( DEBUG_RELOCS, "beginning restricted symbol relocation:" );
    int rv = relocate( cap, capsule_external_dl_relocs,
                       RELOCATION_FLAGS_AVOID_LIBC, cap->seen.some, error );

    debug_flags = df;

    return rv;
}
