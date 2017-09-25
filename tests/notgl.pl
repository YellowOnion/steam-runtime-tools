#!/usr/bin/env perl

# notgl.pl — exercise libcapsule using an imitation of libGLX
#
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

use warnings;
use strict;

use File::Temp qw();
use IPC::Run qw(run);
use Test::More;

use FindBin;
use lib $FindBin::Bin;
use CapsuleTest;

skip_all_unless_bwrap;

my $capsule_prefix = File::Temp->newdir();
my $libs = '';
my $notgl_user = "$builddir/tests/notgl-user";
my $notgl_helper_user = "$builddir/tests/notgl-helper-user";
my $stdout;

if (exists $ENV{CAPSULE_TESTS_UNINSTALLED}) {
    diag 'Running uninstalled: yes';
    # We need to bypass libtool because it plays around with
    # LD_LIBRARY_PATH, and so do we.
    $libs = '/.libs';
    run_ok([
        "$builddir/libtool", qw(--mode=execute ls -1),
        "$builddir/tests/notgl-user"
    ], '>', \$notgl_user) or BAIL_OUT 'Cannot find notgl-user: $?';
    chomp $notgl_user;
    run_ok([
        "$builddir/libtool", qw(--mode=execute ls -1),
        "$builddir/tests/notgl-helper-user"
    ], '>', \$notgl_helper_user) or BAIL_OUT 'Cannot find notgl-helper-user: $?';
    chomp $notgl_helper_user;
}
else {
    diag 'Running uninstalled: no';
}

diag 'Without special measures:';
run_ok(['env', "LD_LIBRARY_PATH=$builddir/tests/lib$libs", $notgl_user],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: reference$/m);
like($stdout, qr/^NotGL helper implementation: container \(reference\)$/m);
like($stdout,
    qr/^notgl_extension_both: reference implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: \(not found\)$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);

diag 'Without special measures (linked to libhelper):';
run_ok(['env', "LD_LIBRARY_PATH=$builddir/tests/lib$libs",
        $notgl_helper_user],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: reference$/m);
like($stdout, qr/^NotGL helper implementation: container \(reference\)$/m);
like($stdout,
    qr/^notgl_extension_both: reference implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: \(not found\)$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);
like($stdout,
    qr/^NotGL helper implementation as seen by executable: container \(reference\)$/m);

diag 'With libcapsule loading red implementation:';
# We mount the "host system" on $capsule_prefix.
#
# In the "container", the shim libnotgl is picked up from tests/shim because
# it was prepended to the LD_LIBRARY_PATH. The helper library used by
# libnotgl, libhelper, is picked up from tests/lib as usual.
#
# In the "host system", there is a tmpfs over the build or installation
# directory to avoid tests/shim being found *again*, and the "red"
# implementations of libnotgl and libhelper is mounted over tests/lib.
run_ok([qw(bwrap
        --ro-bind / /
        --dev-bind /dev /dev
        --ro-bind /), $capsule_prefix,
        '--tmpfs', "$capsule_prefix$builddir",
        '--ro-bind', "$builddir/tests/red",
            "$capsule_prefix$builddir/tests/lib",
        '--setenv', 'CAPSULE_PREFIX', $capsule_prefix,
        '--setenv', 'LD_LIBRARY_PATH',
            "$builddir/tests/shim$libs:$builddir/tests/lib$libs",
        $notgl_user],
    '>', \$stdout);
diag_multiline $stdout;

# Functions from libnotgl get dispatched through the shim to the "red"
# implementation from the "host system". This mirrors functions from libGL
# being dispatched through the shim to the AMD implementation of libGL.
like($stdout, qr/^NotGL implementation: red$/m);

# When the "red" implementation of libnotgl calls functions from libhelper,
# implementation from the "host system". This mirrors functions from
# libstdc++ that are called by the host libGL ending up in the host libstdc++.
like($stdout, qr/^NotGL helper implementation: host \(red\)$/m);

# We can dlsym() for an implemementation of an extension that is part of
# the ABI of the shim and the reference implementation.
like($stdout,
    qr/^notgl_extension_both: red implementation of common extension$/m);

# We can also dlsym() for an implemementation of an extension that is only
# available in the "red" implementation.
like($stdout, qr/^notgl_extension_red: red-only extension$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);

diag 'With libcapsule loading green implementation:';
# Similar to the above, but now the host system is using the "green"
# implementation of libnotgl, mirroring the NVIDIA implementation of libGL.
run_ok([qw(bwrap
        --ro-bind / /
        --dev-bind /dev /dev
        --ro-bind /), $capsule_prefix,
        '--tmpfs', "$capsule_prefix$builddir",
        '--ro-bind', "$builddir/tests/green",
            "$capsule_prefix$builddir/tests/lib",
        '--setenv', 'CAPSULE_PREFIX', $capsule_prefix,
        '--setenv', 'LD_LIBRARY_PATH',
            "$builddir/tests/shim$libs:$builddir/tests/lib$libs",
        $notgl_helper_user],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: green$/m);
like($stdout, qr/^NotGL helper implementation: host \(green\)$/m);
like($stdout,
    qr/^notgl_extension_both: green implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: \(not found\)$/m);
like($stdout, qr/^notgl_extension_green: green-only extension$/m);

# Also, this program is linked directly to libhelper, mirroring a program
# that is linked directly to libstdc++ in the libGL case. It sees the
# container's libhelper, not the host's - even though libnotgl sees the
# host's libhelper when it looks up the same symbol. (This is the point
# of libcapsule.)
like($stdout,
    qr/^NotGL helper implementation as seen by executable: container \(reference\)$/m);

done_testing;

# vim:set sw=4 sts=4 et:
