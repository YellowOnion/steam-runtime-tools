#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -e
set -u

if [ "x${PYFLAKES:=pyflakes3}" = xfalse ] || \
        [ -z "$(command -v "$PYFLAKES")" ]; then
    echo "1..0 # SKIP pyflakes3 not found"
elif "${PYFLAKES}" \
    tests/depot/*.py \
    >&2; then
    echo "1..1"
    echo "ok 1 - $PYFLAKES reported no issues"
else
    echo "1..1"
    echo "not ok 1 # TODO $PYFLAKES issues reported"
fi

# vim:set sw=4 sts=4 et:
