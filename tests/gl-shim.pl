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

skip_all_unless_nm;

my $test_tempdir = File::Temp->newdir();
diag "Working directory: $test_tempdir";
chdir $test_tempdir;

my ($output, $ignored, $error);

my @libcapsule_environment;
if (length $ENV{CAPSULE_TESTS_UNINSTALLED}) {
    # Make sure the shim can load the just-built libcapsule
    push @libcapsule_environment, "LD_LIBRARY_PATH=$ENV{G_TEST_BUILDDIR}/.libs";
}

my $examples = "$srcdir/examples";

my @sonames = qw(libGL.so.1 libX11.so.6 libXext.so.6 libXi.so.6
    libxcb-dri2.so.0 libxcb-glx.so.0 libxcb-present.so.0 libxcb-sync.so.1
    libxcb.so.1);

# Don't require running capsule-version - just use the major versions
sub use_major_version {
    my $soname = shift;
    if ($soname =~ m/.*\.([0-9]+)$/) {
        return "$soname/$1";
    }
    else {
        die "$soname doesn't end with a major version";
    }
}

run_ok([$CAPSULE_INIT_PROJECT_TOOL,
        '--runtime-tree=/run/host',
        '--set-version=1.0.0',
        "--symbols-from=$examples/shim",
        map { use_major_version($_) } @sonames,
    ]);
run_ok([
        'sh', '-euc', 'cd "$1"; shift; ./configure "$@"',
        'sh', "$test_tempdir/libGL-proxy",
    ], '>&2');
run_ok(['make', '-C', "$test_tempdir/libGL-proxy", 'V=1'], '>&2');

foreach my $soname (@sonames) {
    my $basename = $soname;
    $basename =~ s/\.so\..*$//;

    ok(-e "$test_tempdir/libGL-proxy/$basename.la");
    ok(-e "$test_tempdir/libGL-proxy/.libs/$basename.so");
    ok(-e "$test_tempdir/libGL-proxy/.libs/$soname");
    ok(-e "$test_tempdir/libGL-proxy/.libs/$soname.0.0");

    my @symbols_wanted;
    open my $fh, "$examples/shim/$soname.symbols";
    while (defined(my $line = <$fh>)) {
        chomp $line;
        push @symbols_wanted, $line;
    }

    my @symbols_produced =
        get_symbols_with_nm("$test_tempdir/libGL-proxy/.libs/$soname");
    is_deeply \@symbols_wanted, [grep {! m/^capsule_meta $/} @symbols_produced];
}

chdir '/';
done_testing;

# vim:set sw=4 sts=4 et:
