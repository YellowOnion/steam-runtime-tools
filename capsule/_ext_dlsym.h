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

// Standard implementation of _ext_sym(), the implementation used
// when a library outside the capsule calls dlsym(). To override this
// implementation, copy this file to shim/capsule/_ext_dlsym.h in
// your shim library and modify as needed.
//
// This allows symbols inside the capsule to be found by dlopen calls from
// outside the capsule iff they are in one of the exported DSOs.
//
// This is useful in libGL shims as libGL has an ‘interesting’ history
// of symbols appearing and disappearing so its users often do a
// bizarre dlopen()/dlsym() dance instead of referring to a symbol
// directly (and we may be missing those symbols from our static
// export list even if the target libGL has them)
static void *_ext_dlsym (void *handle, const char *symbol)
{
    return capsule_external_dlsym( cap, handle, symbol );
}
