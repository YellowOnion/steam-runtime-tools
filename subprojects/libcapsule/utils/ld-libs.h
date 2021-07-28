// Copyright © 2017-2020 Collabora Ltd
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

#pragma once

#include <limits.h>
#include <link.h>

#include <libelf.h>
#include <gelf.h>

#include "ld-cache.h"

// we only handle up to this many library dependencies -
// yes, hardwired limits are bad but there's already enough complexity
// here - can revisit this decision if it ever becomes close to being
// an issue (shouldn't affect the api or abi):
#define DSO_LIMIT 256

/*
 * dso_needed_t:
 * @fd: @path opened for reading (only valid after ld_lib_open())
 * @name: The name we are looking for, either a bare SONAME or an
 *  absolute path (only valid after ld_lib_open())
 * @path: Absolute path to the library we need to load, including the
 *  %CAPSULE_PREFIX if required (must be set before ld_lib_open())
 * @requestors: `needed[i].requestors[j]` is 1 if `needed[j]`
 *  depends on `needed[i]`
 * @depcount: the number of libraries that this one depends on,
 *  such that `needed[j].depcount` is the number of nonzero
 *  `needed[i].requestors[j]` for each value of *i* where `needed[i]`
 *  has not yet been loaded
 * @dso: @fd opened for ELF inspection (only valid after ld_lib_open())
 *
 * A library that we need to load.
 */
typedef struct
{
    int   fd;
    char *name;
    char  path[PATH_MAX];
    int   requestors[DSO_LIMIT];
    int   depcount;
    Elf  *dso;
} dso_needed_t;

/**
 * ld_libs:
 * @ldcache: the runtime linker cache, or all-zeroes
 *  if ld_libs_load_cache() has not yet been called
 * @last_idx: private, used internally by the ld-libs code
 * @elf_class: the ELF class of the caller that initialized this
 * @elf_machine: the ELF machine type of the caller that initialized this
 * @prefix: the sysroot from which we will load encapsulated libraries
 * @exclude: (array zero-terminated=1) (nullable): libraries to ignore
 * @needed: private, used internally by the ld-libs code.
 *  needed[0] is the library we are looking for, and needed[1...]
 *  are the libraries in its recursive dependency tree.
 * @not_found: (transfer full): private, used internally by the ld-libs
 *  code. Each item is a copy of the name of a missing dependency.
 * @last_not_found: private, used internally by the ld-libs code.
 *  Number of items in @not_found used.
 * @debug: The debug flags passed to ld_libs_init()
 *
 * Data structure representing the libraries used in a capsule.
 */
typedef struct
{
    ld_cache ldcache;
    int last_idx;
    int elf_class;
    Elf64_Half elf_machine;
    struct { char path[PATH_MAX]; size_t len; } prefix;
    const char **exclude;
    dso_needed_t needed[DSO_LIMIT];
    char *not_found[DSO_LIMIT];
    int last_not_found;
    unsigned long debug;
} ld_libs;

int   ld_libs_init (ld_libs *ldlibs,
                    const char **exclude,
                    const char *prefix,
                    unsigned long dbg,
                    int *error,
                    char **message);

int   ld_libs_set_target        (ld_libs *ldlibs, const char *target,
                                 int *code, char **message);
int   ld_libs_find_dependencies (ld_libs *ldlibs, int *code, char **message);
void  ld_libs_finish            (ld_libs *ldlibs);
int   ld_libs_load_cache        (ld_libs *libs, int *code, char **message);

void *ld_libs_load (ld_libs *ldlibs, Lmid_t *namespace, int flag, int *error,
                    char **message);

