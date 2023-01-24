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

if [ "${PYCODESTYLE:=pycodestyle}" = false ] || \
        [ -z "$(command -v "$PYCODESTYLE")" ]; then
    echo "1..0 # SKIP pycodestyle not found"
    exit 0
fi

i=0
for script in "$@"; do
    if ! [ -e "$script" ]; then
        continue
    fi

    i=$((i + 1))
    if [ "${PYCODESTYLE}" = false ] || \
            [ -z "$(command -v "$PYCODESTYLE")" ]; then
        echo "ok $i - $script # SKIP pycodestyle not found"
    elif "${PYCODESTYLE}" \
            "$script" >&2; then
        echo "ok $i - $script"
    else
        echo "not ok $i - $script # TODO pycodestyle issues reported"
    fi
done

if [ "$i" = 0 ]; then
    echo "1..0 # SKIP no Python scripts to test"
else
    echo "1..$i"
fi

# vim:set sw=4 sts=4 et:
