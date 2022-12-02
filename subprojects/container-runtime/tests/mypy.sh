#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -e
set -u

export MYPYPATH="${PYTHONPATH:=$(pwd)}"

set --

if [ -z "${TESTS_ONLY-}" ]; then
    set -- "$@" ./*.py
fi

set -- "$@" tests/depot/*.py

i=0
for script in "$@"; do
    i=$((i + 1))
    if [ "${MYPY:="$(command -v mypy || echo false)"}" = false ]; then
        echo "ok $i - $script # SKIP mypy not found"
    elif "${MYPY}" \
            --python-executable="${PYTHON:=python3}" \
            --follow-imports=skip \
            --ignore-missing-imports \
            "$script"; then
        echo "ok $i - $script"
    else
        echo "not ok $i - $script # TODO mypy issues reported"
    fi
done
echo "1..$i"

# vim:set sw=4 sts=4 et:
