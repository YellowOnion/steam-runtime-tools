#!/bin/bash

# Copyright © 2017 Collabora Ltd

# This file is part of libcapsule.

# libcapsule is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.

# libcapsule is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

set -eu -o pipefail

. "$(dirname "$0")"/libtest.sh

if ! bwrap --ro-bind / / true; then
    skip_all "1..0 # SKIP - cannot run bwrap"
fi

: ${G_TEST_SRCDIR:="$(cd "$(dirname "$0")"/..; pwd)"}
: ${G_TEST_BUILDDIR:="$(cd "$(dirname "$0")"/..; pwd)"}

echo "# Source or installation directory: $G_TEST_SRCDIR"
echo "# Build or installation directory: $G_TEST_BUILDDIR"

if [ -n "${CAPSULE_TESTS_UNINSTALLED:-}" ]; then
    echo "# Running uninstalled: yes"
    # We need to bypass libtool because it plays around with
    # LD_LIBRARY_PATH, and so do we.
    libs=/.libs
    notgl_user="$("$G_TEST_BUILDDIR/libtool" --mode=execute ls -1 "$G_TEST_BUILDDIR/tests/notgl-user")"
    notgl_helper_user="$("$G_TEST_BUILDDIR/libtool" --mode=execute ls -1 "$G_TEST_BUILDDIR/tests/notgl-helper-user")"
else
    echo "# Running uninstalled: no"
    libs=
    notgl_user="$G_TEST_BUILDDIR/tests/notgl-user"
    notgl_helper_user="$G_TEST_BUILDDIR/tests/notgl-helper-user"
fi

echo
echo "# Without special measures:"
run_verbose env LD_LIBRARY_PATH="$G_TEST_BUILDDIR/tests/lib$libs" "$notgl_user" > "$test_tempdir/output"
sed -e 's/^/#   /' "$test_tempdir/output"
exec_is \
    'sed -ne "s/^NotGL implementation: //p" $test_tempdir/output' \
    0 reference
exec_is \
    'sed -ne "s/^NotGL helper implementation: //p" $test_tempdir/output' \
    0 "container (reference)"
exec_is \
    'sed -ne "s/^notgl_extension_both: //p" $test_tempdir/output' \
    0 "reference implementation of common extension"
exec_is \
    'sed -ne "s/^notgl_extension_red: //p" $test_tempdir/output' \
    0 "(not found)"
exec_is \
    'sed -ne "s/^notgl_extension_green: //p" $test_tempdir/output' \
    0 "(not found)"

echo
run_verbose env LD_LIBRARY_PATH="$G_TEST_BUILDDIR/tests/lib$libs" "$notgl_helper_user" > "$test_tempdir/output"
sed -e 's/^/#   /' "$test_tempdir/output"
exec_is \
    'sed -ne "s/^NotGL implementation: //p" $test_tempdir/output' \
    0 reference
exec_is \
    'sed -ne "s/^NotGL helper implementation: //p" $test_tempdir/output' \
    0 "container (reference)"
exec_is \
    'sed -ne "s/^notgl_extension_both: //p" $test_tempdir/output' \
    0 "reference implementation of common extension"
exec_is \
    'sed -ne "s/^notgl_extension_red: //p" $test_tempdir/output' \
    0 "(not found)"
exec_is \
    'sed -ne "s/^NotGL helper implementation as seen by executable: //p" $test_tempdir/output' \
    0 "container (reference)"

echo
echo "# With libcapsule:"
# We mount the "host system" on $test_tempdir/host.
#
# In the "container", the shim libnotgl is picked up from tests/shim because
# it was prepended to the LD_LIBRARY_PATH. The helper library used by
# libnotgl, libhelper, is picked up from tests/lib as usual.
#
# In the "host system", there is a tmpfs over the build or installation
# directory to avoid tests/shim being found *again*, and the "red"
# implementations of libnotgl and libhelper is mounted over tests/lib.
run_verbose bwrap \
    --ro-bind / / \
    --dev-bind /dev /dev \
    --ro-bind / "$CAPSULE_PREFIX" \
    --tmpfs "$CAPSULE_PREFIX$G_TEST_BUILDDIR" \
    --ro-bind "$G_TEST_BUILDDIR/tests/red" "$CAPSULE_PREFIX$G_TEST_BUILDDIR/tests/lib" \
    --setenv LD_LIBRARY_PATH "$G_TEST_BUILDDIR/tests/shim$libs:$G_TEST_BUILDDIR/tests/lib$libs" \
    "$notgl_user" > "$test_tempdir/output"
sed -e 's/^/#   /' "$test_tempdir/output"
# Functions from libnotgl get dispatched through the shim to the "red"
# implementation from the "host system". This mirrors functions from libGL
# being dispatched through the shim to the AMD implementation of libGL.
exec_is \
    'sed -ne "s/^NotGL implementation: //p" $test_tempdir/output' \
    0 red
# When the "red" implementation of libnotgl calls functions from libhelper,
# implementation from the "host system". This mirrors functions from
# libstdc++ that are called by the host libGL ending up in the host libstdc++.
exec_is \
    'sed -ne "s/^NotGL helper implementation: //p" $test_tempdir/output' \
    0 "host (red)"
# We can dlsym() for an implemementation of an extension that is part of
# the ABI of the shim and the reference implementation.
exec_is \
    'sed -ne "s/^notgl_extension_both: //p" $test_tempdir/output' \
    0 "red implementation of common extension"
# We can also dlsym() for an implemementation of an extension that is only
# available in the "red" implementation.
exec_is \
    'sed -ne "s/^notgl_extension_red: //p" $test_tempdir/output' \
    0 "red-only extension"
exec_is \
    'sed -ne "s/^notgl_extension_green: //p" $test_tempdir/output' \
    0 "(not found)"

echo
# Similar to the above, but now the host system is using the "green"
# implementation of libnotgl, mirroring the NVIDIA implementation of libGL.
run_verbose bwrap \
    --ro-bind / / \
    --dev-bind /dev /dev \
    --ro-bind / "$CAPSULE_PREFIX" \
    --tmpfs "$CAPSULE_PREFIX$G_TEST_BUILDDIR" \
    --ro-bind "$G_TEST_BUILDDIR/tests/green" "$CAPSULE_PREFIX$G_TEST_BUILDDIR/tests/lib" \
    --setenv LD_LIBRARY_PATH "$G_TEST_BUILDDIR/tests/shim$libs:$G_TEST_BUILDDIR/tests/lib$libs" \
    "$notgl_helper_user" > "$test_tempdir/output"
sed -e 's/^/#   /' "$test_tempdir/output"
exec_is \
    'sed -ne "s/^NotGL implementation: //p" $test_tempdir/output' \
    0 green
exec_is \
    'sed -ne "s/^NotGL helper implementation: //p" $test_tempdir/output' \
    0 "host (green)"
exec_is \
    'sed -ne "s/^notgl_extension_both: //p" $test_tempdir/output' \
    0 "green implementation of common extension"
exec_is \
    'sed -ne "s/^notgl_extension_red: //p" $test_tempdir/output' \
    0 "(not found)"
exec_is \
    'sed -ne "s/^notgl_extension_green: //p" $test_tempdir/output' \
    0 "green-only extension"
# Also, this program is linked directly to libhelper, mirroring a program
# that is linked directly to libstdc++ in the libGL case. It sees the
# container's libhelper, not the host's - even though libnotgl sees the
# host's libhelper when it looks up the same symbol. (This is the point
# of libcapsule.)
exec_is \
    'sed -ne "s/^NotGL helper implementation as seen by executable: //p" $test_tempdir/output' \
    0 "container (reference)"

done_testing

# vim:set sw=4 sts=4 et:
