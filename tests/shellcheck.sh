#!/bin/sh
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2018-2020 Collabora Ltd
#
# This package is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this package.  If not, see
# <http://www.gnu.org/licenses/>.

set -e
set -u

if ! command -v shellcheck >/dev/null 2>&1; then
    echo "1..0 # SKIP shellcheck not available"
    exit 0
fi

if [ -z "${G_TEST_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    G_TEST_SRCDIR="${me%/*}"
fi

cd "$G_TEST_SRCDIR/.."

n=0
for shell_script in \
        ./ci/*.sh \
        ./bin/*.sh \
        ./pressure-vessel/pressure-vessel-locale-gen \
        ./pressure-vessel/pressure-vessel-unruntime \
        ./subprojects/container-runtime/common/_v2-entry-point \
        ./subprojects/container-runtime/debian/tests/depot \
        ./subprojects/container-runtime/runtimes/scout-on-soldier/_v2-entry-point \
        ./subprojects/container-runtime/runtimes/scout-on-soldier/scout-on-soldier-entry-point-v2 \
        ./subprojects/container-runtime/tests/*.sh \
        ./tests/*.sh \
        ./tests/*/*.sh \
        ; do
    n=$((n + 1))

    # Ignore SC2039,SC3043: we assume a Debian-style shell that has 'local'.
    if shellcheck --exclude=SC2039,SC3043 "$shell_script" >&2; then
        echo "ok $n - $shell_script"
    else
        echo "not ok $n # TODO - $shell_script"
    fi
done

echo "1..$n"

# vim:set sw=4 sts=4 et ft=sh:
