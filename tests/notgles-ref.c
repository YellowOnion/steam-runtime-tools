// Copyright © 2017 Collabora Ltd

// notgles-ref — reference implementation of libnotgles, taking the
// role of the (possibly outdated) implementation of GLES in the container

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

#include "notgles.h"

#include <dlfcn.h>

#include "notgl-helper.h"

const char *
notgles_get_implementation( void )
{
    return "reference";
}

const char *
notgles_extension_both( void )
{
    return "reference implementation of common extension";
}

const char *
notgles_use_helper( void )
{
    return helper_get_implementation ();
}
