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

The GL stack is made available in the container as F</gl/lib/*>.
This can either be the host system's GL stack (provided as symbolic
links into F</gl-provider/{usr/,}lib*>), a different sysroot's GL stack (again
provided as symbolic links into F</gl-provider/{usr/,}lib*>), a
standalone GL stack (for example proprietary binaries), or a libcapsule
shim that will load the real GL stack from F</gl-provider>.

F</usr>, F</bin>, F</sbin>, F</lib*>, F</etc/alternatives> and
F</etc/ld.so.cache> from the host system or a sysroot are always mounted
at F</gl-provider>.

By default, glibc is taken from either the GL provider or the container,
whichever has the newer version. It can also be taken from a third tree
which will be mounted at F</libc-provider>. If the version in use is not
the container's, F</updates/lib/*> is populated with symbolic links into
F</gl-provider/lib*> or F</libc-provider/lib*> as appropriate.

Similarly, libcapsule and libelf are taken from either the GL provider
or the container, or can be taken from a third tree which will be
mounted at F</libcapsule-provider>.

Other core graphics libraries (libX11, libxcb, etc.) are taken from either
the GL provider, the container, or a third tree that will be mounted
at F</x11-provider>.

An application "payload" is mounted on F</app>, as if for Flatpak.

The B<LD_LIBRARY_PATH> for the child process is made to include
library directories from F</app>, F</gl> and F</updates>, but not
F</gl-provider>.

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
B<--libcapsule-provider>, B<--x11-provider>. It will be mounted at
F</gl-provider>. The default is F</>, meaning we use the host system
as the OpenGL provider.

=item --gl-stack=I<TREE>

A pre-prepared OpenGL stack, with F<lib/I<TUPLE>> subdirectories
containing F<libGLX.so.1> and related libraries. It will be mounted
at F</gl> and added to the B<LD_LIBRARY_PATH>.

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
B<--gl-provider=/> B<--gl-stack=none> B<--libc-provider=container>
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

=item get_lib_implementation(I<TUPLE>, I<SONAME>, I<TREE>)

Return the absolute path of the I<TUPLE> library I<SONAME> in the tree I<TREE>,
which may either be a complete sysroot or just the contents of a merged
F</usr> (see C<bind_usr>). The absolute path is relative to the sysroot,
or the root that contains the merged F</usr> as its F</usr>.

=cut

my %CAPSULE_VERSION_TOOLS;

sub get_lib_implementation {
    my ($tuple, $soname, $tree) = @_;
    my $output;
    my $sysroot = $tree;

    if (-d "$tree/usr") {
        run([$CAPSULE_VERSION_TOOLS{$tuple}, $soname, $sysroot],
            '>', \$output);
    }
    else {
        $sysroot = '/tmp/sysroot';
        run([qw(bwrap --bind / / --dev-bind /dev /dev --tmpfs /tmp
            --symlink usr/lib /tmp/sysroot/lib
            --symlink usr/lib64 /tmp/sysroot/lib64
            --symlink usr/etc /tmp/sysroot/etc
            --symlink usr/var /tmp/sysroot/var
            --bind), $tree, '/tmp/sysroot/usr',
            $CAPSULE_VERSION_TOOLS{$tuple}, $soname, '/tmp/sysroot'],
            '>', \$output);
    }

    chomp $output;

    if ($output !~ m/^\S+\s+\S+\s+\S+\s+(\S+)$/) {
        return undef;
    }

    $output = $1;
    $output =~ s{^\Q$sysroot\E}{};
    $output =~ s{/*}{/};

    if (!run_in_container($tree, ['readlink', '-f', $output], '>', \$output)) {
        return undef;
    }

    chomp $output;
    return $output;
}

=item multiarch_tuple_to_ldso(I<TUPLE>)

Return the B<ld.so>(8) implementation for a Debian multiarch tuple.
For example,
C<multiarch_tuple_to_ldso('x86_64-linux-gnu')> returns
F</lib64/ld-linux-x86-64.so.2>.

=cut

sub multiarch_tuple_to_ldso {
    my $tuple = shift;

    # We only really care about x86 here, but might as well be a bit more
    # complete. See https://sourceware.org/glibc/wiki/ABIList
    if ($tuple =~ m/^(i386|alpha|sh\d+|sparc)-linux-gnu$/) {
        return '/lib/ld-linux.so.2';
    }
    elsif ($tuple =~ m/^(x86_64)-linux-gnu$/) {
        my $machine = $1;
        $machine =~ tr/_/-/;
        return "/lib64/ld-linux-$machine.so.2";
    }
    elsif ($tuple =~ m/^(aarch64)-linux-gnu$/) {
        return "/lib/ld-linux-$1.so.1";
    }
    elsif ($tuple eq 'arm-linux-gnueabihf') {
        return '/lib/ld-linux-armhf.so.3';
    }
    elsif ($tuple eq 'arm-linux-gnueabi') {
        return '/lib/ld-linux.so.3';
    }
    elsif ($tuple eq 'sparc64-linux-gnu') {
        return '/lib64/ld-linux.so.2';
    }
    elsif ($tuple =~ m/^(hppa|m68k|powerpc|s390)-linux-gnu$/) {
        return '/lib64/ld.so.1';
    }
    elsif ($tuple =~ m/^(s390x|powerpc64)-linux-gnu$/) {
        return '/lib/ld64.so.1';
    }
    elsif ($tuple eq 'powerpc64le-linux-gnu') {
        return '/lib/ld64.so.2';
    }
    elsif ($tuple eq 'x86_64-linux-gnux32') {
        return '/libx32/ld-linux-x32.so.2';
    }
    elsif ($tuple =~ /^mips/) {
        # Could be /lib{,32,64}/ld{,-linux-mipsn8}.so.1
        #              ^ o32,n32,n64     ^ classic NaN, NaN2008
        die "Is $tuple o32 or n32 mips?";
    }
    else {
        return undef;
    }
}

=item use_libc(I<LIBC_PROVIDER>, I<CONTAINER>, I<TUPLES>, I<DEST>, I<MOUNT>)

Populate I<DEST> with symbolic links to the B<glibc>
in I<TREE>, assuming that I<TREE> will be mounted on I<MOUNT>,
such that I<DEST> can be mounted at F</updates>.

For example, on x86_64 systems, the returned directory will contain
symbolic links F<lib/x86_64-linux-gnu/ld-linux-x86-64.so> →
I<MOUNT>F</lib/x86_64-linux-gnu/ld-linux-x86-64.so>
and
F<lib/x86_64-linux-gnu/libc.so.6> →
I<MOUNT>F</lib/x86_64-linux-gnu/libc.so.6>, among others.

I<TUPLES> is a reference to an array of Debian multiarch tuples.

Return the necessary B<bwrap>(1) arguments to override the container's
B<ld.so>(1) with the one from the GL provider.

=cut

sub use_libc {
    my $libc_provider_tree = shift;
    my $container_tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $dest = shift;
    my $mount = shift;
    my @bwrap;

    diag "Capturing GL provider libc into $dest";

    make_path("$dest/lib", verbose => 1);
    run(['ln', '-fns', $mount, "$dest/LIBC-PROVIDER"]);

    foreach my $tuple (@multiarch_tuples) {
        make_path("$dest/lib/$tuple", verbose => 1);

        my $ldso = multiarch_tuple_to_ldso($tuple);
        my $libdir;

        if (defined $ldso) {
            (undef, $libdir, undef) = File::Spec->splitpath($ldso);
        }

        foreach my $lib (qw(BrokenLocale SegFault anl c cidn crypt dl m
            memusage mvec nsl pcprofile pthread resolv rt thread_db util)) {
            SEARCH: foreach my $search ("/lib/$tuple", $libdir) {
                next SEARCH unless defined $search;
                next SEARCH unless -d "$libc_provider_tree$search";

                opendir(my $dir, "$libc_provider_tree$search");
                SONAME: while (defined(my $soname = readdir $dir)) {
                    next SONAME unless $soname =~ m/^lib\Q$lib\E\.so\.[0-9.]+$/;

                    # Get the right implementation respecting hwcaps,
                    # and make it absolute.
                    my $real = get_lib_implementation($tuple, $soname, $libc_provider_tree);
                    die "Cannot resolve $search/$soname in $libc_provider_tree" unless defined $real;

                    { no autodie; unlink("$dest/lib/$tuple/$soname"); }
                    diag "Creating symbolic link $dest/lib/$tuple/$soname → $mount$real";
                    symlink("$mount$real", "$dest/lib/$tuple/$soname");
                }
                closedir($dir);
            }
        }

        if (defined $ldso) {
            my $has_usr = (-d "$container_tree/usr");
            my %realpaths;

            SEARCH: foreach my $search ("/lib/$tuple", $libdir) {
                next SEARCH unless defined $search;
                next SEARCH unless -d "$container_tree$search";

                opendir(my $dir, "$container_tree$search");
                MEMBER: while (defined(my $member = readdir $dir)) {
                    next MEMBER unless $member =~ m/^ld-.*\.so\./;

                    my $real;
                    run_in_container($container_tree,
                        ['readlink', '-f', "$search/$member"],
                        '>', \$real)
                        or die "Cannot resolve $search/$member in $container_tree";
                    chomp $real;

                    $realpaths{$real} = 1;
                }
                closedir($dir);
            }

            foreach my $real (keys %realpaths) {
                if ($has_usr) {
                    diag "Adding bind-mount of $ldso over /usr$real because /lib{,64} will be symlinks";
                    push @bwrap, '--ro-bind', $ldso, "/usr$real";
                }
                else {
                    diag "Adding bind-mount of $ldso over $real";
                    push @bwrap, '--ro-bind', $ldso, $real;
                }
            }
        }
    }

    return @bwrap;
}

=item capture_libs_if_newer(I<PROVIDER>, I<CONTAINER>|B<undef>, I<TUPLES>, I<DEST>, I<LIBS>, I<MOUNT>, I<COLOUR>)

Populate I<DEST>F</lib/>I<TUPLE> with symbolic links to the libraries
in I<PROVIDER>, assuming that I<PROVIDER> will be mounted on
I<MOUNT>, such that I<DEST>F</lib/>I<TUPLE> can be added
to the B<LD_LIBRARY_PATH>.

If I<CONTAINER> is B<undef>, this is done for all libraries listed.
If I<CONTAINER> is defined, libraries that appear to be strictly newer
in I<CONTAINER> are skipped.

I<TUPLES> is a reference to an array of Debian multiarch tuples.

I<LIBS> is a list of stem names of libraries (C<GL1>, C<stdc++>, C<z>),
or references to regular expressions matching an entire library basename
(C<libnvidia-.*\.so\..*>).

I<COLOUR> is an ANSI escape used to colour-code output from I<PROVIDER>,
or empty.

=cut

sub capture_libs_if_newer {
    my $provider_tree = shift;
    my $container_tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $dest = shift;
    my @libs = @{ shift() };
    my $mount = shift;
    my $provider_colour = shift;

    foreach my $tuple (@multiarch_tuples) {
        make_path("$dest/lib/$tuple", verbose => 1);

        my $ldso = multiarch_tuple_to_ldso($tuple);
        my $libdir;

        my @search_path = ("/lib/$tuple", "/usr/lib/$tuple");

        if (defined $ldso) {
            (undef, $libdir, undef) = File::Spec->splitpath($ldso);
            push @search_path, $libdir, "/usr$libdir";
        }

        foreach my $lib (@libs) {
            SEARCH: foreach my $search (@search_path) {
                next SEARCH unless -d "$provider_tree$search";

                opendir(my $dir, "$provider_tree$search");
                SONAME: while (defined(my $soname = readdir $dir)) {
                    if (ref $lib eq 'Regexp') {
                        # If it's qr{some regexp} it matches the whole thing
                        next SONAME unless $soname =~ m/^$lib$/;
                    }
                    else {
                        # Otherwise it's just a basename, and we take any
                        # SONAME, assuming a single integer version
                        next SONAME unless $soname =~ m/^lib\Q$lib\E\.so\.[0-9]+$/;
                    }

                    # We need to get the correct implementation rather
                    # than just symlinking to the SONAME we found,
                    # because there might be a "better" version in
                    # a hwcap subdirectory, and at least for
                    # libnvidia-tls.so, it matters - loading the
                    # non-TLS version somehow corrupts the TLS used
                    # by libc itself.
                    my $provider_impl = get_lib_implementation($tuple,
                        $soname, $provider_tree);

                    if (! defined $provider_impl) {
                        # capsule-version might not be able to find the
                        # library if it's just a symlink to some library
                        # with a different SONAME, like libGLX_indirect.so.0
                        $provider_impl = get_lib_implementation($tuple,
                            "$search/$soname", $provider_tree);
                    }

                    if (! defined $provider_impl) {
                        die "Unable to resolve $search/$soname in ".
                            $provider_tree;
                    }

                    if (defined $container_tree) {
                        my $container_impl = get_lib_implementation($tuple,
                            $soname, $container_tree);

                        my $provider_version = $provider_impl;
                        $provider_version =~ s/.*\.so\.//;

                        my $container_version = $container_impl;
                        $container_version =~ s/.*\.so\.//
                            if defined $container_version;

                        if (! defined $container_impl) {
                            diag "${provider_colour}Using $provider_tree ".
                                "$tuple $soname '$provider_impl' because ".
                                "container does not have that ".
                                "library${ansi_reset}";
                        }
                        elsif ($container_version eq $provider_version) {
                            # The libraries for which we call this function
                            # are fairly closely tied to the GL provider,
                            # so if in doubt we prefer the GL provider
                            diag "${provider_colour}Using $provider_tree ".
                                "$tuple $soname '$provider_impl'".
                                "${ansi_reset} because container's version ".
                                "appears to be the same";
                        }
                        elsif (versioncmp($container_version,
                                $provider_version) > 0) {
                            diag "${ansi_cyan}Using container's ".
                                "$tuple $soname '$container_impl' because ".
                                "$provider_tree $provider_impl appears ".
                                "older${ansi_reset}";
                            next SONAME;
                        }
                        else {
                            diag "${provider_colour}Using $provider_tree ".
                                "$tuple $soname '$provider_impl' because ".
                                "container's $container_impl appears ".
                                "older${ansi_reset}";
                        }
                    }
                    else {
                        diag "${provider_colour}Using $provider_tree ".
                            "$tuple $soname unconditionally${ansi_reset}";
                    }

                    { no autodie; unlink("$dest/lib/$tuple/$soname"); }
                    diag "Creating symbolic link $dest/lib/$tuple/$soname → ".
                        "$mount$provider_impl";
                    symlink("$mount$provider_impl", "$dest/lib/$tuple/$soname");
                }
                closedir($dir);
            }
        }
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
my $gl_stack = 'greedy';
my $arch = 'x86_64';
my $libc_provider_tree = 'auto';
my $libcapsule_tree = 'container';
my $x11_provider_tree = 'auto';

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
    },
    'libc-provider=s' => \$libc_provider_tree,
    'x11-provider=s' => \$x11_provider_tree,
    'multiarch=s' => sub {
        @multiarch_tuples = split /[\s,]+/, $_[1];
    },
    'proposed-mesa' => sub {
        $gl_provider_tree = '/';
        $gl_stack = '/usr/lib/libcapsule/shims';
        $libcapsule_tree = '/';
        $libc_provider_tree = 'auto';
        $x11_provider_tree = 'auto';
    },
    'proposed-nvidia' => sub {
        $gl_provider_tree = '/';
        $gl_stack = 'nvidia';
        $libcapsule_tree = 'container';
        $libc_provider_tree = 'container';
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
    --gl-stack=greedy|mesa|nvidia|STACK If STACK is specified, get libGL
                                        etc. from STACK/lib/MULTIARCH
                                        instead of --gl-provider, but
                                        still use --gl-provider libc, libX11,
                                        libcapsule if appropriate.
                                        Otherwise get appropriate libraries
                                        for Mesa, NVIDIA, or everything
                                        we might possibly need from
                                        --gl-provider
                                        [greedy]
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
    --proposed-mesa                     Use proposed handling for Mesa:
                                        --gl-provider=/
                                        --libcapsule
                                        --libc-provider=auto
                                        --x11-provider=auto
                                        --libcapsule-provider=auto
    --proposed-nvidia                   Use proposed handling for binary
                                        blob drivers like NVIDIA:
                                        --gl-provider=/
                                        --gl-stack=nvidia
                                        --x11-provider=container
                                        --libc-provider=container
                                        --libcapsule-provider=container
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
    run_verbose([
        'env', "PKG_CONFIG_PATH=/usr/lib/$tuple/pkgconfig",
        'pkg-config', '--variable=CAPSULE_VERSION_TOOL', 'libcapsule-tools',
    ], '>', \$stdout);
    chomp $stdout;
    $CAPSULE_VERSION_TOOLS{$tuple} = $stdout;
}

$app = "$data_home/flatpak/app/org.debian.packages.mesa_utils/$arch/master/active/files"
    unless defined $app;
$container_tree = "$data_home/flatpak/runtime/net.debian.flatpak.Games.Platform/$arch/stretch/active/files"
    unless defined $container_tree;

run_verbose(['glxinfo'], '>', \$stdout);
diag_multiline($stdout);

if ($stdout =~ /Error: couldn't find RGB GLX visual/) {
    plan skip_all => 'Cannot run glxinfo here';
}

if ($stdout !~ /direct rendering: Yes/) {
    plan skip_all => 'glxinfo reports no direct rendering available';
}

my $gl_provider_libc_so = get_lib_implementation($multiarch_tuples[0],
    'libc.so.6', $gl_provider_tree);
my $container_libc_so = get_lib_implementation($multiarch_tuples[0],
    'libc.so.6', $container_tree);

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
    --tmpfs /var/tmp
    --symlink ../run /var/run
);
push @bwrap, '--bind', $ENV{HOME}, $ENV{HOME};

foreach my $mutable (qw(/etc /var)) {
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
push @bwrap, bind_usr($gl_provider_tree, '/gl-provider');
push @bwrap, '--setenv', 'CAPSULE_PREFIX', '/gl-provider';

my @ld_path;

if (defined $app) {
    foreach my $tuple (@multiarch_tuples) {
        push @ld_path, "/app/lib/$tuple";
    }

    push @ld_path, '/app/lib';
    push @bwrap, '--ro-bind', $app, '/app';
    push @bwrap, '--setenv', 'PATH', '/app/bin:/app/sbin:/usr/bin:/bin:/usr/sbin:/sbin';
}

my $gl_provider_libc;

# We can't look for .so.VERSION here because libc's real name ends with
# VERSION.so. Take the basname instead.
my (undef, undef, $gl_provider_libc_version) =
    File::Spec->splitpath($gl_provider_libc_so);
my (undef, undef, $container_libc_version) =
    File::Spec->splitpath($container_libc_so);

my $updates_tree = "$tmpdir/updates";
make_path("$tmpdir/updates", verbose => 1);

if ($libc_provider_tree eq 'auto') {
    if (versioncmp($container_libc_version, $gl_provider_libc_version) <= 0) {
        # We need to parachute in the GL provider's libc instead of the
        # container's, because the GL provider's libX11, libGL etc. would
        # be within their rights to use newer libc features; we also need
        # the GL provider's ld.so, because old ld.so can't necessarily load
        # new libc successfully
        diag "${ansi_bright}${ansi_green}Adding GL provider's libc ".
            "$gl_provider_libc_version via /updates because container's ".
            "$container_libc_version appears older or equal${ansi_reset}";
        push @bwrap, use_libc($gl_provider_tree, $container_tree,
            \@multiarch_tuples, $updates_tree, '/gl-provider');
    }
    else {
        diag "${ansi_bright}${ansi_cyan}Using container's libc ".
            "$container_libc_version because GL provider's ".
            "$gl_provider_libc_version appears older${ansi_reset}";
    }
}
elsif ($libc_provider_tree =~ m/^(?:gl-provider|host)$/) {
    diag "${ansi_bright}${ansi_cyan}Adding GL provider's libc via /updates ".
        "as requested${ansi_reset}";
    push @bwrap, use_libc($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $updates_tree, '/gl-provider');
}
elsif ($libc_provider_tree eq 'container') {
    diag "${ansi_bright}${ansi_cyan}Using container's libc as ".
        "requested${ansi_reset}";
}
else {
    diag "${ansi_bright}${ansi_magenta}Adding ${libc_provider_tree} libc ".
        "via /updates as requested${ansi_reset}";
    push @bwrap, use_libc($libc_provider_tree, $container_tree,
        \@multiarch_tuples, $updates_tree, '/libc-provider');
    push @bwrap, bind_usr($libc_provider_tree, '/libc-provider');
}

my @dri_path;

if (defined $gl_stack && $gl_stack !~ m/^(?:greedy|mesa|nvidia)$/) {
    diag "Using standalone GL stack $gl_stack";

    push @bwrap, '--ro-bind', $gl_stack, '/gl';

    foreach my $tuple (@multiarch_tuples) {
        push @ld_path, "/gl/lib/$tuple";
    }
}
else {
    my $gl_provider_gl = "$tmpdir/gl";

    my @libs = (
        qw(EGL GL GLESv1_CM GLESv2 GLX GLdispatch glx),
        qr{lib(EGL|GLESv1_CM|GLESv2|GLX|drm|vdpau)_.*\.so\.[0-9]+},
    );

    push @libs, qw(
        Xau Xdamage Xdmcp Xext Xfixes Xxf86vm
        drm gbm glapi wayland-client wayland-server
        xcb-dri2 xcb-dri3 xcb-present xcb-sync xcb-xfixes xshmfence
    ) if $gl_stack =~ m/^(?:greedy|mesa)$/;

    push @libs, (
        qw(cuda nvcuvid vdpau),
        # We allow any extension for these, not just a single integer
        qr{libnvidia-.*\.so\..*},
    ) if $gl_stack =~ m/^(?:greedy|nvidia)$/;

    diag "Using GL stack from $gl_provider_tree";
    capture_libs_if_newer($gl_provider_tree, undef,
        \@multiarch_tuples, $gl_provider_gl, \@libs,
        '/gl-provider', $ansi_green);

    if ($gl_stack =~ m/^(?:greedy|mesa)$/) {
        diag "Updating libraries from $gl_provider_tree if necessary";
        capture_libs_if_newer($gl_provider_tree, $container_tree,
            \@multiarch_tuples, $gl_provider_gl, [
                qw(bsd edit elf expat ffi gcc_s
                ncurses pciaccess sensors stdc++ tinfo z),
                qr{libLLVM-.*\.so\.[0-9]+},
            ], '/gl-provider', $ansi_green);
    }

    foreach my $tuple (@multiarch_tuples) {
        my $ldso = multiarch_tuple_to_ldso($tuple);
        my $libdir;

        my @search_path = ("/lib/$tuple", "/usr/lib/$tuple");

        if (defined $ldso) {
            (undef, $libdir, undef) = File::Spec->splitpath($ldso);
            push @search_path, $libdir, "/usr$libdir";
        }

        foreach my $search (@search_path) {
            if (-d "$gl_provider_tree$search/dri") {
                diag "Using GL provider's $search/dri";
                symlink("/gl-provider$search/dri",
                    "$gl_provider_gl/lib/$tuple/dri");
                push @dri_path, "/gl/lib/$tuple/dri";
            }
        }
    }

    foreach my $tuple (@multiarch_tuples) {
        push @ld_path, "/gl/lib/$tuple";
    }

    push @bwrap, '--ro-bind', $gl_provider_gl, '/gl';
}

# These are libraries that can't have more than one instance in use.
# TODO: We don't currently exclude libelf from being encapsulated.
# Should we? Does it have global state?
if ($libcapsule_tree eq 'auto') {
    diag "Using libcapsule from $gl_provider_tree if newer";
    capture_libs_if_newer($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $updates_tree, [qw(capsule elf)],
        '/gl-provider', $ansi_green);
}
elsif ($libcapsule_tree eq 'container') {
    diag "${ansi_cyan}Using libcapsule from container as requested${ansi_reset}";
}
elsif ($libcapsule_tree =~ m/^(?:gl-provider|host)$/) {
    diag "${ansi_green}Using libcapsule from GL provider as requested${ansi_reset}";
    capture_libs_if_newer($gl_provider_tree, undef,
        \@multiarch_tuples, $updates_tree, [qw(capsule elf)],
        '/gl-provider', $ansi_green);
}
else {
    diag "Using libcapsule from $libcapsule_tree";
    capture_libs_if_newer($libcapsule_tree, undef,
        \@multiarch_tuples, $updates_tree, [qw(capsule elf)],
        '/libcapsule-provider', $ansi_magenta);
    push @bwrap, bind_usr($libcapsule_tree, '/libcapsule-provider');
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
    capture_libs_if_newer($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $updates_tree, \@XLIBS,
        '/gl-provider', $ansi_green);
}
elsif ($x11_provider_tree eq 'container') {
    diag "${ansi_cyan}Using X libraries from container as requested${ansi_reset}";
}
elsif ($x11_provider_tree =~ m/^(?:gl-provider|host)$/) {
    diag "${ansi_green}Using X libraries from GL provider as requested${ansi_reset}";
    capture_libs_if_newer($gl_provider_tree, undef,
        \@multiarch_tuples, $updates_tree, \@XLIBS,
        '/gl-provider', $ansi_green);
}
else {
    diag "${ansi_magenta}Using X libraries from $x11_provider_tree as requested${ansi_reset}";
    capture_libs_if_newer($x11_provider_tree, undef,
        \@multiarch_tuples, $updates_tree, \@XLIBS,
        '/x11-provider', $ansi_magenta);
    push @bwrap, bind_usr($libcapsule_tree, '/x11-provider');
}

foreach my $tuple (@multiarch_tuples) {
    push @ld_path, "/updates/lib/$tuple";
}

push @bwrap, '--ro-bind', $updates_tree, '/updates';

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
