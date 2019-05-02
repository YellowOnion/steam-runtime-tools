#!/bin/sh
#
# Copyright © 2019 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

set -eu
NULL=

DEB_BUILD_ARCH="$(dpkg-architecture -a"$1" -qDEB_BUILD_ARCH)"
DEB_HOST_ARCH="$(dpkg-architecture -a"$1" -qDEB_HOST_ARCH)"
DEB_BUILD_GNU_TYPE="$(dpkg-architecture -a"$1" -qDEB_BUILD_GNU_TYPE)"
DEB_HOST_GNU_TYPE="$(dpkg-architecture -a"$1" -qDEB_HOST_GNU_TYPE)"
DEB_HOST_MULTIARCH="$(dpkg-architecture -a"$1" -qDEB_HOST_MULTIARCH)"

case "${DEB_BUILD_ARCH}/${DEB_HOST_ARCH}" in
    (amd64/i386)
        export CC="cc -m32"
        ;;
esac

mkdir -p "build-relocatable/$1/libcapsule"

(
  cd "build-relocatable/$1/libcapsule"
  ../../../libcapsule/configure \
      --build="${DEB_BUILD_GNU_TYPE}" \
      --host="${DEB_HOST_GNU_TYPE}" \
      --enable-host-prefix="${DEB_HOST_MULTIARCH}-" \
      --enable-tools-rpath="/_ORIGIN_/__/lib/${DEB_HOST_MULTIARCH}" \
      --disable-shared \
      --disable-gtk-doc \
      --without-glib \
      ${NULL}
)

touch "$2"

# vim:set sw=2 sts=2 et:
