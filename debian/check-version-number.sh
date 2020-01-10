#!/bin/sh
# Copyright © 2020 Collabora Ltd.
# SPDX-License-Identifier: MIT
# (see debian/copyright)

set -eu

say () {
    echo "$0: $*" >&2
}

export LC_ALL=C.UTF-8
unset LANGUAGE

builddir="$1"
expected="$2"

"$builddir/bin/steam-runtime-system-info" --version > debian/version.txt

case "$2" in
    (*[-a-zA-Z+~]*)
        # Don't fail for 1.2.3~dfsg or 1.2.3+really1.0.1 or similar oddities
        say "SKIP: $2 contains non-digit non-dot"
        exit 0
        ;;
esac

if grep -E "^ *Version: ([\"']?)$2\\1\$" debian/version.txt
then
    say "OK"
else
    say "FAIL: $2 not found in s-r-s-i --version:"
    cat debian/version.txt
    say "If you have updated debian/changelog you must also update"
    say "meson.build and vice versa, unless you use '~' or '+' in"
    say "debian/changelog."
    exit 1
fi
