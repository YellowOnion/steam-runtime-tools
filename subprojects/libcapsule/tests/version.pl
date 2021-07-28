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

my $LIBDIR = qr{/(?:usr/(?:\w+-(?:\w+-)?linux-gnu\w*/)?)?lib(?:32|64|x32)?(?:/\w+-linux-gnu\w*)?(?:/i686)?(?:/cmov)?};

skip_all_unless_bwrap;

my $test_tempdir = File::Temp->newdir();
diag "Working directory: $test_tempdir";
my $host = "${test_tempdir}/host";
mkdir($host);

my $output;
my ($root, $soname, $version, $path);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        qw(--dev-bind /dev /dev), $CAPSULE_VERSION_TOOL, 'libz.so.1',
        $host], '>', \$output);
diag("→ $output");
like($output, qr{^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)$});
$output =~ m{^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)$};
$root = $1;
$soname = $2;
$version = $3;
$path = $4;
is($root, $host);
is($soname, 'libz.so.1');
like($version, qr{^1\.[0-9]+\.[0-9]+$});
like($path, qr{^\Q$host\E($LIBDIR/libz\.so\.\Q$version\E)$});
$path =~ s/^\Q$host\E//;
ok(-e $path);

# Try the same thing without a prefix
run_ok([$CAPSULE_VERSION_TOOL, 'libz.so.1', '/'], '>', \$output);
diag("→ $output");
like($output, qr{^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)$});
$output =~ m{^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)$};
$root = $1;
$soname = $2;
$version = $3;
$path = $4;
is($root, '/');
is($soname, 'libz.so.1');
TODO: {
    local $TODO = 'returns 1 for some reason';
    like($version, qr{^1\.[0-9]+\.[0-9]+$});
}
like($path, qr{^($LIBDIR/libz\.so\.\Q$version\E)$});
ok(-e $1);

# A different way
run_ok([$CAPSULE_VERSION_TOOL, 'libz.so.1', ''], '>', \$output);
diag("→ $output");
like($output, qr{^(\S*)\s+(\S+)\s+(\S+)\s+(\S+)$});
$output =~ m{^(\S*)\s+(\S+)\s+(\S+)\s+(\S+)$};
$root = $1;
$soname = $2;
$version = $3;
$path = $4;
like($root, qr{/?});
is($soname, 'libz.so.1');
TODO: {
    local $TODO = 'returns 1 for some reason';
    like($version, qr{^1\.[0-9]+\.[0-9]+$});
}
like($path, qr{^($LIBDIR/libz\.so\.\Q$version\E)$});
ok(-e $path);

# Another different way
run_ok([$CAPSULE_VERSION_TOOL, 'libz.so.1'], '>', \$output);
diag("→ $output");
like($output, qr{^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)$});
$output =~ m{^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)$};
$root = $1;
$soname = $2;
$version = $3;
$path = $4;
# TODO: This should probably not try to print NULL
unlike($root, qr{^/.+$});   # actually it's (null)
is($soname, 'libz.so.1');
TODO: {
    local $TODO = 'returns 1 for some reason';
    like($version, qr{^1\.[0-9]+\.[0-9]+$});
}
like($path, qr{^($LIBDIR/libz\.so\.\Q$3\E)$});
ok(-e $path);

done_testing;

# vim:set sw=4 sts=4 et:
