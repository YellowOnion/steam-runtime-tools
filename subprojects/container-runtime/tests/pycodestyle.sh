#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -e
set -u

set --

if [ -z "${TESTS_ONLY-}" ]; then
    set -- "$@" ./*.py
fi

set -- "$@" tests/depot/*.py

if [ "${PYCODESTYLE:=pycodestyle}" = false ] || \
        [ -z "$(command -v "$PYCODESTYLE")" ]; then
    echo "1..0 # SKIP pycodestyle not found"
elif "${PYCODESTYLE}" "$@" >&2; then
    echo "1..1"
    echo "ok 1 - $PYCODESTYLE reported no issues"
else
    echo "1..1"
    echo "not ok 1 # TODO $PYCODESTYLE issues reported"
fi

# vim:set sw=4 sts=4 et:
