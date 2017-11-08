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

use autodie;
use warnings;
use strict;

use Cwd qw(realpath);
use File::Temp qw();
use IPC::Run qw(run);
use Test::More;

use FindBin;
use lib $FindBin::Bin;

use CapsuleTest;

skip_all_unless_bwrap;

my $temp = File::Temp->newdir();
my $capsule_prefix = "$temp/prefix";
mkdir $capsule_prefix;

my $libs = '';
my $notgl_user = "$builddir/tests/notgl-user";
my $notgl_helper_user = "$builddir/tests/notgl-helper-user";
my $notgl_dlopener = "$builddir/tests/notgl-dlopener";
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
    run_ok([
        "$builddir/libtool", qw(--mode=execute ls -1),
        "$builddir/tests/notgl-dlopener"
    ], '>', \$notgl_dlopener) or BAIL_OUT 'Cannot find notgl-dlopener: $?';
    chomp $notgl_dlopener;
}
else {
    diag 'Running uninstalled: no';
}

diag 'Without special measures:';
run_ok(['env',
        'LD_LIBRARY_PATH='.join(':', realpath("$builddir/tests/lib$libs"),
            realpath("$builddir/tests/helper$libs")),
        $notgl_user],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: reference$/m);
like($stdout, qr/^NotGL helper implementation: container \(reference\)$/m);
like($stdout,
    qr/^notgl_extension_both: reference implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: \(not found\)$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);

diag 'Without special measures (linked to libhelper):';
run_ok(['env',
        'LD_LIBRARY_PATH='.join(':', realpath("$builddir/tests/lib$libs"),
            realpath("$builddir/tests/helper$libs")),
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
like($stdout, qr/^NotGLES implementation: reference$/m);
like($stdout, qr/^NotGLES helper implementation: container \(reference\)$/m);
like($stdout,
    qr/^notgles_extension_both: reference implementation of common extension$/m);
like($stdout, qr/^notgles_extension_red: \(not found\)$/m);
like($stdout, qr/^notgles_extension_green: \(not found\)$/m);
like($stdout,
    qr/^NotGL helper implementation as seen by executable: container \(reference\)$/m);

diag 'Without special measures (using dlopen):';
run_ok(['env',
        'LD_LIBRARY_PATH='.join(':', realpath("$builddir/tests/lib$libs"),
            realpath("$builddir/tests/helper$libs")),
        $notgl_dlopener],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: reference$/m);
like($stdout, qr/^NotGL helper implementation: container \(reference\)$/m);
like($stdout,
    qr/^notgl_extension_both: reference implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: \(not found\)$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);
like($stdout, qr/^NotGLES implementation: reference$/m);
like($stdout, qr/^NotGLES helper implementation: container \(reference\)$/m);
like($stdout,
    qr/^notgles_extension_both: reference implementation of common extension$/m);
like($stdout, qr/^notgles_extension_red: \(not found\)$/m);
like($stdout, qr/^notgles_extension_green: \(not found\)$/m);

diag 'With libcapsule loading red implementation:';
# We mount the "host system" on $capsule_prefix.
#
# In the "container", the shim libnotgl is picked up from tests/shim because
# it was prepended to the LD_LIBRARY_PATH. The helper library used by
# libnotgl, libhelper, is picked up from tests/helper as usual, and
# we mount a tmpfs over tests/lib to avoid libnotgl being picked up
# from the RPATH (in older toolchains that produce those instead
# of RUNPATH, and cannot be overridden by LD_LIBRARY_PATH):
#
# emulated container on /
# $builddir/
#   tests/
#     helper/ (contains reference libhelper)
#     lib/ (empty)
#     shim/ (contains shim libnotgl)
#
# In the "host system", there is a tmpfs over the build or installation
# directory to avoid tests/shim being found *again*, and the "red"
# implementations of libnotgl and libhelper are mounted over tests/lib
# and tests/helper:
#
# emulated host filesystem on $capsule_prefix
# $builddir/
#   tests/
#     helper/ (contains red libnotgl and libhelper)
#     lib/ (contains red libnotgl and libhelper)
#     shim/ (does not exist)
run_ok([qw(bwrap
        --ro-bind / /
        --dev-bind /dev /dev
        --ro-bind /), $capsule_prefix,
        '--tmpfs', realpath("$builddir/tests/lib$libs"),
        '--tmpfs', $capsule_prefix.realpath($builddir),
        '--ro-bind', realpath("$builddir/tests/red"),
            $capsule_prefix.realpath("$builddir/tests/helper"),
        '--ro-bind', realpath("$builddir/tests/red"),
            $capsule_prefix.realpath("$builddir/tests/lib"),
        '--setenv', 'CAPSULE_PREFIX', $capsule_prefix,
        '--setenv', 'LD_LIBRARY_PATH', join(':',
            realpath("$builddir/tests/shim$libs"),
            realpath("$builddir/tests/helper$libs"),
            realpath("$builddir/tests/lib$libs"),
        ),
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
# implementation of libnotgl, mirroring the NVIDIA implementation of
# libGL; and the binary is linked to libhelper, so we can demonstrate
# that libcapsule gives us the necessary cognitive dissonance to have
# two copies of libhelper loaded simultaneously.
run_ok([qw(bwrap
        --ro-bind / /
        --dev-bind /dev /dev
        --ro-bind /), $capsule_prefix,
        '--tmpfs', realpath("$builddir/tests/lib$libs"),
        '--tmpfs', $capsule_prefix.realpath($builddir),
        '--ro-bind', realpath("$builddir/tests/green"),
            $capsule_prefix.realpath("$builddir/tests/lib"),
        '--setenv', 'CAPSULE_PREFIX', $capsule_prefix,
        '--setenv', 'LD_LIBRARY_PATH', join(':',
            realpath("$builddir/tests/shim$libs"),
            realpath("$builddir/tests/helper$libs"),
            realpath("$builddir/tests/lib$libs"),
        ),
        $notgl_helper_user],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: green$/m);
like($stdout, qr/^NotGL helper implementation: host \(green\)$/m);
like($stdout,
    qr/^notgl_extension_both: green implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: \(not found\)$/m);
like($stdout, qr/^notgl_extension_green: green-only extension$/m);
like($stdout, qr/^NotGLES implementation: green$/m);
like($stdout, qr/^NotGLES helper implementation: host \(green\)$/m);
like($stdout,
    qr/^notgles_extension_both: green implementation of common extension$/m);
like($stdout, qr/^notgles_extension_red: \(not found\)$/m);
like($stdout, qr/^notgles_extension_green: green-only extension$/m);

# Also, this program is linked directly to libhelper, mirroring a program
# that is linked directly to libstdc++ in the libGL case. It sees the
# container's libhelper, not the host's - even though libnotgl sees the
# host's libhelper when it looks up the same symbol. (This is the point
# of libcapsule.)
like($stdout,
    qr/^NotGL helper implementation as seen by executable: container \(reference\)$/m);

# Using dlopen also works.
diag 'With libcapsule loading red implementation, via dlopen:';
run_ok([qw(bwrap
        --ro-bind / /
        --dev-bind /dev /dev
        --ro-bind /), $capsule_prefix,
        '--tmpfs', realpath("$builddir/tests/lib$libs"),
        '--tmpfs', $capsule_prefix.realpath($builddir),
        '--ro-bind', realpath("$builddir/tests/red"),
            $capsule_prefix.realpath("$builddir/tests/lib"),
        '--setenv', 'CAPSULE_PREFIX', $capsule_prefix,
        '--setenv', 'LD_LIBRARY_PATH', join(':',
            realpath("$builddir/tests/shim$libs"),
            realpath("$builddir/tests/helper$libs"),
            realpath("$builddir/tests/lib$libs"),
        ),
        $notgl_dlopener],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: red$/m);
like($stdout, qr/^NotGL helper implementation: host \(red\)$/m);
like($stdout,
    qr/^notgl_extension_both: red implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: red-only extension$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);
like($stdout, qr/^NotGLES implementation: red$/m);
like($stdout, qr/^NotGLES helper implementation: host \(red\)$/m);
like($stdout,
    qr/^notgles_extension_both: red implementation of common extension$/m);
like($stdout, qr/^notgles_extension_red: red-only extension$/m);
like($stdout, qr/^notgles_extension_green: \(not found\)$/m);

# We can use separate prefixes for different encapsulated libraries.
my $red_capsule_prefix = "$temp/red";
mkdir $red_capsule_prefix;
my $green_capsule_prefix = "$temp/green";
mkdir $green_capsule_prefix;

diag 'With libcapsule loading disparate implementations, via dlopen:';
run_ok([qw(bwrap
        --ro-bind / /
        --dev-bind /dev /dev
        --ro-bind /), $red_capsule_prefix,
        qw(--ro-bind /), $green_capsule_prefix,
        '--tmpfs', realpath("$builddir/tests/lib$libs"),
        '--tmpfs', $red_capsule_prefix.realpath($builddir),
        '--tmpfs', $green_capsule_prefix.realpath($builddir),
        '--ro-bind', realpath("$builddir/tests/red"),
            $red_capsule_prefix.realpath("$builddir/tests/lib"),
        '--ro-bind', realpath("$builddir/tests/green"),
            $green_capsule_prefix.realpath("$builddir/tests/lib"),
        '--setenv', 'CAPSULE_LIBNOTGL_SO_0_PREFIX', $red_capsule_prefix,
        # We don't specify CAPSULE_LIBNOTGLES_SO_1_PREFIX, so the
        # generic version gets used.
        '--setenv', 'CAPSULE_PREFIX', $green_capsule_prefix,
        '--setenv', 'LD_LIBRARY_PATH', join(':',
            realpath("$builddir/tests/shim$libs"),
            realpath("$builddir/tests/helper$libs"),
            realpath("$builddir/tests/lib$libs"),
        ),
        $notgl_dlopener],
    '>', \$stdout);
diag_multiline $stdout;
like($stdout, qr/^NotGL implementation: red$/m);
like($stdout, qr/^NotGL helper implementation: host \(red\)$/m);
like($stdout,
    qr/^notgl_extension_both: red implementation of common extension$/m);
like($stdout, qr/^notgl_extension_red: red-only extension$/m);
like($stdout, qr/^notgl_extension_green: \(not found\)$/m);
like($stdout, qr/^NotGLES implementation: green$/m);
like($stdout, qr/^NotGLES helper implementation: host \(green\)$/m);
like($stdout,
    qr/^notgles_extension_both: green implementation of common extension$/m);
like($stdout, qr/^notgles_extension_red: \(not found\)$/m);
like($stdout, qr/^notgles_extension_green: green-only extension$/m);

done_testing;

# vim:set sw=4 sts=4 et:
