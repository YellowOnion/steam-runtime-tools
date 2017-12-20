// Copyright © 2017 Collabora Ltd
//
// This file is part of libcapsule.
//
// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

// Standard implementation of _int_dlopen(), the implementation used
// when a library inside the capsule calls dlopen(). To override this
// implementation, copy this file to shim/capsule/_int_dlopen.h in
// your shim library and modify as needed.
static void *
_int_dlopen (const char *filename, int flag)
{
    if( flag & RTLD_GLOBAL )
    {
        fprintf( stderr, "Warning: libcapsule dlopen wrapper cannot pass "
                         "RTLD_GLOBAL to underlying dlmopen(%s...) call\n",
                 filename );
        flag = (flag & ~RTLD_GLOBAL) & 0xfffff;
    }
    return capsule_shim_dlopen( cap, filename, flag );
}

// if the libc instances aren't unified (ie > 1 libc) then
// we must try to dispatch the to-be-freed pointer to the one
// that actually allocated it.
// This is far from foolproof:
static void
_wrapped_free (void *ptr)
{
    if (ptr)
        capsule_shim_free( cap, ptr );
}
