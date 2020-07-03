// Copyright © 2020 Collabora Ltd

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

#include "library-cmp.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "utils.h"

/*
 * library_cmp_by_name:
 * @soname: The library we are interested in, used in debug logging
 * @left_path: The path to the "left" instance of the library
 * @left_from: Arbitrary description of the container/provider/sysroot
 *  where we found @left_path, used in debug logging
 * @right_path: The path to the "right" instance of the library
 * @right_from: Arbitrary description of the container/provider/sysroot
 *  where we found @right_path, used in debug logging
 *
 * Attempt to determine whether @left_path is older than, newer than or
 * the same as than @right_path by inspecting their filenames.
 *
 * Return a strcmp-style result: negative if left < right,
 * positive if left > right, zero if left == right or if left and right
 * are non-comparable.
 */
int
library_cmp_by_name( const char *soname,
                     const char *left_path,
                     const char *left_from,
                     const char *right_path,
                     const char *right_from )
{
    _capsule_autofree char *left_realpath = NULL;
    _capsule_autofree char *right_realpath = NULL;
    const char *left_basename;
    const char *right_basename;

    // This might look redundant when our arguments come from the ld_libs,
    // but resolve_symlink_prefixed() doesn't chase symlinks if the
    // prefix is '/' or empty.
    left_realpath = realpath( left_path, NULL );
    right_realpath = realpath( right_path, NULL );
    left_basename = _capsule_basename( left_realpath );
    right_basename = _capsule_basename( right_realpath );

    DEBUG( DEBUG_TOOL,
           "Comparing %s \"%s\" from \"%s\" with "
           "\"%s\" from \"%s\"",
           soname, left_basename, left_from, right_basename, right_from );

    if( strcmp( left_basename, right_basename ) == 0 )
    {
        DEBUG( DEBUG_TOOL,
               "Name of %s \"%s\" from \"%s\" compares the same as "
               "\"%s\" from \"%s\"",
               soname, left_basename, left_from, right_basename, right_from );
        return 0;
    }

    if( strcmp( soname, left_basename ) == 0 )
    {
        /* In some distributions (Debian, Ubuntu, Manjaro)
         * libgcc_s.so.1 is a plain file, not a symlink to a
         * version-suffixed version. We cannot know just from the name
         * whether that's older or newer, so assume equal. The caller is
         * responsible for figuring out which one to prefer. */
        DEBUG( DEBUG_TOOL,
               "Unversioned %s \"%s\" from \"%s\" cannot be compared with "
               "\"%s\" from \"%s\"",
               soname, left_basename, left_from,
               right_basename, right_from );
        return 0;
    }

    if( strcmp( soname, right_basename ) == 0 )
    {
        /* The same, but the other way round */
        DEBUG( DEBUG_TOOL,
               "%s \"%s\" from \"%s\" cannot be compared with "
               "unversioned \"%s\" from \"%s\"",
               soname, left_basename, left_from,
               right_basename, right_from );
        return 0;
    }

    return ( strverscmp( left_basename, right_basename ) );
}
