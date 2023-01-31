#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -eu

n=0
fail=

if [ -z "$(command -v python3.5)" ]; then
    echo "1..0 # SKIP python3.5 not found"
    exit 0
fi

for script in \
    tests/depot/*.py \
; do
    n=$(( n + 1 ))
    if python3.5 -S "$script" --help >/dev/null; then
        echo "ok $n - $script --help succeeded with python3.5"
    else
        echo "not ok $n - $script --help failed with python3.5"
        fail=yes
    fi
done

echo "1..$n"

if [ -n "$fail" ]; then
    exit 1
fi

# vim:set sw=4 sts=4 et:
