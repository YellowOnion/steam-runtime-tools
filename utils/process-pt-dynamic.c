// Copyright © 2017-2019 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later

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

#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <capsule/capsule.h>
#include "utils.h"
#include "process-pt-dynamic.h"
#include "mmap-info.h"

static void *
#if __ELF_NATIVE_CLASS == 32
addr (void *base, ElfW(Addr) offset, ElfW(Sword) addend)
#elif __ELF_NATIVE_CLASS == 64
addr (void *base, ElfW(Addr) offset, ElfW(Sxword) addend)
#else
#error "Unsupported __ELF_NATIVE_CLASS size (not 32 or 64)"
#endif
{
    return base + offset + addend;
}


static int
try_relocation (ElfW(Addr) *reloc_addr, const char *name, void *data)
{
    capsule_item *map;
    relocation_data *rdata = data;

    if( !name || !*name || !reloc_addr )
        return 0;

    for( map = rdata->relocs; map->name; map++ )
    {
        if( strcmp( name, map->name ) )
            continue;

        DEBUG( DEBUG_RELOCS,
               "relocation for %s (%p->{ %p }, %p, %p)",
               name, reloc_addr, NULL, (void *)map->shim, (void *)map->real );

        // we used to check for the shim address here but it's possible
        // that we can't look it up if the proxy library was dlopen()ed
        // in which case map->shim will be null.
        // this turns out not to be a problem as we only need it when
        // working around RELRO linking, which doesn't apply to dlopen()

        // sought after symbols is not available in the private namespace
        if( !map->real )
        {
            rdata->count.failure++;
            DEBUG( DEBUG_RELOCS, "--failed" );

            return 1;
        }

        // our work here is already done, apparently
        if( *reloc_addr == map->real )
        {
            DEBUG( DEBUG_RELOCS, "==target %p already contains %p (%p)",
                   reloc_addr, (void *)*reloc_addr, (void *)map->real );
            return 0;
        }
        // ======================================================================
        // exegesis:

        // linking goes like this: we start with a PLT entry pointing at the
        // 'trampoline' entry which patches up the relocations. The first
        // time we call a function, we go to the PLT which sends us to the
        // trampoline, which  finds the shim (in the case of our proxy library)
        // or the real address (in the case of a normal library) and pastes that
        // address into the PLT.

        // This function scribbles over the trampoline address with the real
        // address, thus bypassing the trampoline _and_ the shim permanently.

        /// IOW the 0th, 1st and second function calls normally look like this:
        // 0: function-call → PLT → trampoline : (PLT ← address) → address
        // 1: function-call → PLT → address
        // 2: ibid

        // If we are already pointing to the shim instead of the trampoline
        // that indicates we have RELRO linking - the linker has already resolved
        // the address to the shim (as it doesn't know about the real address
        // which is hidden inside the capsule).

        // -1: linker → function-lookup : (PLT ← address)
        //  0: function-call → PLT → address
        //  1: ibid

        // but⁰ RELRO linking also mprotect()s the relevant pages to be read-only
        // which prevents us from overwriting the address.

        // but¹ we are smarter than the average bear, and we tried to harvest
        // the mprotect info: If we did, then we will already have toggled the
        // write permission on everything that didn't have it and can proceed
        // (we're also not savages, so we'll put those permissions back later)

        // however, if we don't have any mprotect into for this relocation entry,
        // then we can't de-shim the RELROd PLT entry, and it's sad 🐼 time.
        // ======================================================================
        if( (*reloc_addr == map->shim) &&
            !find_mmap_info(rdata->mmap_info, reloc_addr) )
        {
            DEBUG( DEBUG_RELOCS|DEBUG_MPROTECT,
                   " ERROR: cannot update relocation record for %s", name );
            return 1; // FIXME - already shimmed, can't seem to override?
        }

        *reloc_addr = map->real;
        rdata->count.success++;
        DEBUG( DEBUG_RELOCS, "++relocated" );
        return 0;
    }

    // nothing to relocate
    return 0;
}

#if defined(__x86_64__)

static const char *
reloc_type_name (int type)
{
    switch (type)
    {
      // Please keep these in numerical order.

#define CASE(x) \
      case x: \
        return #x;

      CASE( R_X86_64_NONE )
      CASE( R_X86_64_64 )
      CASE( R_X86_64_PC32 )
      CASE( R_X86_64_GOT32 )
      CASE( R_X86_64_PLT32 )
      CASE( R_X86_64_COPY )
      CASE( R_X86_64_GLOB_DAT )
      CASE( R_X86_64_JUMP_SLOT )
      CASE( R_X86_64_RELATIVE )
      CASE( R_X86_64_GOTPCREL )
      CASE( R_X86_64_32 )
      CASE( R_X86_64_32S )
      CASE( R_X86_64_16 )
      CASE( R_X86_64_PC16 )
      CASE( R_X86_64_8 )
      CASE( R_X86_64_PC8 )
      CASE( R_X86_64_DTPMOD64 )
      CASE( R_X86_64_DTPOFF64 )
      CASE( R_X86_64_TPOFF64 )
      CASE( R_X86_64_TLSGD )
      CASE( R_X86_64_TLSLD )
      CASE( R_X86_64_DTPOFF32 )
      CASE( R_X86_64_GOTTPOFF )
      CASE( R_X86_64_TPOFF32 )
      CASE( R_X86_64_PC64 )
      CASE( R_X86_64_GOTOFF64 )
      CASE( R_X86_64_GOTPC32 )
      CASE( R_X86_64_GOT64 )
      CASE( R_X86_64_GOTPCREL64 )
      CASE( R_X86_64_GOTPC64 )
      CASE( R_X86_64_GOTPLT64 )
      CASE( R_X86_64_PLTOFF64 )
      CASE( R_X86_64_SIZE32 )
      CASE( R_X86_64_SIZE64 )
      CASE( R_X86_64_GOTPC32_TLSDESC )
      CASE( R_X86_64_TLSDESC_CALL )
      CASE( R_X86_64_TLSDESC )
      CASE( R_X86_64_IRELATIVE )

      // Entries below this point are new since glibc 2.19

#ifdef R_X86_64_RELATIVE64
      CASE( R_X86_64_RELATIVE64 )
#endif
#ifdef R_X64_64_GOTPCRELX
      CASE( R_X86_64_GOTPCRELX )
#endif
#ifdef R_X64_64_REX_GOTPCRELX
      CASE( R_X86_64_REX_GOTPCRELX )
#endif

#undef CASE

      default:
        return "UNKNOWN";
    }
}

#elif defined(__i386__)

static const char *
reloc_type_name (int type)
{
    switch (type)
    {
      // Please keep these in numerical order.

#define CASE(x) \
      case x: \
        return #x;

      CASE( R_386_NONE )
      CASE( R_386_32 )
      CASE( R_386_PC32 )
      CASE( R_386_GOT32 )
      CASE( R_386_PLT32 )
      CASE( R_386_COPY )
      CASE( R_386_GLOB_DAT )
      CASE( R_386_JMP_SLOT )
      CASE( R_386_RELATIVE )
      CASE( R_386_GOTOFF )
      CASE( R_386_GOTPC )
      CASE( R_386_32PLT )
      CASE( R_386_TLS_TPOFF )
      CASE( R_386_TLS_IE )
      CASE( R_386_TLS_GOTIE )
      CASE( R_386_TLS_LE )
      CASE( R_386_TLS_GD )
      CASE( R_386_TLS_LDM )
      CASE( R_386_16 )
      CASE( R_386_PC16 )
      CASE( R_386_8 )
      CASE( R_386_PC8 )
      CASE( R_386_TLS_GD_32 )
      CASE( R_386_TLS_GD_PUSH )
      CASE( R_386_TLS_GD_CALL )
      CASE( R_386_TLS_GD_POP )
      CASE( R_386_TLS_LDM_32 )
      CASE( R_386_TLS_LDM_PUSH )
      CASE( R_386_TLS_LDM_CALL )
      CASE( R_386_TLS_LDM_POP )
      CASE( R_386_TLS_LDO_32 )
      CASE( R_386_TLS_IE_32 )
      CASE( R_386_TLS_LE_32 )
      CASE( R_386_TLS_DTPMOD32 )
      CASE( R_386_TLS_DTPOFF32 )
      CASE( R_386_TLS_TPOFF32 )
#ifdef R_386_SIZE32
      CASE( R_386_SIZE32 )
#endif
      CASE( R_386_TLS_GOTDESC )
      CASE( R_386_TLS_DESC_CALL )
      CASE( R_386_TLS_DESC )
      CASE( R_386_IRELATIVE )

      // Entries below this point are new since glibc 2.19

#ifdef R_386_GOT32X
      CASE( R_386_GOT32X )
#endif

#undef CASE

      default:
        return "UNKNOWN";
    }
}

#else

#error Unsupported CPU architecture

#endif

int
process_dt_rela (const ElfW(Rela) *start,
                 size_t relasz,
                 const char *strtab,
                 size_t strsz,
                 const ElfW(Sym) *symtab,
                 size_t symsz,
                 void *base,
                 void *data)
{
    const ElfW(Rela) *entry;

    DEBUG( DEBUG_ELF,
           "%zu RELA entries (%zu bytes) starting at %p",
           relasz / sizeof(*entry),
           relasz,
           start );

    if( relasz % sizeof(*entry) != 0 )
        DEBUG( DEBUG_ELF, "%zu bytes left over?!", relasz % sizeof(*entry) );

    for( entry = start; entry < start + (relasz / sizeof(*entry)); entry++ )
    {
        int sym;
        int chr;
        const char *name = NULL;
        const ElfW(Sym) *symbol;

#if __ELF_NATIVE_CLASS == 32
        sym = ELF32_R_SYM (entry->r_info);
        chr = ELF32_R_TYPE(entry->r_info);
#elif __ELF_NATIVE_CLASS == 64
        sym = ELF64_R_SYM (entry->r_info);
        chr = ELF64_R_TYPE(entry->r_info);
#else
        fprintf( stderr, "__ELF_NATIVE_CLASS is neither 32 nor 64" );
        exit( 22 );
#endif

        DEBUG( DEBUG_ELF, "RELA entry at %p", entry );

        symbol = find_symbol( sym, symtab, symsz, strtab, strsz, &name );

        DEBUG( DEBUG_ELF,
               "symbol %p; name: %p:%s", symbol, name, name ? name : "-" );

        if( !symbol || !name || !*name )
            continue;

#if defined(__i386__) || defined(__x86_64__)
        switch( chr )
        {
            void *slot;
       // details at: https://github.com/hjl-tools/x86-psABI/wiki/X86-psABI
#if defined(__i386__)
          case R_386_32:
          case R_386_GLOB_DAT:
          case R_386_JMP_SLOT:
#elif defined(__x86_64__)
          case R_X86_64_64:
          case R_X86_64_GLOB_DAT:
          case R_X86_64_JUMP_SLOT:
#else
#error Unsupported CPU architecture
#endif
            slot = addr( base, entry->r_offset, entry->r_addend );
            DEBUG( DEBUG_ELF,
                   " %30s %30s: %p ← { offset: %"FMT_ADDR"; add: %"FMT_SIZE" }",
                   name, reloc_type_name( chr ),
                   slot, entry->r_offset, entry->r_addend );
            try_relocation( slot, name, data );
            break;

          default:
            DEBUG( DEBUG_ELF,
                   "%s has slot type %s (%d), not doing anything special",
                   name, reloc_type_name( chr ), chr );
            break;
        }
#else
#error Unsupported CPU architecture
#endif
    }

    return 0;
}

int
process_dt_rel (const ElfW(Rel) *start,
                size_t relasz,
                const char *strtab,
                size_t strsz,
                const ElfW(Sym) *symtab,
                size_t symsz,
                void *base,
                void *data)
{
    const ElfW(Rel) *entry;

    DEBUG( DEBUG_ELF,
           "%zu REL entries (%zu bytes) starting at %p",
           relasz / sizeof(*entry),
           relasz,
           start );

    if( relasz % sizeof(*entry) != 0 )
        DEBUG( DEBUG_ELF, "%zu bytes left over?!", relasz % sizeof(*entry) );

    for( entry = start; entry < start + (relasz / sizeof(*entry)); entry++ )
    {
        int sym;
        int chr;
        const char *name = NULL;

        const ElfW(Sym) *symbol;

#if __ELF_NATIVE_CLASS == 32
        sym = ELF32_R_SYM (entry->r_info);
        chr = ELF32_R_TYPE(entry->r_info);
#elif __ELF_NATIVE_CLASS == 64
        sym = ELF64_R_SYM (entry->r_info);
        chr = ELF64_R_TYPE(entry->r_info);
#else
#error Unsupported CPU architecture
#endif

        symbol = find_symbol( sym, symtab, symsz, strtab, strsz, &name );

        DEBUG( DEBUG_ELF,
               "symbol %p; name: %p:%s", symbol, name, name ? name : "-" );

        if( !symbol || !name || !*name )
            continue;

#if defined(__i386__) || defined(__x86_64__)
        switch( chr )
        {
            void *slot;
       // details at: https://github.com/hjl-tools/x86-psABI/wiki/X86-psABI
#if defined(__i386__)
          case R_386_32:
          case R_386_GLOB_DAT:
          case R_386_JMP_SLOT:
#elif defined(__x86_64__)
          case R_X86_64_64:
          case R_X86_64_GLOB_DAT:
          case R_X86_64_JUMP_SLOT:
#else
#error Unsupported CPU architecture
#endif
            slot = addr( base, entry->r_offset, 0 );
            DEBUG( DEBUG_ELF,
                   " %30s %30s: %p ← { offset: %"FMT_ADDR"; addend: n/a }",
                   name,
                   reloc_type_name( chr ),
                   slot, entry->r_offset );
            try_relocation( slot, name, data );
            break;

          default:
            DEBUG( DEBUG_ELF,
                   "%s has slot type %s (%d), not doing anything special",
                   name, reloc_type_name( chr ), chr );
            break;
        }
#else
#error Unsupported CPU architecture
#endif
    }

    return 0;
}

/*
 * process_pt_dynamic:
 * @start: offset of dynamic section (an array of ElfW(Dyn) structures)
 *  relative to @base
 * @size: size of dynamic section in bytes (not structs!), or 0
 *  if the dynamic section is terminated by an entry with d_tag == DT_NULL
 * @base: Starting address of the program header (the shared object)
 *  in memory. @start is relative to this. Addresses are normally
 *  relative to this, except for when they are absolute (see fix_addr()).
 * @process_rela: called when we find the DT_RELA section
 * @process_rel: called when we find the DT_REL section
 * @data: arbitrary user data to be passed to both @process_rela
 *  and @process_rel
 *
 * Iterate over the PT_DYNAMIC entry in a shared library and perform
 * relocations using the given callbacks.
 */
int
process_pt_dynamic (ElfW(Addr) start,
                    size_t size,
                    void *base,
                    relocate_rela_cb process_rela,
                    relocate_rel_cb process_rel,
                    void *data)
{
    int ret = 0;
    const ElfW(Dyn) *entries;
    const ElfW(Dyn) *entry;
    size_t relasz   = (size_t) -1;
    size_t relsz    = (size_t) -1;
    size_t jmprelsz = (size_t) -1;
    int jmpreltype = DT_NULL;
    const ElfW(Sym) *symtab = NULL;
    size_t strsz = (size_t) -1;
    const char *strtab = dynamic_section_find_strtab( base + start, base, &strsz );
    size_t symsz;

    DEBUG( DEBUG_ELF,
           "start: %#" PRIxPTR "; size: %" FMT_SIZE "; base: %p; handlers: %p %p; …",
           start, size, base, process_rela, process_rel );
    entries = base + start;
    DEBUG( DEBUG_ELF, "dyn entry: %p", entries );

    DEBUG( DEBUG_ELF,
           "strtab is at %p: %s", strtab, strtab ? "…" : "");

    // Do a first pass to find the bits we'll need later
    for( entry = entries;
         (entry->d_tag != DT_NULL) &&
           ((size == 0) || ((void *)entry < (start + base + size)));
         entry++ ) {
        switch( entry->d_tag )
        {
          case DT_PLTRELSZ:
            jmprelsz = entry->d_un.d_val;
            DEBUG( DEBUG_ELF, "jmprelsz is %zu", jmprelsz );
            break;

          case DT_SYMTAB:
            symtab = fix_addr( base, entry->d_un.d_ptr );
            DEBUG( DEBUG_ELF, "symtab is %p", symtab );
            break;

          case DT_RELASZ:
            relasz = entry->d_un.d_val;
            DEBUG( DEBUG_ELF, "relasz is %zu", relasz );
            break;

          case DT_RELSZ:
            relsz = entry->d_un.d_val;
            DEBUG( DEBUG_ELF, "relsz is %zu", relsz );
            break;

          case DT_PLTREL:
            jmpreltype = entry->d_un.d_val;
            DEBUG( DEBUG_ELF, "jmpreltype is %d : %s", jmpreltype,
                   jmpreltype == DT_REL  ? "DT_REL"  :
                   jmpreltype == DT_RELA ? "DT_RELA" : "???" );
            break;

          default:
            // We'll deal with this later
            break;
        }
    }

    /* XXX Apparently the only way to find out the size of the dynamic
       symbol section is to assume that the string table follows right
       afterwards... —glibc elf/dl-fptr.c */
    assert( strtab >= (const char *) symtab );
    symsz = strtab - (const char *) symtab;

    DEBUG( DEBUG_ELF,
           "%zu symbol table entries (%zu bytes) starting at %p",
           symsz / sizeof(*symtab),
           symsz,
           symtab );

    if( symsz % sizeof(*symtab) != 0 )
        DEBUG( DEBUG_ELF, "%zu bytes left over?!", symsz % sizeof(*symtab) );

    for( entry = entries;
         (entry->d_tag != DT_NULL) &&
           ((size == 0) || ((void *)entry < (start + base + size)));
         entry++ ) {
        switch( entry->d_tag )
        {
          // Please keep the contents of this switch in numerical order.

          // IGNORE(x): Ignore a known d_tag that we believe we don't need
          // to know about
#define IGNORE(x) \
          case x: \
            DEBUG( DEBUG_ELF, "ignoring %s (0x%zx): 0x%zx", \
                   #x, (size_t) x, (size_t) entry->d_un.d_val); \
            break;

          // ALREADY_DID(x): Silently ignore a case we dealt with in
          // the previous loop
#define ALREADY_DID(x) \
          case x: \
            break;

          IGNORE( DT_NEEDED )
          ALREADY_DID( DT_PLTRELSZ )
          IGNORE( DT_PLTGOT )
          IGNORE( DT_HASH )
          IGNORE( DT_STRTAB )
          ALREADY_DID( DT_SYMTAB )

          case DT_RELA:
            if( process_rela != NULL )
            {
                const ElfW(Rela) *relstart;

                DEBUG( DEBUG_ELF, "processing DT_RELA section" );
                if( relasz == (size_t) -1 )
                {
                    fprintf( stderr, "libcapsule: DT_RELA section not accompanied by DT_RELASZ, ignoring" );
                    break;
                }
                relstart = fix_addr( base, entry->d_un.d_ptr );
                process_rela( relstart, relasz, strtab, strsz, symtab, symsz, base, data );
            }
            else
            {
                DEBUG( DEBUG_ELF,
                       "skipping DT_RELA section: no handler" );
            }
            break;

          ALREADY_DID( DT_RELASZ )
          IGNORE( DT_RELAENT )
          IGNORE( DT_STRSZ )
          IGNORE( DT_SYMENT )
          IGNORE( DT_INIT )
          IGNORE( DT_FINI )
          IGNORE( DT_SONAME )
          IGNORE( DT_RPATH )
          IGNORE( DT_SYMBOLIC )

          case DT_REL:
            if( process_rel != NULL )
            {
                const ElfW(Rel) *relstart;

                DEBUG( DEBUG_ELF, "processing DT_REL section" );
                if( relsz == (size_t) -1 )
                {
                    fprintf( stderr, "libcapsule: DT_REL section not accompanied by DT_RELSZ, ignoring" );
                    break;
                }
                relstart = fix_addr( base, entry->d_un.d_ptr );
                process_rel( relstart, relsz, strtab, strsz, symtab, symsz, base, data );
            }
            else
            {
                DEBUG( DEBUG_ELF, "skipping DT_REL section: no handler" );
            }
            break;

          ALREADY_DID( DT_RELSZ )
          IGNORE( DT_RELENT )
          ALREADY_DID( DT_PLTREL )
          IGNORE( DT_DEBUG )
          IGNORE( DT_TEXTREL )

          case DT_JMPREL:
            if( jmprelsz == (size_t) -1 )
            {
                fprintf( stderr, "libcapsule: DT_JMPREL section not accompanied by DT_PLTRELSZ, ignoring" );
                break;
            }

            if( jmpreltype == DT_NULL )
            {
                fprintf( stderr, "libcapsule: DT_JMPREL section not accompanied by DT_PLTREL, ignoring" );
                break;
            }

            switch( jmpreltype )
            {
              case DT_REL:
                if( process_rel != NULL )
                {
                    const ElfW(Rel) *relstart;

                    DEBUG( DEBUG_ELF,
                           "processing DT_JMPREL/DT_REL section" );
                    relstart = fix_addr( base, entry->d_un.d_ptr );
                    DEBUG( DEBUG_ELF, "  -> REL entry #0 at %p", relstart );
                    ret = process_rel( relstart, jmprelsz, strtab, strsz,
                                       symtab, symsz, base, data );
                }
                else
                {
                    DEBUG( DEBUG_ELF,
                           "skipping DT_JMPREL/DT_REL section: no handler" );
                }
                break;

              case DT_RELA:
                if( process_rela != NULL )
                {
                    const ElfW(Rela) *relstart;

                    DEBUG( DEBUG_ELF,
                           "processing DT_JMPREL/DT_RELA section" );
                    relstart = fix_addr( base, entry->d_un.d_ptr );
                    ret = process_rela( relstart, jmprelsz, strtab, strsz,
                                        symtab, symsz, base, data );
                }
                else
                {
                    DEBUG( DEBUG_ELF,
                           "skipping DT_JMPREL/DT_RELA section: no handler" );
                }
                break;

              default:
                DEBUG( DEBUG_ELF,
                       "Unknown DT_PLTREL value: %d (expected %d or %d)",
                       jmpreltype, DT_REL, DT_RELA );
                ret = 1;
                break;
            }
            break;

          IGNORE( DT_BIND_NOW )
          IGNORE( DT_INIT_ARRAY )
          IGNORE( DT_FINI_ARRAY )
          IGNORE( DT_INIT_ARRAYSZ )
          IGNORE( DT_FINI_ARRAYSZ )
          IGNORE( DT_RUNPATH )
          IGNORE( DT_FLAGS )
          // DT_ENCODING is numerically equal to DT_PREINIT_ARRAY
          // so we can't separate them
          case DT_ENCODING:
            DEBUG( DEBUG_ELF,
                   "ignoring DT_ENCODING or DT_PREINIT_ARRAY (0x%zx): 0x%zx",
                   (size_t) DT_ENCODING, (size_t) entry->d_un.d_val); \
            break;
          IGNORE( DT_PREINIT_ARRAYSZ )
          IGNORE( DT_NUM )

          // OS-specifics, in range DT_LOOS to DT_HIOS

          // Uses Dyn.d_un.d_val
          IGNORE( DT_GNU_PRELINKED )
          IGNORE( DT_GNU_CONFLICTSZ )
          IGNORE( DT_GNU_LIBLISTSZ )
          IGNORE( DT_CHECKSUM )
          IGNORE( DT_PLTPADSZ )
          IGNORE( DT_MOVEENT )
          IGNORE( DT_MOVESZ )
          IGNORE( DT_FEATURE_1 )
          IGNORE( DT_POSFLAG_1 )
          IGNORE( DT_SYMINSZ )
          IGNORE( DT_SYMINENT )

          // Uses Dyn.d_un.d_ptr
          IGNORE( DT_GNU_HASH )
          IGNORE( DT_TLSDESC_PLT )
          IGNORE( DT_TLSDESC_GOT )
          IGNORE( DT_GNU_CONFLICT )
          IGNORE( DT_GNU_LIBLIST )
          IGNORE( DT_CONFIG )
          IGNORE( DT_DEPAUDIT )
          IGNORE( DT_AUDIT )
          IGNORE( DT_PLTPAD )
          IGNORE( DT_MOVETAB )
          IGNORE( DT_SYMINFO )

          IGNORE( DT_VERSYM )

          IGNORE( DT_RELACOUNT )
          IGNORE( DT_RELCOUNT )

          // Sun-compatible
          IGNORE( DT_FLAGS_1 )
          IGNORE( DT_VERDEF )
          IGNORE( DT_VERDEFNUM )
          IGNORE( DT_VERNEED )
          IGNORE( DT_VERNEEDNUM )

          // Sun-compatible
          IGNORE( DT_AUXILIARY )
          IGNORE( DT_FILTER )

#undef IGNORE
#undef ALREADY_DID

          default:
            DEBUG( DEBUG_ELF, "Ignoring unknown dynamic section entry tag 0x%zx",
                   (size_t) entry->d_tag );
            break;
        }
    }

    return ret;
}

