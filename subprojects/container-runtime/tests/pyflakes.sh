#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2018-2023 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -e
set -u

set --

if [ -z "${TESTS_ONLY-}" ]; then
    set -- "$@" ./*.py
fi

set -- "$@" tests/depot/*.py

echo "TAP version 13"

if [ "${PYFLAKES:=pyflakes3}" = false ] || \
        [ -z "$(command -v "$PYFLAKES")" ]; then
    echo "1..0 # SKIP pyflakes3 not found"
elif "${PYFLAKES}" "$@" >&2; then
    echo "1..1"
    echo "ok 1 - $PYFLAKES reported no issues"
else
    echo "1..1"
    echo "not ok 1 # TODO $PYFLAKES issues reported"
fi

# vim:set sw=4 sts=4 et:
