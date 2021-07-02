#!/usr/bin/perl

# Copyright © 2017 Collabora Ltd
# SPDX-License-Identifier: LGPL-2.1-or-later
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
my $host = "${test_tempdir}/host";
mkdir($host);

if (length $ENV{CAPSULE_TESTS_UNINSTALLED}) {
    my $path = $ENV{LD_LIBRARY_PATH};

    if (length $path) {
        $path = "$ENV{G_TEST_BUILDDIR}/.libs:$path";
    }
    else {
        $path = $ENV{G_TEST_BUILDDIR};
    }

    $ENV{LD_LIBRARY_PATH} = $path;

    TODO: {
        local $TODO = "capsule-symbols doesn't respect LD_LIBRARY_PATH";
        fail 'Re-enable this later';
    }
}
else {
    # We need a well-behaved library with a simple ABI to inspect. Let's use
    # libcapsule itself :-)
    my $output;
    run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
            qw(--dev-bind /dev /dev),
            $CAPSULE_SYMBOLS_TOOL, 'libcapsule.so.0', $host], '>', \$output);
    diag_multiline $output;
    return unless $output;

    like($output, qr{^capsule_init $}m, "capsule_init is an unversioned symbol");
    unlike($output, qr{^(?!capsule)}m, "all of libcapsule's ABI matches /^capsule/");

    # Try the same thing without a prefix
    run_ok([$CAPSULE_SYMBOLS_TOOL, 'libcapsule.so.0', '/'], '>', \$output);
    diag_multiline $output;

    like($output, qr{^capsule_init $}m, "capsule_init is an unversioned symbol");
    unlike($output, qr{^(?!capsule)}m, "all of libcapsule's ABI matches /^capsule/");
}

# How about versioned symbols?
my $output;
run_ok([$CAPSULE_SYMBOLS_TOOL, 'libxml2.so.2', '/'], '>', \$output);
diag_multiline $output;

like($output, qr{^xmlFree \@\@LIBXML2_2\.4\.30$}m,
    "xmlFree is a versioned symbol");
unlike($output, qr{^xmlFree (?!\@\@LIBXML2_2\.4\.30)}m,
    "xmlFree does not appear unversioned");

done_testing;

# vim:set sw=4 sts=4 et:
