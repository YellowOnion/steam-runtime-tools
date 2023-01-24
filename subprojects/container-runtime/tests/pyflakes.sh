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

if [ -n "${PYFLAKES-}" ]; then
    echo "# Using PYFLAKES=$PYFLAKES from environment"
elif command -v pyflakes3 >/dev/null; then
    PYFLAKES=pyflakes3
elif command -v pyflakes >/dev/null; then
    PYFLAKES=pyflakes
else
    PYFLAKES=false
fi

i=0
for script in "$@"; do
    if ! [ -e "$script" ]; then
        continue
    fi

    i=$((i + 1))
    if [ "${PYFLAKES}" = false ] || \
            [ -z "$(command -v "$PYFLAKES")" ]; then
        echo "ok $i - $script # SKIP pyflakes3 not found"
    elif "${PYFLAKES}" \
            "$script" >&2; then
        echo "ok $i - $script"
    else
        echo "not ok $i - $script # TODO $PYFLAKES issues reported"
    fi
done

if [ "$i" = 0 ]; then
    echo "1..0 # SKIP no Python scripts to test"
else
    echo "1..$i"
fi

# vim:set sw=4 sts=4 et:
