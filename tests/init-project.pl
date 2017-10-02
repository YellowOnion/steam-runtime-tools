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

my $CAPSULE_INIT_PROJECT_TOOL = $ENV{CAPSULE_INIT_PROJECT_TOOL};

if (! length $CAPSULE_INIT_PROJECT_TOOL) {
    $CAPSULE_INIT_PROJECT_TOOL = `pkg-config --variable=CAPSULE_INIT_PROJECT_TOOL capsule`;
    chomp $CAPSULE_INIT_PROJECT_TOOL;
}

my $CAPSULE_SYMBOLS_TOOL = $ENV{CAPSULE_SYMBOLS_TOOL};

if (! length $CAPSULE_SYMBOLS_TOOL) {
    $CAPSULE_SYMBOLS_TOOL = `pkg-config --variable=CAPSULE_SYMBOLS_TOOL capsule`;
    chomp $CAPSULE_SYMBOLS_TOOL;
}

run_ok([$CAPSULE_INIT_PROJECT_TOOL, 'libz.so.1', '/']);
run_ok([
        'sh', '-euc', 'cd "$1"; shift; time ./configure "$@"',
        'sh', "$test_tempdir/libz-proxy",
    ], '>&2');
run_ok(['sh', '-euc', 'time "$@"', 'sh',
        'make', '-C', "$test_tempdir/libz-proxy", 'V=1'], '>&2');
ok(-e "$test_tempdir/libz-proxy/libz.la");
ok(-e "$test_tempdir/libz-proxy/.libs/libz.so");
ok(-e "$test_tempdir/libz-proxy/.libs/libz.so.1");
ok(-e "$test_tempdir/libz-proxy/.libs/libz.so.1.0.0");

my $output;
run_ok([$CAPSULE_SYMBOLS_TOOL, 'libz.so.1'],
    '>', \$output);
my @symbols_wanted = sort(split /\n/, $output);
foreach my $sym (@symbols_wanted) {
    diag "- $sym";
}

sub uniq {
    my @unique;
    my %seen;
    foreach my $item (@_) {
        push @unique, $item unless $seen{$item};
        $seen{$item} = 1;
    }
    return @unique;
}

run_ok([$CAPSULE_SYMBOLS_TOOL, "$test_tempdir/libz-proxy/.libs/libz.so.1"],
    '>', \$output);
# TODO: For some reason each symbol in the proxy library shows up twice
my @symbols_produced = uniq(sort(split /\n/, $output));
foreach my $sym (@symbols_produced) {
    diag "- $sym";
}

is_deeply \@symbols_wanted, \@symbols_produced;

chdir '/';
done_testing;

# vim:set sw=4 sts=4 et:
