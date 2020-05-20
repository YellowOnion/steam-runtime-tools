#!/bin/sh

# Copyright © 2019 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

set -e
set -u

me="$0"
me="$(readlink -f "$0")"
here="${me%/*}"
me="${me##*/}"

# TAP test helper functions inspired by Perl Test::More.

test_num=0
any_failed=

skip_all () {
    echo "1..0 # SKIP $*"
    exit 0
}

diag () {
    echo "# $*"
}

ok () {
    test_num=$(( test_num + 1 ))
    echo "ok $test_num - $*"
}

skip () {
    test_num=$(( test_num + 1 ))
    echo "ok $test_num # SKIP $*"
}

not_ok () {
    test_num=$(( test_num + 1 ))
    any_failed=yes
    echo "not ok $test_num - $*"
}

done_testing () {
    echo "1..$test_num"
    if [ -n "$any_failed" ]; then
        exit 1
    fi
    exit 0
}

if ! tmpdir="$(mktemp -d)"; then
    skip_all "Unable to create a temporary directory"
fi

if [ -z "${G_TEST_SRCDIR-}" ]; then
    G_TEST_SRCDIR="${here}"
fi

if [ -z "${G_TEST_BUILDDIR-}" ]; then
    G_TEST_BUILDDIR="${here}"
fi

if [ -n "${PRESSURE_VESSEL_UNINSTALLED-}" ]; then
    PATH="${G_TEST_BUILDDIR}/..:${G_TEST_SRCDIR}/..:${PATH}"
fi

locale_gen="pressure-vessel-locale-gen"
try_setlocale="pressure-vessel-try-setlocale"

mkdir "$tmpdir/1"

one_missing=

# Locales used in the test below, chosen to include several that you
# probably don't have all of.
for locale in \
    cs_CZ \
    cy_GB.UTF-8 \
    en_AU.UTF-8 \
    en_DK \
    en_GB.ISO-8859-15 \
    en_US.UTF-8 \
    hu_HU.UTF-8 \
    it_IT@euro \
    mn_MN \
    ms_MY \
    nb_NO \
    nan_TW@latin \
    pl_PL \
    ta_IN \
    ti_ER \
; do
    if "$try_setlocale" "$locale" >/dev/null; then
        diag "You already have $locale"
    else
        diag "You do not already have $locale"
        one_missing=yes
        break
    fi
done

if [ -z "$one_missing" ]; then
    skip "You have all the test locales, cannot test"
else
    failed=

    # Deliberately setting environment variables in subshell:
    # shellcheck disable=SC2030
    (
        cd "$tmpdir/1"
        export LC_CTYPE=cy_GB.utf8
        export LC_COLLATE=cs_CZ
        export LC_IDENTIFICATION=it_IT@euro
        export LC_MEASUREMENT=mn_MN
        export LC_MESSAGES=en_GB.ISO-8859-15
        export LC_MONETARY=ms_MY
        export LC_NUMERIC=nb_NO
        export LC_NAME=nan_TW@latin
        export LC_PAPER=pl_PL
        export LC_TELEPHONE=ta_IN
        export LC_TIME=ti_ER

        export HOST_LC_ALL=hu_HU.UTF-8
        export LANG=en_DK
        export LC_ALL=en_AU.UTF-8
        "$locale_gen"
    ) || failed="$?"

    if [ "$failed" = 72 ]; then
        ok "ran $locale_gen, got status 72"
    elif [ -n "$failed" ]; then
        not_ok "failed to run $locale_gen"
    else
        not_ok "$locale_gen did not report missing locales"
    fi

    for locale in \
        cs_CZ \
        cy_GB.UTF-8 \
        cy_GB.utf8 \
        en_AU.UTF-8 \
        en_DK \
        en_GB.ISO-8859-15 \
        en_US.UTF-8 \
        hu_HU.UTF-8 \
        it_IT@euro \
        mn_MN \
        ms_MY \
        nb_NO \
        nan_TW@latin \
        pl_PL \
        ta_IN \
        ti_ER \
    ; do
        if [ -e "$tmpdir/1/$locale/LC_IDENTIFICATION" ]; then
            ok "$tmpdir/1/$locale/LC_IDENTIFICATION exists"
        else
            not_ok "$tmpdir/1/$locale/LC_IDENTIFICATION not generated"
        fi
    done
fi

mkdir "$tmpdir/2"

if "$try_setlocale" "en_US.UTF-8" >/dev/null; then
    failed=

    # Deliberately setting environment variables in subshell:
    # shellcheck disable=SC2031
    (
        cd "$tmpdir/2"
        unset LC_CTYPE
        unset LC_COLLATE
        unset LC_IDENTIFICATION
        unset LC_MEASUREMENT
        unset LC_MESSAGES
        unset LC_MONETARY
        unset LC_NUMERIC
        unset LC_NAME
        unset LC_PAPER
        unset LC_TELEPHONE
        unset LC_TIME

        export HOST_LC_ALL=en_US.UTF-8
        export LANG=en_US.UTF-8
        export LC_ALL=en_US.UTF-8
        "$locale_gen"
    ) || failed=yes

    if [ -n "$failed" ]; then
        not_ok "failed to run $locale_gen"
    else
        ok "ran $locale_gen"
    fi

    failed=

    for x in "$tmpdir/2"/*; do
        if [ -e "$x" ]; then
            not_ok "$locale_gen generated $x but should not have generated anything"
            failed=yes
        fi
    done

    if [ -z "$failed" ]; then
        ok "$locale_gen did not redundantly generate en_US.UTF-8"
    fi
else
    skip "Cannot test with existing en_US.UTF-8 locale"
fi

rm -fr "$tmpdir"

done_testing

# vim:set sw=4 sts=4 et:
