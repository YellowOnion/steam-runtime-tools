# vim:set ft=sh sts=4 sw=4 et:

# Copyright © 2017 Collabora Ltd

# This file is part of libcapsule.

# libcapsule is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.

# libcapsule is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

test_tempdir="$(mktemp -d /tmp/libcapsule.XXXXXXXX)"
trap 'rm -fr --one-file-system $test_tempdir' EXIT
mkdir "$test_tempdir/host"
export CAPSULE_PREFIX="$test_tempdir/host"

test_num=0
any_failed=0

skip_all () {
    echo "1..0 # SKIP - $*"
    exit 0
}

pass () {
    test_num=$((test_num + 1))
    echo "ok $test_num - $*"
}

fail () {
    test_num=$((test_num + 1))
    echo "not ok $test_num - $*"
    any_failed=1
}

skip () {
    test_num=$((test_num + 1))
    echo "ok $test_num # SKIP - $*"
}

ok () {
    local condition="$1"
    shift

    if $condition; then
        pass "$*"
    else
        fail "$*"
    fi
}

is () {
    local got="$1"
    local expected="$2"
    shift 2

    if [ "x$got" = "x$expected" ]; then
        pass "$* ($got)"
    else
        echo "# Got: $got"
        echo "# Expected: $expected"
        fail "$* ($got != $expected)"
    fi
}

like () {
    local got="$1"
    local expected="$2"
    shift 2

    if [[ $got == $expected ]]; then
        pass "$* ($got matches $expected)"
    else
        echo "# Got: $got"
        echo "# Expected extglob: $expected"
        fail "$* ($got does not match $expected)"
    fi
}

isnt () {
    local got="$1"
    local unexpected="$2"
    shift 2

    if [ "x$got" != "x$unexpected" ]; then
        echo "# Got: $got"
        echo "# Expected: anything but $unexpected"
        fail "$* (expected anything but $unexpected)"
    else
        pass "$* ($got)"
    fi
}

shell_is () {
    local command="$1"
    local expected_status="$2"
    local expected="$3"
    local status=0
    shift 3

    got="$(eval "$command")" || status="$?"

    if [ "x$status" != "x$expected_status" ]; then
        fail "$* (status $status != $expected_status)"
    fi

    if [ "x$got" = "x$expected" ]; then
        pass "$* ($got)"
    else
        echo "# Got: $got"
        echo "# Expected: $expected"
        fail "$* ($got != $expected)"
    fi
}

run_verbose () {
    echo "# \$($*)..."
    "$@"
}

done_testing () {
    echo "# End of tests"
    echo "1..$test_num"
    exit $any_failed
}
