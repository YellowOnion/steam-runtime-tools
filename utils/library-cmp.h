// Copyright © 2020 Collabora Ltd
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

#include <stdbool.h>
#include <stdio.h>

typedef struct _library_details library_details;

/*
 * library_cmp_function:
 * @details: The details about the library we are interested in and how
 *  to compare it
 * @container_path: The path to the library in the container
 * @container_root: The path to the top-level directory of the container
 * @provider_path: The path to the library in the provider
 * @provider_root: The path to the top-level directory of the provider
 *
 * Compare two libraries, returning a result with the same convention
 * as strcmp(): informally, `cmp(a, b) < 0` if `a < b`, and the same
 * for `>`.
 *
 * Returns: Negative if the container version appears newer, zero if they
 *  appear the same or we cannot tell, or positive if the provider version
 *  appears newer.
 */
typedef int ( *library_cmp_function ) ( const library_details *details,
                                        const char *container_path,
                                        const char *container_root,
                                        const char *provider_path,
                                        const char *provider_root );

/*
 * library_details:
 * @name: The name we look for the library under, normally a SONAME
 * @comparators: Functions to use to compare versions, or %NULL for
 *  default behaviour
 * @public_symbol_versions: An array of patterns to discriminate public
 *  versions from private versions
 * @public_symbols: An array of patterns to discriminate public
 *  symbols from private symbols
 */
struct _library_details
{
    char *name;
    library_cmp_function *comparators;
    char **public_symbol_versions;
    char **public_symbols;
};

library_cmp_function *library_cmp_list_from_string( const char *spec,
                                                    const char *delimiters,
                                                    int *code,
                                                    char **message );
int library_cmp_list_iterate( const library_details *details,
                              const char *container_path,
                              const char *container_root,
                              const char *provider_path,
                              const char *provider_root );

/*
 * library_knowledge:
 * @tree: (element-type library_details): A tsearch(3) tree
 *
 * Details of all known libraries.
 */
typedef struct
{
    /*< private >*/
    void *tree;
} library_knowledge;

#define LIBRARY_KNOWLEDGE_INIT { NULL }

bool library_knowledge_load_from_stream( library_knowledge *self,
                                         FILE *stream, const char *name,
                                         int *code, char **message );
const library_details *library_knowledge_lookup( const library_knowledge *self,
                                                 const char *library );
void library_knowledge_clear( library_knowledge *self );
