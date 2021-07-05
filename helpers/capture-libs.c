// Copyright © 2021 Collabora Ltd
// SPDX-License-Identifier: LGPL-2.1-or-later
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

#ifdef __GNUC__
// ELF code taken from glibc does a lot of pointer arithmetic assuming
// that (void * + integer) behaves like (char * + integer), which it does
// in gcc/clang
#pragma GCC diagnostic ignored "-Wpointer-arith"
// For fallback implementation of static_assert() */
#pragma GCC diagnostic ignored "-Wredundant-decls"
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wpointer-arith"
#pragma clang diagnostic ignored "-Wredundant-decls"
#endif

#include "subprojects/libcapsule/utils/ld-cache.c"
#include "subprojects/libcapsule/utils/ld-libs.c"
#include "subprojects/libcapsule/utils/library-cmp.c"
#include "subprojects/libcapsule/utils/tools.c"
#include "subprojects/libcapsule/utils/utils.c"

#include "subprojects/libcapsule/utils/capture-libs.c"
