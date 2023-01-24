#!/bin/sh
#
# Copyright © 2018-2023 Collabora Ltd
# SPDX-License-Identifier: MIT

set -e
set -u

if ! command -v shellcheck >/dev/null 2>&1; then
    echo "1..0 # SKIP shellcheck not available"
    exit 0
fi

if [ -z "${G_TEST_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    srcdir="${me%/*}"
    G_TEST_SRCDIR="${srcdir%/*}"
fi

cd "$G_TEST_SRCDIR"
echo "TAP version 13"

n=0
for shell_script in \
        common/_v2-entry-point \
        debian/tests/depot \
        runtimes/scout-on-soldier/_v2-entry-point \
        runtimes/scout-on-soldier/scout-on-soldier-entry-point-v2 \
        tests/*.sh \
        ; do
    n=$((n + 1))

    # Ignore SC2039: we assume a Debian-style shell that has 'local'.
    if shellcheck --exclude=SC2039 "$shell_script"; then
        echo "ok $n - $shell_script"
    elif [ -n "${LINT_WARNINGS_ARE_ERRORS-}" ]; then
        echo "not ok $n - $shell_script"
    else
        echo "not ok $n # TO""DO - $shell_script"
    fi
done

echo "1..$n"

# vim:set sw=4 sts=4 et ft=sh:
