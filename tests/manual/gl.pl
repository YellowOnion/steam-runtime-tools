#!/usr/bin/env perl

# gl.pl — exercise libGL + bwrap, with or without libcapsule
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

use autodie;
use Cwd qw(realpath);
use File::Path qw(make_path);
use File::Spec qw();
use File::Temp qw();
use Getopt::Long;
use IPC::Run qw(run);
use Sort::Versions;
use Test::More;

use FindBin;
use lib ($FindBin::Bin, "$FindBin::Bin/..");
use CapsuleTest;

=encoding utf8

=head1 NAME

gl.pl - exercise libGL + bwrap, with or without libcapsule

=head1 SYNOPSIS

B<gl.pl> [I<OPTIONS>] [I<COMMAND> [I<ARGUMENTS...>]]

Run C<I<COMMAND> I<ARGUMENTS...>> in the container, or if no command
is given, run B<glxinfo>(1) and make some assertions about its output.

The GL stack is made available in the container as F</overrides/lib/*>.
This can either be the host system's GL stack (provided as symbolic
links into F</run/host/{usr/,}lib*>), a different sysroot's GL stack
(provided as symbolic links into F</run/host/some-sysroot/{usr/,}lib*>), a
standalone GL stack (for example proprietary binaries), or a libcapsule
shim that will load the real GL stack from F</run/host> or
F</run/host/some-sysroot>.

F</usr>, F</bin>, F</sbin>, F</lib*>, F</etc/alternatives> and
F</etc/ld.so.cache> from the host system or a sysroot are always mounted
at F</run/host> or F</run/host/some-sysroot>.

By default, glibc is taken from either the GL provider or the container,
whichever has the newer version. It can also be taken from a third tree.
If the version in use is not the container's, F</overrides/lib/*> is
populated with symbolic links into F</run/host/.../lib*>.

Similarly, libcapsule and libelf are taken from either the GL provider
or the container, or can be taken from a third tree.

Other core graphics libraries (libX11, libxcb, etc.) are taken from either
the GL provider, the container, or another tree.

An application "payload" is mounted on F</app>, as if for Flatpak.

The B<LD_LIBRARY_PATH> for the child process is made to include
library directories from F</app> and F</overrides>.

=head1 EXAMPLES

=over

=item Basic use

    $ gl.pl

Run B<glxinfo>(1), non-interactively. The container is a Debian stretch
Flatpak runtime; get it using the instructions from
L<https://gitlab.collabora.com/smcv/flatdeb/blob/master/README>,
and rsync ~/.cache/flatdeb/repo onto a separate test machine if necessary.

The OpenGL implementation and graphics stack are provided by the host
system. Core libraries (glibc, libcapsule, libX11) and
supporting libraries for the graphics stack (libstdc++, libpciaccess,
libXext) are provided by either the container or the host system,
whichever has the newer version, preferring the host system if the
versions appear to be the same.

=item Using the Steam Runtime

    $ gl.pl \
    --flatpak-runtime=com.valvesoftware.SteamRuntime.Platform/x86_64/scout_beta \
    --flatpak-app=org.debian.packages.mesa_utils/x86_64/scout_beta

Run B<glxinfo>(1) in the Steam Runtime. The container is a Steam Runtime
scout_beta Flatpak runtime; get it using the instructions from
L<https://gitlab.collabora.com/smcv/flatdeb-steam/blob/master/README>,
and rsync ~/.cache/flatdeb/repo onto a separate test machine if necessary.

=item Running glxgears

    $ gl.pl glxgears

Run B<glxgears>(1) instead of B<glxinfo>(1). Press B<Escape> to exit.

=item Running OpenArena

    $ gl.pl \
    --flatpak-app=org.debian.packages.openarena/x86_64/master \
    -- \
    openarena +set com_hunkmegs 512 +set timedemo 1 +demo demo088-test1

Run B<openarena>(6) instead of B<glxinfo>(1), and run a demo at the maximum
possible frame rate. Type B<\quit> into the terminal, or
B<Shift+Escape \quit> into the GUI window, when the demo has finished.

=item Using a sysroot instead of a Flatpak runtime

    $ sudo debootstrap --include=(many packages) stretch \
    ~/stretch-sysroot http://deb.debian.org/debian
    $ gl.pl --container ~/stretch-sysroot

The container can be a complete sysroot (with a F</usr>) instead of just
a Flatpak runtime or a merged F</usr>.

=item Using NVIDIA proprietary drivers

    $ gl.pl --proposed-nvidia

This uses the host NVIDIA drivers, without libcapsule.

=item Using Mesa not via libcapsule

    $ gl.pl --full-mesa

This uses the host Mesa drivers, without libcapsule. We need to pull in
lots of libraries from the host system.

=item Using Mesa via libcapsule

    $ gl.pl --proposed-mesa

This uses the host Mesa drivers, with libcapsule.

=item Using a non-host GL stack

    $ sudo debootstrap --include=(many packages) sid \
    ~/sid-sysroot http://deb.debian.org/debian
    $ gl.pl --gl-provider ~/sid-sysroot --container ~/stretch-sysroot

The GL provider can be a complete sysroot (with a F</usr>), or a
merged F</usr>, instead of the host system.

If the GL provider uses B<GLVND> and the host system's X11 display does
not have the B<GLX_EXT_libglvnd> extension, then either the GL provider
must have a symbolic link B<libGLX_indirect.so.0> to the correct
vendor GLX library, or B<__GLX_FORCE_VENDOR_LIBRARY_0> or
B<__GLX_VENDOR_LIBRARY_NAME> must be set to the correct vendor name
(typically B<mesa> or B<nvidia>).

=item Using libcapsule

    $ gl.pl --libcapsule
      # which is just a shortcut for...
    $ gl.pl --gl-stack=/usr/lib/libcapsule/shims

Actually use libcapsule. This currently works with a SteamOS brewmaster
(Debian jessie-based) host, a Steam Runtime or Debian stretch container,
Mesa graphics, and glxinfo, glxgears or OpenArena as the app, but crashes
when the graphics driver is NVIDIA.

=back

=head1 OPTIONS

=over

=item --app=I<APP>

Mount I<APP> on F</app>. See B<--flatpak-app> for the default.

=item --container=I<TREE>

A complete sysroot or a merged F</usr> for the container. Most libraries
will come from here. See B<--flatpak-runtime> for the default.

=item --flatpak-app=I<ID>/I<ARCH>/I<BRANCH>

Assume that I<ID>/I<ARCH>/I<BRANCH> has been installed per-user with
C<flatpak --user install ...>, and use it as the B<--app>.
The default is F<org.debian.packages.mesa_utils/x86_64/scout_beta>.

=item --flatpak-runtime=I<ID>/I<ARCH>/I<BRANCH>

Assume that I<ID>/I<ARCH>/I<BRANCH> has been installed per-user with
C<flatpak --user install ...>, and use it as the B<--container>.
The default is F<net.debian.flatpak.Games.Platform/x86_64/stretch>.
Another interesting value is
F<com.valvesoftware.SteamRuntime.Platform/x86_64/scout_beta>.

=item --gl-provider=I<TREE>

A complete sysroot or a merged F</usr> for the OpenGL provider.
The OpenGL implementation will come from here unless overridden
by B<--gl-stack>, and whatever supporting libraries are required will
come from here unless overridden by B<--libc-provider>,
B<--libcapsule-provider>, B<--x11-provider>. It will be mounted under
F</run/host>. The default is F</>, meaning we use the host system
as the OpenGL provider.

=item --gl-stack=I<TREE>

A pre-prepared OpenGL stack, with F<lib/I<TUPLE>> subdirectories
containing F<libGLX.so.1> and related libraries. It will be mounted
under F</run/host>, and F</overrides> will be populated with symlinks
to it and added to the B<LD_LIBRARY_PATH>.

=item --multiarch=I<TUPLE>[,I<TUPLE>...]

A list of Debian multiarch tuples to enable. The default is
C<x86_64-linux-gnu>.

=item --libc-provider=B<auto>|B<container>|B<gl-provider>|I<USR>|I<SYSROOT>

Get glibc (libc, libm, libpthread etc.) from here. B<container> uses the
B<--container> (or B<--flatpak-runtime>). B<gl-provider> uses the
B<--gl-provider>. B<auto> uses whichever of those appears to have newer
libraries, mixing versions from both if necessary. Alternatively,
a complete sysroot or a merged F</usr> can be given.
The default is B<auto>.

=item --x11-provider=B<auto>|B<container>|B<gl-provider>|I<USR>|I<SYSROOT>

The same as for B<--libc-provider>, but for libX11 and related libraries.
The default is B<auto>.

=item --libcapsule-provider=B<auto>|B<container>|B<gl-provider>|I<USR>|I<SYSROOT>

The same as for B<--libc-provider>, but for libcapsule and libelf.
The default is B<container>, but B<--libcapsule> overrides this to
B</>.

=item --libcapsule

A shortcut for B<--gl-stack=/usr/lib/libcapsule/shims> and
B<--libcapsule-provider=/>.
This uses the libcapsule libGL, etc. shims from the host system.

=item --proposed-mesa

Use behaviour proposed as potentially appropriate for the Mesa graphics
stack: B<--gl-provider=/> B<--libcapsule> B<--libc-provider=auto>
B<--x11-provider=auto>.

=item --proposed-nvidia

Use behaviour proposed as potentially appropriate for the NVIDIA graphics
stack, or other binary blobs with conservative dependencies:
B<--gl-provider=/> B<--libc-provider=container>
B<--x11-provider=container>.

=back

=cut

my $ansi_bright = "";
my $ansi_cyan = "";
my $ansi_green = "";
my $ansi_magenta = "";
my $ansi_reset = "";

if (-t STDOUT) {
    # Mnemonic: cyan is the container, green is the GL provider, magenta
    # is miscellaneous
    $ansi_bright = "\e[1m";
    $ansi_cyan = "\e[36m";
    $ansi_green = "\e[32m";
    $ansi_magenta = "\e[35m";
    $ansi_reset = "\e[0m";
}

my %CAPSULE_VERSION_TOOLS;
my %CAPSULE_CAPTURE_LIBS_TOOLS;

=head1 FUNCTIONS

=over

=item bind_usr(I<TREE>[, I<DEST>])

Return B<bwrap>(1) arguments that would bind-mount I<TREE> on I<DEST>.
If I<TREE> has a F<usr> directory, it is assumed to be a complete
sysroot with F<bin>, F<sbin>, F<lib*> and F<usr> directories, which
will be mounted on I<DEST>. If not, it is assumed to be a merged
F</usr>, and will be mounted on I<DEST>F</usr>, with compatibility
symbolic links in I<DEST> for the F<bin>, F<sbin> and F<lib*>
subdirectories.

=cut

sub bind_usr {
    my $tree = shift;
    my $dest = shift;
    $dest = '' unless defined $dest;
    my @bwrap;

    my $has_usr = (-d "$tree/usr");

    if ($has_usr) {
        push @bwrap, '--ro-bind', "$tree/usr", "$dest/usr";
    }
    else {
        push @bwrap, '--ro-bind', "$tree", "$dest/usr";
    }

    opendir(my $dir, $tree);
    while (defined(my $subdir = readdir $dir)) {
        if ($subdir =~ /^lib/ || $subdir =~ /^s?bin$/) {
            if ($has_usr) {
                push @bwrap, '--ro-bind', "$tree/$subdir", "$dest/$subdir";
            }
            else {
                push @bwrap, '--symlink', "usr/$subdir", "$dest/$subdir";
            }
        }
    }
    closedir $dir;

    if (-e "$tree/etc/ld.so.cache") {
        push @bwrap, "--ro-bind", "$tree/etc/ld.so.cache",
            "$dest/etc/ld.so.cache";
    }

    # TODO: This wouldn't be necessary in a purely glvnd system
    if (-d "$tree/etc/alternatives") {
        push @bwrap, "--ro-bind", "$tree/etc/alternatives",
            "$dest/etc/alternatives";
    }

    return @bwrap;
}

=item run_in_container(I<TREE>, I<ARGV>, ...)

Use B<bwrap>(1) to run the command I<ARGV> (an array reference) in I<TREE>,
which may either be a complete sysroot or just the contents of a merged
F</usr> (see C<bind_usr>). The command is assumed not to need write
access to anything in the tree. Remaining arguments are passed to
C<IPC::Run::run> after the I<ARGV>.

=cut

sub run_in_container {
    my $tree = shift;
    my $argv = shift;
    my @run_params = @_;
    my @bwrap;

    @bwrap = qw(bwrap --dev-bind /dev /dev --bind /proc /proc --bind /sys /sys);
    push @bwrap, bind_usr($tree);

    foreach my $subdir (qw(etc var)) {
        if (-d "$tree/$subdir") {
            push @bwrap, '--ro-bind', "$tree/$subdir", "/$subdir";
        }
    }

    return run([@bwrap, @$argv], @run_params);
}

=item multiarch_tuple_to_ldso(I<TUPLE>)

Return the B<ld.so>(8) implementation for a Debian multiarch tuple.
For example,
C<multiarch_tuple_to_ldso('x86_64-linux-gnu')> returns
F</lib64/ld-linux-x86-64.so.2>.

=cut

my %multiarch_tuple_to_ldso;

sub multiarch_tuple_to_ldso {
    my $tuple = shift;
    my $stdout;

    if (defined $multiarch_tuple_to_ldso{$tuple}) {
        return $multiarch_tuple_to_ldso{$tuple};
    }

    assert_run_verbose([
        $CAPSULE_CAPTURE_LIBS_TOOLS{$tuple}, '--print-ld.so'
    ], '>', \$stdout);
    chomp $stdout;
    $multiarch_tuple_to_ldso{$tuple} = $stdout;
    return $stdout;
}

=item real_path_of_ldso(I<TREE>, I<TUPLE>, I<WORKSPACE>)

Return the path to the B<ld.so>(8) implementation for a Debian multiarch
tuple in I<TREE>, relative to I<TREE> itself. For example,
C<real_path_of_ldso('/', 'x86_64-linux-gnu', $workspace)> might
return F</lib/x86_64-linux-gnu/ld-2.25.so>, while
C<real_path_of_ldso('/chroots/stretch', 'i386-linux-gnu', $workspace)>
might return F</lib/i386-linux-gnu/ld-2.24.so>.

I<WORKSPACE> is a temporary directory used internally.

=cut

sub real_path_of_ldso {
    my $tree = shift;
    my $tuple = shift;
    my $workspace = shift;
    my $stdout;
    $tree = '/' unless defined $tree;

    assert_run_verbose([
        'bwrap',
        '--ro-bind', '/', '/',
        '--tmpfs', $workspace,
        bind_usr($tree, $workspace),
        $CAPSULE_CAPTURE_LIBS_TOOLS{$tuple},
        "--resolve-ld.so=$workspace",
    ], '>', \$stdout);
    chomp $stdout;
    diag "-> $stdout";
    return $stdout;
}

=item use_ldso(I<LIBC_PROVIDER>, I<CONTAINER>, I<TUPLES>, I<WORKSPACE>)

Return the necessary B<bwrap>(1) arguments to override the container's
B<ld.so>(1) with the one from the libc provider.

=cut

sub use_ldso {
    my $libc_provider_tree = shift;
    my $container_tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $workspace = shift;
    my @bwrap;

    foreach my $tuple (@multiarch_tuples) {
        my $resolved = real_path_of_ldso($libc_provider_tree, $tuple,
            $workspace);
        my $ldso = real_path_of_ldso($container_tree, $tuple, $workspace);

        if (-d "$container_tree/usr" || $ldso =~ m{^/usr/}) {
            diag "Adding bind-mount of $libc_provider_tree$resolved over $ldso";
            push @bwrap, '--ro-bind', "$libc_provider_tree$resolved", $ldso;
        }
        else {
            diag "Adding bind-mount of $libc_provider_tree$resolved over /usr$ldso because /lib{,64} will be symlinks";
            push @bwrap, '--ro-bind', "$libc_provider_tree$resolved", "/usr$ldso";
        }
    }

    return @bwrap;
}

=item capture_libs(I<PROVIDER>, I<CONTAINER>, I<TUPLES>, I<DEST>, I<LIBS>, I<WORKSPACE>, I<MOUNT>)

Populate I<DEST>F</lib/>I<TUPLE> with symbolic links to the libraries
in I<PROVIDER>, assuming that I<PROVIDER> will be mounted on
I<MOUNT>, such that I<DEST>F</lib/>I<TUPLE> can be added
to the B<LD_LIBRARY_PATH>. Use I<WORKSPACE> as a temporary work area.

Libraries that appear to be strictly newer in I<CONTAINER> are skipped.

I<TUPLES> is a reference to an array of Debian multiarch tuples.

I<LIBS> is a list of library patterns as for B<capsule-capture-libs>(1).

=cut

sub capture_libs {
    my $provider_tree = shift;
    my $container_tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $dest = shift;
    my @libs = @{ shift() };
    my $workspace = shift;
    my $mount = shift;
    my @maybe_container;


    foreach my $tuple (@multiarch_tuples) {
        make_path("$dest/lib/$tuple", verbose => 1);

        assert_run_verbose([
            'bwrap',
            '--ro-bind', '/', '/',
            '--bind', $dest, $dest,
            '--tmpfs', $workspace,
            bind_usr($container_tree, $workspace),
            $CAPSULE_CAPTURE_LIBS_TOOLS{$tuple},
            "--container=$workspace",
            "--link-target=$mount",
            "--dest=$dest/lib/$tuple",
            "--provider=$provider_tree",
            @libs,
        ], '>&2');
    }
}

=back

=cut

skip_all_unless_bwrap;

my $stdout;

my $data_home = $ENV{XDG_DATA_HOME};
$data_home = "$ENV{HOME}/.local/share" unless defined $data_home;
my $gl_provider_tree = "/";
my $container_tree;
my $flatpak_app;
my $flatpak_runtime;
my @multiarch_tuples = qw(x86_64-linux-gnu);
my $app;
my $gl_stack = undef;
my $arch = 'x86_64';
my $libc_provider_tree = 'auto';
my $libcapsule_tree = 'container';
my $x11_provider_tree = 'auto';
my $mesa_drivers = 1;
my $mesa_driver_deps = 1;
my $nvidia_only = 0;

GetOptions(
    'app=s' => \$app,
    'container=s' => \$container_tree,
    'flatpak-app=s' => \$flatpak_app,
    'flatpak-runtime=s' => \$flatpak_runtime,
    'gl-provider=s' => \$gl_provider_tree,
    'gl-stack=s' => \$gl_stack,
    'libcapsule-provider=s' => \$libcapsule_tree,
    'libcapsule' => sub {
        $gl_stack = '/usr/lib/libcapsule/shims';
        $libcapsule_tree = '/';
        $mesa_driver_deps = 0;
    },
    'libc-provider=s' => \$libc_provider_tree,
    'mesa-drivers' => \$mesa_drivers,
    'no-mesa-drivers' => sub { $mesa_drivers = 0; },
    'mesa-driver-deps' => \$mesa_driver_deps,
    'no-mesa-driver-deps' => sub { $mesa_driver_deps = 0; },
    'nvidia-only' => \$nvidia_only,
    'x11-provider=s' => \$x11_provider_tree,
    'multiarch=s' => sub {
        @multiarch_tuples = split /[\s,]+/, $_[1];
    },
    'full-mesa' => sub {
        $gl_provider_tree = '/';
        undef $gl_stack;
        $libcapsule_tree = 'container';
        $libc_provider_tree = 'auto';
        $mesa_drivers = 1;
        $mesa_driver_deps = 1;
        $nvidia_only = 0;
        $x11_provider_tree = 'auto';
    },
    'proposed-mesa' => sub {
        $gl_provider_tree = '/';
        $gl_stack = '/usr/lib/libcapsule/shims';
        $libcapsule_tree = '/';
        $libc_provider_tree = 'auto';
        $mesa_drivers = 1;
        $mesa_driver_deps = 0;
        $nvidia_only = 0;
        $x11_provider_tree = 'auto';
    },
    'proposed-nvidia' => sub {
        $gl_provider_tree = '/';
        undef $gl_stack;
        $libcapsule_tree = 'container';
        $libc_provider_tree = 'container';
        $mesa_drivers = 0;
        $mesa_driver_deps = 0;
        $nvidia_only = 1;
        $x11_provider_tree = 'container';
    },
    help => sub {
        print <<EOF;
Usage: $0 [OPTIONS] [-- COMMAND]
Options:
    --app=DIRECTORY                     Mount DIRECTORY at /app
    --flatpak-app=NAME/ARCH/BRANCH      Use a `flatpak --user` app for --app
                                        [….mesa_utils/$arch/master]
    --container=USR|SYSROOT             Populate /usr from USR or SYSROOT
    --flatpak-runtime=NAME/ARCH/BRANCH  Use a `flatpak --user` runtime
                                        for --container
                                        [….Games.Platform/$arch/stretch]
    --gl-provider=USR|SYSROOT           Get libGL etc. from USR or SYSROOT
                                        [/]
    --gl-stack=STACK                    Get libGL etc. from STACK/lib/MULTIARCH
                                        instead of --gl-provider, but
                                        still use --gl-provider libc, libX11,
                                        libcapsule if appropriate.
                                        [Same as --gl-provider]
    --x11-provider=auto|container|gl-provider|USR|SYSROOT
                                        Use libX11 etc. from here
                                        [auto]
    --libc-provider=auto|container|gl-provider|USR|SYSROOT
                                        Use glibc from here
                                        [auto]
    --libcapsule-provider=auto|container|gl-provider|USR|SYSROOT
                                        Use libcapsule, libelf from here
                                        [container]
    --libcapsule                        Shortcut for:
                                        --gl-stack=/usr/lib/libcapsule/shims
                                        --libcapsule-provider=/
    --multiarch=TUPLE[,TUPLE…]          Enable architecture(s) by Debian
                                        multiarch tuple
                                        [x86_64-linux-gnu]
    --nvidia-only                       Only capture NVIDIA proprietary
                                        libraries (and possibly GLVND
                                        wrappers), not the rest of the
                                        libGL stack
    --full-mesa                         Use short-term handling for Mesa:
                                        --gl-provider=/
                                        --libcapsule
                                        --libc-provider=auto
                                        --mesa-drivers
                                        --mesa-driver-deps
                                        --x11-provider=auto
                                        --libcapsule-provider=auto
    --proposed-mesa                     Use proposed handling for Mesa:
                                        --gl-provider=/
                                        --libcapsule
                                        --libc-provider=auto
                                        --mesa-drivers
                                        --no-mesa-driver-deps
                                        --x11-provider=auto
                                        --libcapsule-provider=auto
    --proposed-nvidia                   Use proposed handling for binary
                                        blob drivers like NVIDIA:
                                        --gl-provider=/
                                        --x11-provider=container
                                        --libc-provider=container
                                        --libcapsule-provider=container
                                        --no-mesa-drivers
                                        --no-mesa-driver-deps
                                        --nvidia-only
EOF
        exit 0;
    },
) or die "Error parsing command-line options";

if (defined $flatpak_app) {
    die "--flatpak-app and --app are mutually exclusive" if defined $app;
    $app = "$data_home/flatpak/app/$flatpak_app/active/files";
}

if (defined $flatpak_runtime) {
    die "--flatpak-runtime and --container are mutually exclusive"
        if defined $container_tree;
    $container_tree = "$data_home/flatpak/runtime/$flatpak_runtime/active/files";
}

foreach my $tuple (@multiarch_tuples) {
    assert_run_verbose([
        'env', "PKG_CONFIG_PATH=/usr/lib/$tuple/pkgconfig",
        'pkg-config', '--variable=CAPSULE_VERSION_TOOL', 'libcapsule-tools',
    ], '>', \$stdout);
    chomp $stdout;
    $CAPSULE_VERSION_TOOLS{$tuple} = $stdout;
    assert_run_verbose([
        'env', "PKG_CONFIG_PATH=/usr/lib/$tuple/pkgconfig",
        'pkg-config', '--variable=CAPSULE_CAPTURE_LIBS_TOOL',
        'libcapsule-tools',
    ], '>', \$stdout);
    chomp $stdout;
    $CAPSULE_CAPTURE_LIBS_TOOLS{$tuple} = $stdout;
}

$app = "$data_home/flatpak/app/org.debian.packages.mesa_utils/$arch/master/active/files"
    unless defined $app;
$container_tree = "$data_home/flatpak/runtime/net.debian.flatpak.Games.Platform/$arch/stretch/active/files"
    unless defined $container_tree;

assert_run_verbose(['glxinfo'], '>', \$stdout);
diag_multiline($stdout);

if ($stdout =~ /Error: couldn't find RGB GLX visual/) {
    plan skip_all => 'Cannot run glxinfo here';
}

if ($stdout !~ /direct rendering: Yes/) {
    plan skip_all => 'glxinfo reports no direct rendering available';
}

my $tmpdir = File::Temp->newdir(
        TMPDIR => 1, TEMPLATE => 'capsule-test-temp-XXXXXXXX');

my @bwrap = qw(
    bwrap
    --unshare-user
    --unshare-pid
    --unshare-uts --hostname bwrap
    --dev-bind /dev /dev
    --bind /dev/shm /dev/shm
    --proc /proc
    --ro-bind /sys /sys
    --tmpfs /run
    --tmpfs /tmp
    --tmpfs /var
    --tmpfs /var/tmp
    --symlink ../run /var/run
);
push @bwrap, '--bind', $ENV{HOME}, $ENV{HOME};

foreach my $mutable (qw(/etc /var/cache /var/lib)) {
    if (-d "$container_tree$mutable") {
        opendir(my $dir, "$container_tree$mutable");

        while (defined(my $member = readdir $dir)) {
            next if $member eq '.' || $member eq '..';
            next if "$mutable/$member" eq '/etc/group';
            next if "$mutable/$member" eq '/etc/host.conf';
            next if "$mutable/$member" eq '/etc/hosts';
            next if "$mutable/$member" eq '/etc/localtime';
            next if "$mutable/$member" eq '/etc/machine-id';
            next if "$mutable/$member" eq '/etc/passwd';
            next if "$mutable/$member" eq '/etc/resolv.conf';
            next if "$mutable/$member" eq '/var/lib/dbus';
            next if "$mutable/$member" eq '/var/lib/dhcp';
            next if "$mutable/$member" eq '/var/lib/sudo';
            next if "$mutable/$member" eq '/var/lib/urandom';

            no autodie 'readlink';
            my $target = readlink "$container_tree$mutable/$member";

            if (defined $target) {
                push @bwrap, '--symlink', "$target", "$mutable/$member";
            }
            else {
                push @bwrap, '--ro-bind', "$container_tree$mutable/$member", "$mutable/$member";
            }
        }

        closedir($dir);
    }
}

if (-e '/etc/machine-id') {
    push @bwrap, '--ro-bind', '/etc/machine-id', '/etc/machine-id';
    push @bwrap, '--symlink', '/etc/machine-id', '/var/lib/dbus/machine-id';
}
elsif (-e '/var/lib/dbus/machine-id') {
    push @bwrap, '--ro-bind', '/var/lib/dbus/machine-id', '/etc/machine-id';
    push @bwrap, '--symlink', '/etc/machine-id', '/var/lib/dbus/machine-id';
}

if (-e '/etc/localtime') {
    no autodie 'readlink';
    my $target = readlink '/etc/localtime';

    if (defined $target && $target =~ m{^/usr/}) {
        push @bwrap, '--symlink', $target, '/etc/localtime';
    }
    else {
        push @bwrap, '--ro-bind', '/etc/localtime', '/etc/localtime';
    }
}

foreach my $file (qw(resolv.conf host.conf hosts passwd group)) {
    if (-e "/etc/$file") {
        push @bwrap, '--ro-bind', "/etc/$file", "/etc/$file";
    }
}

push @bwrap, bind_usr($container_tree);
$gl_provider_tree = realpath($gl_provider_tree);
my $capsule_prefix = "/run/host$gl_provider_tree";
$capsule_prefix =~ s,/*$,,;
push @bwrap, bind_usr($gl_provider_tree, $capsule_prefix);
push @bwrap, '--setenv', 'CAPSULE_PREFIX', $capsule_prefix;

my @ld_path;

if (defined $app) {
    foreach my $tuple (@multiarch_tuples) {
        push @ld_path, "/app/lib/$tuple";
    }

    push @ld_path, '/app/lib';
    push @bwrap, '--ro-bind', $app, '/app';
    push @bwrap, '--setenv', 'PATH', '/app/bin:/app/sbin:/usr/bin:/bin:/usr/sbin:/sbin';
}

my $overrides_tree = "$tmpdir/overrides";
make_path("$tmpdir/scratch", verbose => 1);
make_path("$tmpdir/overrides", verbose => 1);

foreach my $tuple (@multiarch_tuples) {
    push @ld_path, "/overrides/lib/$tuple";
    make_path("$overrides_tree/lib/$tuple", verbose => 1);
}

if ($libc_provider_tree eq 'auto') {
    diag "Choosing GL provider's libc or container's libc automatically";
    capture_libs($gl_provider_tree, $container_tree, \@multiarch_tuples,
        $overrides_tree, ['soname:libc.so.6'], "$tmpdir/scratch", $capsule_prefix);

    # See what we've got
    foreach my $tuple (@multiarch_tuples) {
        assert_run_verbose([
            'ls', '-l', "$tmpdir/overrides/lib/$tuple",
        ], '>', \$stdout);
        diag_multiline $stdout;
    }

    if (-l "$overrides_tree/lib/$multiarch_tuples[0]/libc.so.6") {
        diag "${ansi_bright}${ansi_green}GL provider's libc appears ".
            "newer${ansi_reset}";
        push @bwrap, use_ldso($gl_provider_tree, $container_tree,
            \@multiarch_tuples, "$tmpdir/scratch");
    }
    else {
        diag "${ansi_bright}${ansi_cyan}Container's libc appears ".
            "newer${ansi_reset}";
    }
}
elsif ($libc_provider_tree =~ m/^(?:gl-provider|host)$/) {
    capture_libs($gl_provider_tree, $container_tree, \@multiarch_tuples,
        $overrides_tree, ['even-if-older:soname:libc.so.6'],
        "$tmpdir/scratch", $capsule_prefix);
    diag "${ansi_bright}${ansi_green}Adding GL provider's libc via /overrides ".
        "as requested${ansi_reset}";
    push @bwrap, use_ldso($gl_provider_tree, $container_tree,
        \@multiarch_tuples, "$tmpdir/scratch");
}
elsif ($libc_provider_tree eq 'container') {
    diag "${ansi_bright}${ansi_cyan}Using container's libc as ".
        "requested${ansi_reset}";
}
else {
    diag "${ansi_bright}${ansi_magenta}Adding ${libc_provider_tree} libc ".
        "via /overrides as requested${ansi_reset}";
    capture_libs($libc_provider_tree, $container_tree, \@multiarch_tuples,
        $overrides_tree, ['even-if-older:soname:libc.so.6'],
        "$tmpdir/scratch", "/run/host/$libc_provider_tree");
    push @bwrap, use_ldso($libc_provider_tree, $container_tree,
        \@multiarch_tuples, "$tmpdir/scratch");
    push @bwrap, bind_usr($libc_provider_tree, "/run/host/$libc_provider_tree");
}

my @dri_path;

if (defined $gl_stack) {
    diag "Using standalone GL stack $gl_stack";
    $gl_stack = realpath($gl_stack);

    push @bwrap, '--ro-bind', $gl_stack, "/run/host$gl_stack";

    foreach my $tuple (@multiarch_tuples) {
        next unless -d "$gl_stack/lib/$tuple";
        opendir(my $dir, "$gl_stack/lib/$tuple");

        while(defined(my $member = readdir $dir)) {
            next if $member eq '.' || $member eq '..';
            my $target = realpath("$gl_stack/lib/$tuple/$member");
            symlink(
                "/run/host$target",
                "$overrides_tree/lib/$tuple/$member");
        }

        closedir($dir);
    }
}
elsif ($nvidia_only) {
    diag "Using NVIDIA-only GL stack from $gl_provider_tree";
    capture_libs($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree, ['no-dependencies:nvidia:'],
        "$tmpdir/scratch", $capsule_prefix);
}
else {
    diag "Using GL stack from $gl_provider_tree";
    capture_libs($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree, ['gl:'],
        "$tmpdir/scratch", $capsule_prefix);

    # If we're using Mesa, we need the DRI drivers too
    if ($mesa_drivers || $mesa_driver_deps) {
        foreach my $tuple (@multiarch_tuples) {
            my $ldso = multiarch_tuple_to_ldso($tuple);
            my $libdir;

            my @search_path = ("/lib/$tuple", "/usr/lib/$tuple");

            if (defined $ldso) {
                # Add /lib64 and /usr/lib64 if applicable
                (undef, $libdir, undef) = File::Spec->splitpath($ldso);
                push @search_path, $libdir, "/usr$libdir";
            }

            foreach my $search (@search_path) {
                if (-d "$gl_provider_tree$search/dri") {
                    diag "Using GL provider's $search/dri";

                    if ($mesa_drivers) {
                        symlink("$capsule_prefix$search/dri",
                            "$overrides_tree/lib/$tuple/dri");
                        push @dri_path, "/overrides/lib/$tuple/dri";
                    }

                    if ($mesa_driver_deps) {
                        capture_libs($gl_provider_tree, $container_tree,
                            \@multiarch_tuples, $overrides_tree,
                            ["only-dependencies:path-match:$search/dri/*"],
                            "$tmpdir/scratch", $capsule_prefix);
                    }
                }
            }
        }
    }
}

# These are libraries that can't have more than one instance in use.
# TODO: We don't currently exclude libelf from being encapsulated.
# Should we? Does it have global state?
if ($libcapsule_tree eq 'auto') {
    diag "Using libcapsule from $gl_provider_tree if newer";
    capture_libs($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree, ['soname:libcapsule.so.0'],
        "$tmpdir/scratch", $capsule_prefix);
}
elsif ($libcapsule_tree eq 'container') {
    diag "${ansi_cyan}Using libcapsule from container as requested${ansi_reset}";
}
elsif ($libcapsule_tree =~ m/^(?:gl-provider|host)$/) {
    diag "${ansi_green}Using libcapsule from GL provider as requested${ansi_reset}";
    capture_libs($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree,
        ['even-if-older:soname:libcapsule.so.0'],
        "$tmpdir/scratch", $capsule_prefix);
}
else {
    diag "${ansi_magenta}Using libcapsule from $libcapsule_tree${ansi_reset}";
    capture_libs($libcapsule_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree,
        ['even-if-older:soname:libcapsule.so.0'],
        "$tmpdir/scratch", "/run/host/$libcapsule_tree");
    push @bwrap, bind_usr($libcapsule_tree, "/run/host/$libcapsule_tree");
}

# These are more libraries that can't have more than one instance in use.
# They also need to appear in libGL.so.1.excluded, so that the capsule
# won't load the GL-provider copy if the container copy is newer.
diag 'Adding singleton libraries from GL provider, if newer...';

# TODO: We don't currently exclude libX11-xcb, libxcb-dri3,
# libxcb-xfixes, libXau, libXdamage, libXdmcp, libXfixes, libXxf86vm
# from being encapsulated. We should be consistent one way or the other
my @XLIBS = qw(X11 X11-xcb xcb xcb-dri2 xcb-dri3 xcb-glx
        xcb-present xcb-sync xcb-xfixes
        Xau Xdamage Xdmcp Xext Xfixes Xxf86vm
        );

if ($x11_provider_tree eq 'auto') {
    diag "Using X libraries from $gl_provider_tree if newer";
    capture_libs($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree,
        [map { "soname-match:lib$_.so.*" } @XLIBS],
        "$tmpdir/scratch", $capsule_prefix);
}
elsif ($x11_provider_tree eq 'container') {
    diag "${ansi_cyan}Using X libraries from container as requested${ansi_reset}";
}
elsif ($x11_provider_tree =~ m/^(?:gl-provider|host)$/) {
    diag "${ansi_green}Using X libraries from GL provider as requested${ansi_reset}";
    capture_libs($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree,
        [map { "even-if-older:soname-match:lib$_.so.*" } @XLIBS],
        "$tmpdir/scratch", $capsule_prefix);
}
else {
    diag "${ansi_magenta}Using X libraries from $x11_provider_tree as requested${ansi_reset}";
    capture_libs($x11_provider_tree, $container_tree,
        \@multiarch_tuples, $overrides_tree,
        [map { "even-if-older:soname-match:lib$_.so.*" } @XLIBS],
        "$tmpdir/scratch", "/run/host/$x11_provider_tree", $ansi_magenta);
    push @bwrap, bind_usr($libcapsule_tree, "/run/host/$x11_provider_tree")
}

foreach my $tuple (@multiarch_tuples) {
    push @ld_path, "/overrides/lib/$tuple";
}

push @bwrap, '--ro-bind', $overrides_tree, '/overrides';

if (@ld_path) {
    push @bwrap, '--setenv', 'LD_LIBRARY_PATH', join(':', @ld_path);
}

if (@dri_path) {
    push @bwrap, '--setenv', 'LIBGL_DRIVERS_PATH', join(':', @dri_path);
}

push @bwrap, '--setenv', 'XDG_RUNTIME_DIR', "/run/user/$<";

# Simplified version of flatpak run --socket=X11
push @bwrap, '--bind', '/tmp/.X11-unix', '/tmp/.X11-unix';
push @bwrap, '--setenv', 'DISPLAY', $ENV{DISPLAY};

foreach my $xauth ($ENV{XAUTHORITY}, "$ENV{HOME}/.Xauthority") {
    if (defined $xauth && -e $xauth) {
        push @bwrap, '--ro-bind', $xauth, "/run/user/$</Xauthority";
        push @bwrap, '--setenv', 'XAUTHORITY', "/run/user/$</Xauthority";
        last;
    }
}

# Simplified version of flatpak run --socket=wayland
my $wayland_display = $ENV{WAYLAND_DISPLAY};
$wayland_display = 'wayland-0' unless defined $wayland_display;
if (exists $ENV{XDG_RUNTIME_DIR} &&
    -S "$ENV{XDG_RUNTIME_DIR}/$wayland_display") {
    push @bwrap, '--bind', "$ENV{XDG_RUNTIME_DIR}/$wayland_display",
        "/run/user/$</$wayland_display";
}

# Simplified version of flatpak run --socket=pulseaudio, modified to
# support the SteamOS system-wide PulseAudio instance
if (exists $ENV{XDG_RUNTIME_DIR} &&
    -S "$ENV{XDG_RUNTIME_DIR}/pulse/native") {
    push @bwrap, '--bind', "$ENV{XDG_RUNTIME_DIR}/pulse/native",
        "/run/user/$</pulse/native";
    push @bwrap, '--setenv', 'PULSE_SERVER', "unix:/run/user/$</pulse/native";
}
elsif (-S "/var/run/pulse/native") {
    push @bwrap, '--bind', "/var/run/pulse/native",
        "/run/user/$</pulse/native";
    push @bwrap, '--setenv', 'PULSE_SERVER', "unix:/run/user/$</pulse/native";
}

# Simplified version of flatpak run --socket=system-bus
if (exists $ENV{DBUS_SYSTEM_BUS_ADDRESS} &&
    $ENV{DBUS_SYSTEM_BUS_ADDRESS} =~ m/^unix:path=([^,;]*)/ &&
    -S $1) {
    push @bwrap, '--bind', $1, '/var/run/dbus/system_bus_socket';
    push @bwrap, '--unsetenv', 'DBUS_SYSTEM_BUS_ADDRESS';
}
elsif (-S '/var/run/dbus/system_bus_socket') {
    push @bwrap, '--bind', '/var/run/dbus/system_bus_socket',
        '/run/dbus/system_bus_socket';
}

# Simplified version of flatpak run --socket=session-bus
if (exists $ENV{DBUS_SESSION_BUS_ADDRESS} &&
    $ENV{DBUS_SESSION_BUS_ADDRESS} =~ m/^unix:path=([^,;]*)/ &&
    -S $1) {
    push @bwrap, '--bind', $1, "/run/user/$</bus";
    push @bwrap, '--setenv', 'DBUS_SESSION_BUS_ADDRESS',
        "unix:path=/run/user/$</bus";
}
elsif (exists $ENV{XDG_RUNTIME_DIR} &&
    -S "$ENV{XDG_RUNTIME_DIR}/bus") {
    push @bwrap, '--bind', "$ENV{XDG_RUNTIME_DIR}/bus", "/run/user/$</bus";
    push @bwrap, '--setenv', 'DBUS_SESSION_BUS_ADDRESS',
        "unix:path=/run/user/$</bus";
}
# ... else hope it's unix:abstract=... which will work because we don't
# unshare networking

# See what we've got
foreach my $tuple (@multiarch_tuples) {
    foreach my $dir ('overrides') {
        next unless -d "$tmpdir/$dir/lib/$tuple";
        assert_run_verbose([
            'ls', '-l', "$tmpdir/$dir/lib/$tuple",
        ], '>', \$stdout);
        diag_multiline $stdout;
    }
}

if (@ARGV) {
    run_ok([@bwrap, @ARGV]);
}
else {
    run_ok([@bwrap, 'glxinfo'], '>', \$stdout);
    diag_multiline $stdout;
    like($stdout, qr{direct rendering: Yes});
}

done_testing;

# vim:set sw=4 sts=4 et:
