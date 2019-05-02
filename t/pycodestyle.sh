#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2018 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
# (See build-relocatable-install.py)

set -e
set -u

if [ -z "${G_TEST_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    srcdir="${me%/*}"
    G_TEST_SRCDIR="${srcdir%/*}"
fi

cd "$G_TEST_SRCDIR"

if [ "x${PYCODESTYLE:=pycodestyle}" = xfalse ] || \
        [ -z "$(command -v "$PYCODESTYLE")" ]; then
    echo "1..0 # SKIP pycodestyle not found"
    exit 0
fi

echo "1..1"

# Ignore E402: when using GObject-Introspection, not all imports
# can come first

if "${PYCODESTYLE}" \
    --ignore=E402 \
    "$G_TEST_SRCDIR"/*.py \
    "${G_TEST_SRCDIR}"/pressure-vessel-test-ui \
    "${G_TEST_SRCDIR}"/sysroot/*.py \
    >&2; then
    echo "ok 1 - $PYCODESTYLE reported no issues"
else
    echo "not ok 1 # TODO $PYCODESTYLE issues reported"
fi

# vim:set sw=4 sts=4 et:
