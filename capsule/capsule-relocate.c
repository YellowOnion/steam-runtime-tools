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
static int
process_phdr (struct dl_phdr_info *info,
              size_t size,
              relocation_data_t *rdata)
{
    int ret = 0;

    for( int j = 0; !ret && (j < info->dlpi_phnum); j++ )
        if( info->dlpi_phdr[j].p_type == PT_DYNAMIC )
            ret = process_pt_dynamic( (void *) info->dlpi_phdr[j].p_vaddr,
                                      info->dlpi_phdr[j].p_memsz,
                                      info->dlpi_addr,
                                      process_dt_rela,
                                      process_dt_rel,
                                      rdata );

    return ret;
}

static int
dso_is_blacklisted (const char *path, const char **blacklist)
{
    char **soname;

    if( blacklist == NULL )
        return 0;

    for( soname = (char **) blacklist; soname && *soname; soname++ )
        if( soname_matches_path( *soname, path ) )
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
    relocation_data_t *rdata = data;
    const char *dso_path = *info->dlpi_name ? info->dlpi_name : "-elf-";

    if( dso_is_blacklisted( dso_path, rdata->blacklist ) )
    {
        DEBUG( DEBUG_RELOCS, "skipping %s (blacklisted)", dso_path );
        return 0;
    }

    DEBUG( DEBUG_RELOCS, "processing %s", dso_path );

    return process_phdr( info, size, rdata );
}

static int relocate (const capsule cap,
                     capsule_item *relocations,
                     const char **dso_blacklist,
                     int keep_relocs,
                     char **error)
{
    relocation_data_t rdata = { 0 };
    capsule_item *map;
    int mmap_errno = 0;
    const char *mmap_error = NULL;
    int rval = 0;

    if( keep_relocs )
    {
        if( cap->relocations == NULL )
        {
            DEBUG( DEBUG_RELOCS,
                   "caching relocation list: %p", relocations );
            cap->relocations = relocations;
        }
        else if( cap->relocations != NULL &&
                 relocations      != NULL &&
                 cap->relocations != relocations )
        {
            DEBUG( DEBUG_RELOCS,
                   "overwriting cached relocation list (bug?): %p ← %p",
                   cap->relocations, relocations );
            cap->relocations = relocations;
        }

        if( relocations != NULL &&
            relocations != cap->relocations )
        {
            DEBUG( DEBUG_RELOCS,
                   "using one-shot relocation list %p", relocations );
            rdata.relocs = relocations;
        }
        else
        {
            DEBUG( DEBUG_RELOCS,
                   "using cached relocation list %p", cap->relocations );
            rdata.relocs = cap->relocations;
        }
    }
    else
    {
        rdata.relocs = relocations;
    }

    // load the relevant metadata into the callback argument:
    rdata.debug     = debug_flags;
    rdata.error     = NULL;
    rdata.blacklist = dso_blacklist;
    rdata.mmap_info = load_mmap_info( &mmap_errno, &mmap_error );

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
    // otherwise populate the map using dlsym():
    if( cap->dl_handle )
        for( map = cap->relocations; map->name; map++ )
        {
            if( !map->shim )
                map->shim = (ElfW(Addr)) dlsym( RTLD_DEFAULT, map->name );

            if( !map->real )
                map->real = (ElfW(Addr)) dlsym( cap->dl_handle, map->name );
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
capsule_relocate (const capsule cap, capsule_item *relocations, char **error)
{
    return relocate( cap, relocations, NULL, 1, error );
}

int
capsule_relocate_except (const capsule cap,
                         capsule_item *relocations,
                         const char **except,
                         char **error)
{
    unsigned long df = debug_flags;
    debug_flags |= DEBUG_RELOCS;

    int rv = relocate( cap, relocations, except, 0, error );

    debug_flags = df;

    return rv;
}
