// Copyright © 2017 Collabora Ltd

// notgles-green — one of two implementations of libnotgles, taking the
// role of the NVIDIA implementation of GLES

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
    return "green";
}

const char *
notgles_extension_both( void )
{
    return "green implementation of common extension";
}

const char *
notgles_extension_green( void )
{
    return "green-only extension";
}

const char *
notgles_use_helper( void )
{
    return helper_get_implementation();
}
