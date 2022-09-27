#!/bin/sh
# Copyright © 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

# Run tests in the LD_LIBRARY_PATH Steam Runtime environment.
#
# Usage:
#
# On development machine:
# build-aux/many-builds.py setup
# build-aux/many-builds.py build
# build-aux/many-builds.py install
# rsync -avzP --delete \
#     _build/scout-DESTDIR/usr/libexec/installed-tests/steam-runtime-tools-0 \
#     test-machine:tmp/steam-runtime-tools-tests/
#
# On test machine:
# ~/.steam/root/ubuntu12_32/steam-runtime/setup.sh
# ~/.steam/root/ubuntu12_32/steam-runtime/run.sh -- \
#     ~/tmp/steam-runtime-tools-tests/run-in-ldlp-runtime.sh

set -eu

me="$(realpath "$0")"
here="${me%/*}"
me="${me##*/}"

verbose=

usage () {
    local code="$1"
    shift

    if [ "$code" -ne 0 ]; then
        exec >&2
    fi

    echo "Usage:"
    echo ".../steam-runtime/run.sh -- $me [OPTIONS]"
    echo
    echo "Run automated tests in a Steam Runtime environment."
    echo
    echo "Options"
    echo "--verbose         Be more verbose."

    exit "${code}"
}

log () {
    printf '%s\n' "${me}[$$]: $*" >&2 || :
}

verbose () {
    if [ -n "$verbose" ]; then
        log "$@"
    fi
}

main () {
    local command
    local failures=0
    local getopt_temp
    local name
    local tee_pid
    local tempdir
    local testdir=0
    local tests=0
    local verbose=

    export G_TEST_BUILDDIR="$here"
    export G_TEST_SRCDIR="$here"
    unset SRT_HELPERS_PATH
    unset SRT_TEST_MULTIARCH
    unset SRT_TEST_TOP_BUILDDIR
    unset SRT_TEST_TOP_SRCDIR
    unset SRT_TEST_UNINSTALLED

    case "${STEAM_RUNTIME-}" in
        (/*)
            ;;

        (*)
            echo "1..0 # SKIP Must be run in a Steam Runtime environment"
            return 0
            ;;
    esac

    getopt_temp="help"
    getopt_temp="$(getopt -o '' --long "$getopt_temp" -n "$me" -- "$@")"
    eval "set -- $getopt_temp"
    unset getopt_temp

    while [ "$#" -gt 0 ]; do
        case "$1" in
            (--help)
                usage 0
                # not reached
                ;;

            (--verbose)
                verbose=1
                ;;

            (--)
                shift
                break
                ;;

            (-*)
                log "Unknown option: $1"
                usage 125   # EX_USAGE from sysexits.h
                # not reached
                ;;

            (*)
                break
                ;;
        esac
    done

    if [ "$#" -gt 0 ]; then
        usage 125
    fi

    set --

    while read -r line; do
        case "$line" in
            ('#'*)
                continue
                ;;
        esac
        set -- "$@" "$line"
    done < "${G_TEST_BUILDDIR}/run-in-ldlp-runtime.txt"

    tempdir="$(mktemp -d)"

    while [ "$#" -gt 0 ]; do
        tests=$((tests + 1))
        name="${1%%:*}"
        command="${1#*:}"
        shift

        testdir="$tempdir/$name"
        mkdir "$testdir"
        mkfifo "$testdir/fifo"

        echo "# $tests $name: \"$command\"..."

        tee "$testdir/output.txt" < "$testdir/fifo" | sed -e 's/^/#   /' &
        tee_pid="$!"

        if ( cd "$testdir" && eval "$command" > "$testdir/fifo" 2>&1 ); then
            echo "ok $tests - $name"
        else
            echo "not ok $tests - $name exit status $? from: $command"
            echo "# Log: $testdir/output.txt"
            failures=$((failures + 1))
        fi

        wait "$tee_pid"
    done

    if [ "$failures" -gt 0 ]; then
        echo "# FAILED $failures/$tests"
        echo "# See $tempdir for logs"
        echo "1..$tests"
        return 1
    else
        echo "# SUCCESS $tests/$tests"
        echo "1..$tests"
        rm -fr "$tempdir"
    fi
}

main "$@"
