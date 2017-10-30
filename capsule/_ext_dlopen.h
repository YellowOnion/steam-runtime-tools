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

// Standard implementation of _ext_dlopen(), the implementation used
// when a library outside the capsule calls dlopen(). To override this
// implementation, copy this file to shim/capsule/_ext_dlopen.h in
// your shim library and modify as needed.
static void *_ext_dlopen (const char *filename, int flag)
{
    return capsule_external_dlopen( filename, flag );
}
