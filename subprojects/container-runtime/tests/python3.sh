#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -e
set -u

n=0
fail=

set --

if [ -z "${TESTS_ONLY-}" ]; then
    set -- "$@" ./*.py
fi

set -- "$@" tests/depot/*.py

for script in "$@"; do
    n=$(( n + 1 ))
    if python3 "$script" --help >/dev/null; then
        echo "ok $n - $script --help succeeded with python3"
    else
        echo "not ok $n - $script --help failed with python3"
        fail=yes
    fi
done

echo "1..$n"

if [ -n "$fail" ]; then
    exit 1
fi

# vim:set sw=4 sts=4 et:
