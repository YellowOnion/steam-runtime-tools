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

use Cwd qw(abs_path);
use File::Temp qw();
use IPC::Run qw(run);
use Test::More;

use FindBin;
use lib $FindBin::Bin;

use CapsuleTest;

sub tolerant_readlink ($)
{
    no autodie;
    return readlink shift;
}

skip_all_unless_bwrap;

my $LIBDIR = qr{/(?:usr/)?lib(?:32|64|x32)?(?:/\w+-linux-gnu\w*)?(?:/i686)?(?:/cmov)?};

my $test_tempdir = File::Temp->newdir();
diag "Working directory: $test_tempdir";
my $host = "${test_tempdir}/host";
mkdir($host);
my $container = "${test_tempdir}/container";
mkdir($container);
my $libdir = "${test_tempdir}/libdir";

{
    open(my $fh, '>', "${test_tempdir}/empty");
    close $fh;
}
{
    open(my $fh, '>', "${test_tempdir}/libc");
    print $fh "# comment\n";
    print $fh "\n";
    print $fh "soname:libc.so.6\n";
    print $fh "soname:libc.so.6";  # deliberately no trailing newline
    close $fh;
}

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([$CAPSULE_CAPTURE_LIBS_TOOL, 'soname:libc.so.6'], '>&2',
    init => sub { chdir $libdir or die $!; });
ok(-e "$libdir/libc.so.6", 'libc.so.6 was captured');
like(readlink "$libdir/libc.so.6", qr{^$LIBDIR/libc\.so\.6$},
    '$libdir/libc.so.6 is a symlink to the real libc.so.6');
my $libc = abs_path("$libdir/libc.so.6");
ok(-e "$libdir/libBrokenLocale.so.1", 'related libraries were captured');
ok(-e "$libdir/libm.so.6", 'related libraries were captured');
ok(-e "$libdir/libpthread.so.0", 'related libraries were captured');
ok(-e "$libdir/libmemusage.so", 'related libraries were captured');
like(readlink "$libdir/libm.so.6", qr{^$LIBDIR/libm\.so\.6$},
    'libm is a symlink to the right place');
my @libc_family;
{
    opendir(my $dir_iter, $libdir);
    @libc_family = sort grep { $_ ne '.' and $_ ne '..' } readdir $dir_iter;
    closedir $dir_iter;
    foreach my $symlink (@libc_family) {
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
}

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([$CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/run/host',
        "--dest=$libdir", 'soname-match:libc.so.6'], '>&2');
{
    opendir(my $dir_iter, $libdir);
    my @links = sort grep { $_ ne '.' and $_ ne '..' } readdir $dir_iter;
    foreach my $symlink (@links) {
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
    is_deeply(\@links, \@libc_family,
        'the same libraries are captured when using --link-target');
}
like(readlink "$libdir/libc.so.6", qr{^/run/host\Q$libc\E$},
     '$libdir/libc.so.6 is a symlink to /run/host + realpath of libc.so.6');

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL,
        "--dest=$libdir",
        "--provider=$host",
        "from:$test_tempdir/empty",
        "from:$test_tempdir/libc"], '>&2');
{
    opendir(my $dir_iter, $libdir);
    my @links = sort grep { $_ ne '.' and $_ ne '..' } readdir $dir_iter;
    foreach my $symlink (@links) {
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
    is_deeply(\@links, \@libc_family,
        'the same libraries are captured when using $host');
}
like(readlink "$libdir/libc.so.6", qr{^\Q$host\E\Q$libc\E$},
     '$libdir/libc.so.6 is a symlink to $host + realpath of libc.so.6');

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/run/host',
        "--dest=$libdir",
        "--provider=$host", 'soname:libc.so.6'], '>&2');
{
    opendir(my $dir_iter, $libdir);
    my @links = sort grep { $_ ne '.' and $_ ne '..' } readdir $dir_iter;
    foreach my $symlink (@links) {
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
    is_deeply(\@links, \@libc_family,
        'the same libraries are captured when using $host and --link-target');
}
like(readlink "$libdir/libc.so.6", qr{^/run/host\Q$libc\E$},
     '$libdir/libc.so.6 is a symlink to /run/host + realpath of libc.so.6');

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
        "--dest=$libdir", "--provider=$host",
        'soname-match:libjp*g.so.6*'], '>&2');
{
    opendir(my $dir_iter, $libdir);
    foreach my $symlink (readdir $dir_iter) {
        next if $symlink eq '.' || $symlink eq '..';
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
}
like(readlink "$libdir/libjpeg.so.62", qr{^$LIBDIR/libjpeg\.so\.62(?:[0-9.]+)$},
     '$libdir/libjpeg.so.62 is a symlink to /run/host + realpath of libjpeg-6b');

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
my $stderr;
my $result = run_verbose([qw(bwrap --ro-bind / / --ro-bind /), $host,
                          '--bind', $libdir, $libdir,
                          qw(--dev-bind /dev /dev),
                          $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
                          "--dest=$libdir", "--provider=$host",
                          'soname-match:this*library*does?not?exist'],
                         '2>', \$stderr, '>&2');
ok(! $result, 'a non-matching wildcard should fail');
like($stderr, qr{"this\*library\*does\?not\?exist"});

run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
        "--dest=$libdir", "--provider=$host",
        'if-exists:soname-match:this*library*does?not?exist']);

# libgobject-2.0.so.0 depends on libglib-2.0.so.0 so normally,
# capturing GObject captures GLib
run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
        "--dest=$libdir", "--provider=$host",
        'soname:libgobject-2.0.so.0']);
{
    opendir(my $dir_iter, $libdir);
    foreach my $symlink (readdir $dir_iter) {
        next if $symlink eq '.' || $symlink eq '..';
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
}
like(readlink "$libdir/libgobject-2.0.so.0",
     qr{^$LIBDIR/libgobject-2\.0\.so\.0(?:[0-9.]+)$},
     '$libdir/libgobject-2.0.so.0 is captured');
like(readlink "$libdir/libglib-2.0.so.0",
     qr{^$LIBDIR/libglib-2\.0\.so\.0(?:[0-9.]+)$},
     '$libdir/libglib-2.0.so.0 is captured as a dependency');

# only-dependencies: captures the dependency, GLib but not the
# dependent library, GObject
run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
        "--dest=$libdir", "--provider=$host",
        'only-dependencies:soname:libgobject-2.0.so.0']);
{
    opendir(my $dir_iter, $libdir);
    foreach my $symlink (readdir $dir_iter) {
        next if $symlink eq '.' || $symlink eq '..';
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
}
ok(! -e "$libdir/libgobject-2.0.so.0",
   'only-dependencies: does not capture the library itself');
like(readlink "$libdir/libglib-2.0.so.0",
     qr{^$LIBDIR/libglib-2\.0\.so\.0(?:[0-9.]+)$},
     '$libdir/libglib-2.0.so.0 is captured as a dependency');

# no-dependencies: captures the dependent library, GObject but not the
# dependency, GLib (not amazingly useful)
run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
        "--dest=$libdir", "--provider=$host",
        'no-dependencies:soname:libgobject-2.0.so.0']);
{
    opendir(my $dir_iter, $libdir);
    foreach my $symlink (readdir $dir_iter) {
        next if $symlink eq '.' || $symlink eq '..';
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
}
like(readlink "$libdir/libgobject-2.0.so.0",
     qr{^$LIBDIR/libgobject-2\.0\.so\.0(?:[0-9.]+)$},
     '$libdir/libgobject-2.0.so.0 is captured');
ok(! -e "$libdir/libglib-2.0.so.0",
   'no-dependencies: does not capture libglib-2.0.so.0');

# --no-glibc doesn't capture glibc even if other dependencies are
# captured
run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
        '--bind', $libdir, $libdir,
        qw(--dev-bind /dev /dev),
        $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
        "--dest=$libdir", "--provider=$host", "--no-glibc",
        'soname:libgobject-2.0.so.0']);
{
    opendir(my $dir_iter, $libdir);
    foreach my $symlink (readdir $dir_iter) {
        next if $symlink eq '.' || $symlink eq '..';
        diag "- $symlink -> ".readlink("$libdir/$symlink");
    }
    closedir $dir_iter;
}
like(readlink "$libdir/libgobject-2.0.so.0",
     qr{^$LIBDIR/libgobject-2\.0\.so\.0(?:[0-9.]+)$},
     '$libdir/libgobject-2.0.so.0 is captured');
like(readlink "$libdir/libglib-2.0.so.0",
     qr{^$LIBDIR/libglib-2\.0\.so\.0(?:[0-9.]+)$},
     '$libdir/libglib-2.0.so.0 is captured');
ok(! -e "$libdir/libc.so.6",
   '--no-glibc does not capture libc.so.6');

# only-dependencies:no-dependencies: (either way round) is completely useless
run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
$result = run_verbose([qw(bwrap --ro-bind / / --ro-bind /), $host,
                       '--bind', $libdir, $libdir,
                       qw(--dev-bind /dev /dev),
                       $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
                       "--dest=$libdir", "--provider=$host",
                       'only-dependencies:no-dependencies:libglib-2.0.so.0'],
                       '>&2');
ok(! $result, 'only-dependencies:no-dependencies: is useless');
run_ok(['rm', '-fr', $libdir]);
mkdir($libdir);
$result = run_verbose([qw(bwrap --ro-bind / / --ro-bind /), $host,
                       '--bind', $libdir, $libdir,
                       qw(--dev-bind /dev /dev),
                       $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
                       "--dest=$libdir", "--provider=$host",
                       'no-dependencies:only-dependencies:libglib-2.0.so.0'],
                       '>&2');
ok(! $result, 'no-dependencies:only-dependencies: is useless');

SKIP: {
    skip "not on Linux? Good luck!", 1 unless $^O eq 'linux';
    my $stdout;
    my $ld_so;

    run_ok([$CAPSULE_CAPTURE_LIBS_TOOL, '--print-ld.so'], '>', \$ld_so);
    chomp $ld_so;
    diag "ld.so is $ld_so";

    run_ok([$CAPSULE_CAPTURE_LIBS_TOOL, '--resolve-ld.so=/'], '>', \$stdout);
    my $resolved = $stdout;
    chomp $resolved;

    is(abs_path($resolved), abs_path($ld_so),
       '--resolve-ld.so=/ should print a path to '.abs_path($ld_so));
    ok(! -l $resolved,
       '--resolve-ld.so=/ should not print a symlink');

    # Autotools considers an unexpected pass to be a test failure, so we
    # have to second-guess whether this is going to work.
    if ($stdout eq abs_path($ld_so)."\n") {
        is($stdout, abs_path($ld_so)."\n",
           '--resolve-ld.so=/ should print '.abs_path($ld_so));
    }
    else {
        TODO: {
            local $TODO = 'symlinks before the last component are not resolved, '
                .'e.g. /lib64 -> usr/lib on Arch Linux';
            is($stdout, abs_path($ld_so)."\n",
               '--resolve-ld.so=/ should print '.abs_path($ld_so));
        }
    }

    run_ok([qw(bwrap --ro-bind / / --ro-bind /), $host,
            $CAPSULE_CAPTURE_LIBS_TOOL, "--resolve-ld.so=$host"],
        '>', \$stdout);
    $resolved = $stdout;
    chomp $resolved;

    is(abs_path($resolved), abs_path($ld_so),
       "--resolve-ld.so=$host should print a path to ".abs_path($ld_so));
    ok(! -l $resolved,
       "--resolve-ld.so=$host should not print a symlink");

    if ($stdout eq abs_path($ld_so)."\n") {
        is($stdout, abs_path($ld_so)."\n",
           "--resolve-ld.so=$host should print ".abs_path($ld_so));
    }
    else {
        TODO: {
            local $TODO = 'symlinks before the last component are not resolved, '
                .'e.g. /lib64 -> usr/lib on Arch Linux';
            is($stdout, abs_path($ld_so)."\n",
               "--resolve-ld.so=$host should print ".abs_path($ld_so));
        }
    }
};

SKIP: {
    my $multiarch;
    my $other_multiarch;

    # For simplicity, we only consider this case on x86.
    skip "$ENV{CAPSULE_TESTS_GNU_HOST} not x86_64 or i386", 1
        unless $ENV{CAPSULE_TESTS_GNU_HOST} =~ m/^(x86_64|i.86)-/;

    if ($ENV{CAPSULE_TESTS_GNU_HOST} =~ m/^x86_64-/) {
        $other_multiarch = 'i386-linux-gnu';
    }
    else {
        $other_multiarch = 'x86_64-linux-gnu';
    }

    skip "$other_multiarch libjpeg.so.62 not available", 1
        unless -e "/usr/lib/$other_multiarch/libjpeg.so.62";

    # Normally, the wrong ABI is an error...
    run_ok(['rm', '-fr', $libdir]);
    mkdir($libdir);
    $result = run_verbose([qw(bwrap --ro-bind / / --ro-bind /), $host,
                           '--bind', $libdir, $libdir,
                           qw(--dev-bind /dev /dev),
                           $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
                           "--dest=$libdir", "--provider=$host",
                           "path:/usr/lib/$other_multiarch/libjpeg.so.62"],
                           '>&2');
    ok(! $result, 'library of wrong ABI yields an error');
    ok(! -e "$libdir/libjpeg.so.62");

    # ... but when we're dealing with a glob match, other ABIs are silently
    # ignored.
    run_ok(['rm', '-fr', $libdir]);
    mkdir($libdir);
    $result = run_verbose([qw(bwrap --ro-bind / / --ro-bind /), $host,
                           '--bind', $libdir, $libdir,
                           qw(--dev-bind /dev /dev),
                           $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
                           "--dest=$libdir", "--provider=$host",
                           "path-match:/usr/lib/$other_multiarch/libjpeg.so.62"],
                           '>&2');
    ok($result, 'library of wrong ABI ignored when using path-match');
    ok(! -e "$libdir/libjpeg.so.62");

    # We can also ignore this case explicitly.
    run_ok(['rm', '-fr', $libdir]);
    mkdir($libdir);
    $result = run_verbose([qw(bwrap --ro-bind / / --ro-bind /), $host,
                           '--bind', $libdir, $libdir,
                           qw(--dev-bind /dev /dev),
                           $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/',
                           "--dest=$libdir", "--provider=$host",
                           "if-same-abi:path:/usr/lib/$other_multiarch/libjpeg.so.62"],
                           '>&2');
    ok($result, 'library of wrong ABI ignored when using if-same-abi');
    ok(! -e "$libdir/libjpeg.so.62");
};

SKIP: {
    my $version1 = "$builddir/tests/version1";
    $version1 .= '/.libs' if libcapsule_uninstalled;
    my $version2 = "$builddir/tests/version2";
    $version2 .= '/.libs' if libcapsule_uninstalled;

    skip "shared libraries disabled", 1 unless -e "$version1/libunversionedsymbols.so.1";

    run_ok(['rm', '-fr', $libdir]);
    mkdir($libdir);
    $result = run_verbose([qw(bwrap --ro-bind / /),
                           '--tmpfs', $host,
                           bind_usr('/', $host),
                           '--tmpfs', $container,
                           bind_usr('/', $container),
                           '--ro-bind', $version1, "$host/opt",
                           '--ro-bind', $version2, "$container/opt",
                           '--bind', $libdir, $libdir,
                           qw(--dev-bind /dev /dev),
                           'env', 'CAPSULE_DEBUG=all',
                           $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/run/host',
                           "--dest=$libdir", "--provider=$host",
                           "--compare-by=versions,name,symbols,provider",
                           "--container=$container",
                           'path-match:/opt/lib*.so.1'],
                           '2>', \$stderr, '>&2');
    diag $stderr;
    ok($result);
    is(tolerant_readlink("$libdir/libunversionedabibreak.so.1"),
       '/run/host/opt/libunversionedabibreak.so.1.0.0',
       "should take provider's version when neither's symbols are a superset of the other");
    ok(! -l "$libdir/libunversionednumber.so.1",
       "should not take provider's version when container's numeric tail is strictly newer");
    ok(! -l "$libdir/libunversionedsymbols.so.1",
       "should not take provider's version when container has strictly more symbols");
    is(tolerant_readlink("$libdir/libversionedabibreak.so.1"),
       '/run/host/opt/libversionedabibreak.so.1.0.0',
       "should take provider's version when neither's symbol-versions are a superset of the other");
    ok(! -l "$libdir/libversionedlikedbus.so.1",
       "should not take provider's older fake libdbus");
    ok(! -l "$libdir/libversionedlikeglibc.so.1",
       "should not take provider's older fake glibc");
    ok(! -l "$libdir/libversionednumber.so.1",
       "should not take provider's version when container's numeric tail is strictly newer and symbols are equal");
    ok(! -l "$libdir/libversionedsymbols.so.1",
       "should not take provider's version when container has strictly more symbols");
    ok(! -l "$libdir/libversionedupgrade.so.1",
       "should not take provider's version when container has strictly more symbols");

    # The opposite.
    run_ok(['rm', '-fr', $libdir]);
    mkdir($libdir);
    $result = run_verbose([qw(bwrap --ro-bind / /),
                           '--tmpfs', $host,
                           bind_usr('/', $host),
                           '--tmpfs', $container,
                           bind_usr('/', $container),
                           '--ro-bind', $version1, "$container/opt",
                           '--ro-bind', $version2, "$host/opt",
                           '--bind', $libdir, $libdir,
                           qw(--dev-bind /dev /dev),
                           'env', 'CAPSULE_DEBUG=all',
                           $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/run/host',
                           "--dest=$libdir", "--provider=$host",
                           # This time we leave the trailing ,provider implict
                           "--compare-by=versions,name,symbols",
                           "--container=$container",
                           'path-match:/opt/lib*.so.1'],
                           '2>', \$stderr, '>&2');
    diag $stderr;
    ok($result);
    is(tolerant_readlink("$libdir/libunversionedabibreak.so.1"),
       '/run/host/opt/libunversionedabibreak.so.1.0.0',
       "should take provider's version when neither's symbols are a superset of the other");
    is(tolerant_readlink("$libdir/libunversionednumber.so.1"),
       '/run/host/opt/libunversionednumber.so.1.2.3',
       "should take provider's version when container's numeric tail is strictly older");
    is(tolerant_readlink("$libdir/libunversionedsymbols.so.1"),
       '/run/host/opt/libunversionedsymbols.so.1.0.0',
       "should take provider's version when it has strictly more symbols than container");
    is(tolerant_readlink("$libdir/libversionedabibreak.so.1"),
       '/run/host/opt/libversionedabibreak.so.1.0.0',
       "should take provider's version when neither's symbol-versions are a superset of the other");
    is(tolerant_readlink("$libdir/libversionedlikedbus.so.1"),
       '/run/host/opt/libversionedlikedbus.so.1.2.0',
       "should take provider's newer fake libdbus");
    is(tolerant_readlink("$libdir/libversionedlikeglibc.so.1"),
       '/run/host/opt/libversionedlikeglibc.so.1.0.0',
       "should take provider's newer fake glibc");
    is(tolerant_readlink("$libdir/libversionednumber.so.1"),
       '/run/host/opt/libversionednumber.so.1.2.3',
       "should take provider's version when its numeric tail is strictly newer and symbols are equal");
    is(tolerant_readlink("$libdir/libversionedsymbols.so.1"),
       '/run/host/opt/libversionedsymbols.so.1.0.0',
       "should take provider's version when it has strictly more symbols");
    is(tolerant_readlink("$libdir/libversionedupgrade.so.1"),
       '/run/host/opt/libversionedupgrade.so.1.0.0',
       "should take provider's version when it has strictly more symbols");

    # The default is equivalent to --compare-by=name,provider.
    run_ok(['rm', '-fr', $libdir]);
    mkdir($libdir);
    $result = run_verbose([qw(bwrap --ro-bind / /),
                           '--tmpfs', $host,
                           bind_usr('/', $host),
                           '--tmpfs', $container,
                           bind_usr('/', $container),
                           '--ro-bind', $version1, "$host/opt",
                           '--ro-bind', $version2, "$container/opt",
                           '--bind', $libdir, $libdir,
                           qw(--dev-bind /dev /dev),
                           'env', 'CAPSULE_DEBUG=all',
                           $CAPSULE_CAPTURE_LIBS_TOOL, '--link-target=/run/host',
                           "--dest=$libdir", "--provider=$host",
                           "--container=$container",
                           'path-match:/opt/lib*.so.1'],
                           '2>', \$stderr, '>&2');
    diag $stderr;
    ok($result);
    is(tolerant_readlink("$libdir/libunversionedabibreak.so.1"),
       '/run/host/opt/libunversionedabibreak.so.1.0.0',
       "should take provider's version by default if name is the same");
    is(tolerant_readlink("$libdir/libunversionedsymbols.so.1"),
       '/run/host/opt/libunversionedsymbols.so.1.0.0',
       "should take provider's version by default if name is the same");
    is(tolerant_readlink("$libdir/libversionedabibreak.so.1"),
       '/run/host/opt/libversionedabibreak.so.1.0.0',
       "should take provider's version by default if name is the same");
    is(tolerant_readlink("$libdir/libversionedsymbols.so.1"),
       '/run/host/opt/libversionedsymbols.so.1.0.0',
       "should take provider's version by default if name is the same");
    is(tolerant_readlink("$libdir/libversionedupgrade.so.1"),
       '/run/host/opt/libversionedupgrade.so.1.0.0',
       "should take provider's version by default if name is the same");
    is(tolerant_readlink("$libdir/libversionedlikeglibc.so.1"),
       '/run/host/opt/libversionedlikeglibc.so.1.0.0',
       "should take provider's version by default if name is the same");
    ok(! -l "$libdir/libunversionednumber.so.1",
       "should not take provider's version when container's numeric tail is newer");
    ok(! -l "$libdir/libversionedlikedbus.so.1",
       "should not take provider's version when container's numeric tail is newer");
    ok(! -l "$libdir/libversionednumber.so.1",
       "should not take provider's version when container's numeric tail is newer");
}

done_testing;

# vim:set sw=4 sts=4 et:
