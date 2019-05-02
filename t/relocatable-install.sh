#!/bin/sh
set -eu

if [ -z "${G_TEST_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    srcdir="${me%/*}"
    G_TEST_SRCDIR="${srcdir%/*}"
fi

: "${G_TEST_BUILDDIR:=.}"

n=0
failed=

EXES="
bwrap
"

MULTIARCH="
capsule-capture-libs
capsule-symbols
"

SCRIPTS="
pressure-vessel-wrap
pressure-vessel-unruntime
"

relocatable_install="${G_TEST_BUILDDIR}/relocatable-install"

if ! [ -e "${relocatable_install}" ]; then
    echo "1..0 # SKIP relocatable-install not in expected location"
    exit 0
fi

for exe in $EXES; do
    n=$(( n + 1 ))

    if "${relocatable_install}/bin/$exe" --help >&2; then
        echo "ok $n - $exe --help"
    else
        echo "not ok $n - $exe --help"
        failed=yes
    fi
done

for basename in $MULTIARCH; do
    for pair in \
        x86_64-linux-gnu:/lib64/ld-linux-x86-64.so.2 \
        i386-linux-gnu:/lib/ld-linux.so.2 \
    ; do
        ld_so="${pair#*:}"
        multiarch="${pair%:*}"
        exe="${multiarch}-${basename}"

        n=$(( n + 1 ))

        if [ -x "${relocatable_install}/bin/$exe" ]; then
            echo "ok $n - $exe exists and is executable"
        else
            echo "not ok $n - $exe not executable"
            failed=yes
        fi

        n=$(( n + 1 ))

        if ! [ -x "$ld_so" ]; then
            echo "ok $n - $exe # SKIP: $ld_so not found"
        elif [ "$basename" = "capsule-symbols" ]; then
            echo "ok $n - $exe # SKIP: capsule-symbols has no --help yet"
        elif "${relocatable_install}/bin/$exe" --help >&2; then
            echo "ok $n - $exe --help"
        else
            echo "not ok $n - $exe --help"
            failed=yes
        fi
    done
done

for exe in $SCRIPTS; do
    n=$(( n + 1 ))

    if ! [ -x /bin/bash ]; then
        echo "ok $n - $exe # SKIP Cannot run a bash script without bash"
    elif "${relocatable_install}/bin/$exe" --help >&2; then
        echo "ok $n - $exe --help"
    else
        echo "not ok $n - $exe --help"
        failed=yes
    fi
done

echo "1..$n"

if [ -n "$failed" ]; then
    exit 1
fi

# vim:set sw=4 sts=4 et:
