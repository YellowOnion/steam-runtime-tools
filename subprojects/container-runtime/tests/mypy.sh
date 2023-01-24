#!/bin/sh
# Copyright © 2016-2018 Simon McVittie
# Copyright © 2018-2023 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

set -e
set -u

export MYPYPATH="${PYTHONPATH:=$(pwd)}"
echo "TAP version 13"

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
            "$script" >&2; then
        echo "ok $i - $script"
    elif [ -n "${LINT_WARNINGS_ARE_ERRORS-}" ]; then
        echo "not ok $i - $script"
    else
        echo "not ok $i - $script # TODO mypy issues reported"
    fi
done

if [ "$i" = 0 ]; then
    echo "1..0 # SKIP no Python scripts to test"
else
    echo "1..$i"
fi

# vim:set sw=4 sts=4 et:
