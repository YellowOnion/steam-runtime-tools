#!/usr/bin/perl

# Copyright © 2017 Collabora Ltd
#
# This file is part of libcapsule.
#
# libcapsule is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# libcapsule is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

use autodie;
use warnings;
use strict;

use File::Temp qw();
use IPC::Run qw(run);
use Test::More;

use FindBin;
use lib $FindBin::Bin;

use CapsuleTest;

skip_all_unless_bwrap;

my $test_tempdir = File::Temp->newdir();
diag "Working directory: $test_tempdir";
chdir $test_tempdir;

my $CAPSULE_INIT_PROJECT;

if (length $ENV{CAPSULE_TESTS_UNINSTALLED}) {
    $CAPSULE_INIT_PROJECT = "$ENV{G_TEST_SRCDIR}/data/capsule-init-project";
}
else {
    my $libexecdir = `pkg-config --variable=libexecdir capsule`;
    chomp $libexecdir;
    $CAPSULE_INIT_PROJECT = "$libexecdir/capsule-init-project";
}

run_ok([$CAPSULE_INIT_PROJECT, 'libz.so.1', '/']);
run_ok([
        'sh', '-euc', 'cd "$1"; shift; time ./configure "$@"',
        'sh', "$test_tempdir/libz-proxy",
    ], '>&2');
run_ok(['sh', '-euc', 'time "$@"', 'sh',
        'make', '-C', "$test_tempdir/libz-proxy", 'V=1'], '>&2');

chdir '/';
done_testing;

# vim:set sw=4 sts=4 et:
