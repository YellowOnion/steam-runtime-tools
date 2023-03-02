#!/bin/sh
# Copyright © 2022-2023 Collabora Ltd
# SPDX-License-Identifier: MIT

set -eu

if ! command -v reuse >/dev/null 2>&1; then
    echo "1..0 # SKIP reuse not available"
    exit 0
fi

if [ -z "${G_TEST_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    G_TEST_SRCDIR="${me%/*}"
fi

cd "$G_TEST_SRCDIR/.."

echo "TAP version 13"
echo "1..1"

if reuse lint >&2; then
    echo "ok 1"
elif [ -n "${LINT_WARNINGS_ARE_ERRORS-}" ]; then
    echo "not ok 1"
else
    echo "not ok 1 # TO""DO reuse lint failed"
fi
