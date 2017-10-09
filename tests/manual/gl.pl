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

=item Using libcapsule

    $ ( cd libGL-proxy && ./configure --with-runtime-tree=/gl-provider \
        --with-search-tree=/ --prefix=/gl \
        --libdir='${exec_prefix}/x86_64-linux-gnu' )
    $ make -C libGL-proxy
    $ make -C libGL-proxy install DESTDIR=$HOME/capsulized-libgl
    $ gl.pl --gl-stack=$HOME/capsulized-libgl/gl

Actually use libcapsule. This currently works with a SteamOS brewmaster
(Debian jessie-based) host, a Steam Runtime container, Mesa graphics,
and glxinfo or glxgears as the app, but crashes when the graphics
driver is NVIDIA, the container is Debian stretch or the app is
OpenArena.

=back

=head1 OPTIONS

=over

=item --app=I<APP>

Mount I<APP> on F</app>

=item --capsule-version-tool=I<EXECUTABLE>

Run I<EXECUTABLE> as B<capsule-version>(1)

=item --container=I<TREE>

A complete sysroot or a merged F</usr> for the container. Most libraries
will come from here.

=item --flatpak-app=I<ID>/I<ARCH>/I<BRANCH>

Assume that I<ID>/I<ARCH>/I<BRANCH> has been installed per-user with
C<flatpak --user install ...>, and use it as the B<--app>.
The default is F<org.debian.packages.mesa_utils/x86_64/scout_beta>.

=item --flatpak-runtime=I<ID>/I<ARCH>/I<BRANCH>

Assume that I<ID>/I<ARCH>/I<BRANCH> has been installed per-user with
C<flatpak --user install ...>, and use it as the B<--container>.
The default is F<com.valvesoftware.SteamRuntime.Platform/x86_64/scout_beta>.
Another good value is F<net.debian.flatpak.Games.Platform/x86_64/stretch>.

=item --gl-provider=I<TREE>

A complete sysroot or a merged F</usr> for the OpenGL provider.
The OpenGL implementation will come from here, along with whatever
supporting libraries are required. It will be mounted at
F</gl-provider>.

=item --gl-stack=I<TREE>

A pre-prepared OpenGL stack, with F<lib/I<TUPLE>> subdirectories
containing F<libGLX.so.1> and related libraries. It will be mounted
at F</gl> and added to the B<LD_LIBRARY_PATH>.

=item --multiarch=I<TUPLE>[,I<TUPLE>...]

A list of Debian multiarch tuples to enable. The default is
C<x86_64-linux-gnu>.

=back

=cut

my $ansi_bright = "";
my $ansi_cyan = "";
my $ansi_green = "";
my $ansi_reset = "";

if (-t STDOUT) {
    # Mnemonic: cyan is the container, green is the GL provider
    $ansi_bright = "\e[1m";
    $ansi_cyan = "\e[36m";
    $ansi_green = "\e[32m";
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

my $CAPSULE_VERSION_TOOL = $ENV{CAPSULE_VERSION_TOOL};

=item get_lib_implementation(I<SONAME>, I<TREE>)

Return the absolute path of the library I<SONAME> in the tree I<TREE>,
which may either be a complete sysroot or just the contents of a merged
F</usr> (see C<bind_usr>). The absolute path is relative to the sysroot,
or the root that contains the merged F</usr> as its F</usr>.

=cut

sub get_lib_implementation {
    my ($soname, $tree) = @_;
    my $output;
    my $sysroot = $tree;

    if (-d "$tree/usr") {
        run([$CAPSULE_VERSION_TOOL, $soname, $sysroot], '>', \$output);
    }
    else {
        $sysroot = '/tmp/sysroot';
        run([qw(bwrap --bind / / --dev-bind /dev /dev --tmpfs /tmp
            --symlink usr/lib /tmp/sysroot/lib
            --symlink usr/lib64 /tmp/sysroot/lib64
            --symlink usr/etc /tmp/sysroot/etc
            --symlink usr/var /tmp/sysroot/var
            --bind), $tree, '/tmp/sysroot/usr',
            $CAPSULE_VERSION_TOOL, $soname, '/tmp/sysroot'], '>', \$output);
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

=item make_container_libc_overridable(I<TMPDIR>, I<TREE>, I<TUPLES>)

Create a copy of I<TREE> in I<TMPDIR> and modify it so that all
libraries built by B<glibc>, including the dynamic linker B<ld.so>(8),
are installed to F</libc/lib/I<TUPLE>/>. This allows the Standard C
library to be overridden by mounting a different libc on F</libc>.

I<TUPLES> is a reference to an array of the Debian multiarch tuples
available in I<TREE>.

Return the copy.

=cut

sub make_container_libc_overridable {
    my $tmpdir = shift;
    my $old_tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $new_tree = "$tmpdir/tree";

    diag 'Making container libc overridable';

    if (!run(['cp', '-a', $old_tree, $new_tree], '>&2')) {
        BAIL_OUT "Unable to copy $old_tree";
    }

    make_path("$new_tree/libc/lib", verbose => 1);
    make_path("$new_tree/etc", verbose => 1);
    make_path("$new_tree/var/tmp", verbose => 1);
    run(['touch', "$new_tree/libc/CONTAINER"]);
    run(['touch', "$new_tree/etc/machine-id"]);
    run(['touch', "$new_tree/etc/passwd"]);

    foreach my $tuple (@multiarch_tuples) {
        make_path("$new_tree/libc/lib/$tuple", verbose => 1);

        my $ldso = multiarch_tuple_to_ldso($tuple);
        my $libdir;

        if (defined $ldso) {
            (undef, $libdir, undef) = File::Spec->splitpath($ldso);
        }

        foreach my $lib (qw(BrokenLocale SegFault anl c cidn crypt dl m
            memusage mvec nsl pcprofile pthread resolv rt thread_db util)) {
            SEARCH: foreach my $search ("/lib/$tuple", $libdir) {
                next SEARCH unless defined $search;
                next SEARCH unless -d "$new_tree$search";

                opendir(my $dir, "$new_tree$search");
                SONAME: while (defined(my $soname = readdir $dir)) {
                    next SONAME unless $soname =~ m/^lib\Q$lib\E\.so\.[0-9.]+$/;

                    # Get the right implementation respecting hwcaps,
                    # and make it absolute.
                    # TODO: This doesn't work for foreign architectures:
                    # we would have to run a version of capsule-version
                    # that is appropriate for the foreign architecture.
                    my $real = get_lib_implementation($soname, $new_tree);
                    die "Cannot resolve $search/$soname in $new_tree" unless defined $real;

                    if ($real !~ m{^/libc}) {
                        if (! -d "$new_tree/usr") {
                            $real =~ s{^/usr/}{/};
                        }
                        diag("Renaming $new_tree$real → $new_tree/libc/lib/$tuple/$soname");
                        rename("$new_tree$real",
                            "$new_tree/libc/lib/$tuple/$soname");
                    }
                    { no autodie; unlink("$new_tree$search/$soname"); }
                    diag "Creating symbolic link $new_tree$search/$soname → /libc/lib/$tuple/$soname";
                    symlink("/libc/lib/$tuple/$soname", "$new_tree$search/$soname");
                }
                closedir($dir);
            }
        }

        if (defined $ldso) {
            SEARCH: foreach my $search ("/lib/$tuple", $libdir) {
                next SEARCH unless defined $search;
                next SEARCH unless -d "$new_tree$search";

                opendir(my $dir, "$new_tree$search");
                MEMBER: while (defined(my $member = readdir $dir)) {
                    next MEMBER unless $member =~ m/^ld-.*\.so\./;

                    my $value = readlink "$new_tree$search/$member";
                    next MEMBER if ($value =~ m{^/libc/});

                    my $real;
                    run_in_container($new_tree,
                        ['readlink', '-f', "$search/$member"],
                        '>', \$real)
                        or die "Cannot resolve $search/$member in $new_tree";
                    chomp $real;

                    if ($real !~ m{^/libc/}) {
                        if (! -d "$new_tree/usr") {
                            $real =~ s{^/usr/}{/};
                        }
                        diag("Renaming $new_tree$real → $new_tree/libc/lib/$tuple/$member");
                        rename("$new_tree$real",
                            "$new_tree/libc/lib/$tuple/$member");
                    }
                    { no autodie; unlink("$new_tree$search/$member"); }
                    { no autodie; unlink("$new_tree$ldso"); }
                    diag "Creating symbolic link $new_tree$search/$member → /libc/lib/$tuple/$member";
                    symlink("/libc/lib/$tuple/$member", "$new_tree$search/$member");
                    diag "Creating symbolic link $new_tree$ldso → /libc/lib/$tuple/$member";
                    symlink("/libc/lib/$tuple/$member", "$new_tree$ldso");
                }
                closedir($dir);
            }
        }
    }

    return $new_tree;
}

=item capture_gl_provider_libc(I<TREE>, I<TUPLES>)

Populate a new temporary directory with symbolic links to the B<glibc>
in I<TREE>, assuming that I<TREE> will be mounted on F</gl-provider>,
such that I<TREE> can be mounted over F</libc> in a container that
has been modified by C<make_container_libc_overridable>.

For example, on x86_64 systems, the returned directory will contain
symbolic links F<lib/x86_64-linux-gnu/ld-linux-x86-64.so> →
F</gl-provider/lib/x86_64-linux-gnu/ld-linux-x86-64.so>
and
F<lib/x86_64-linux-gnu/libc.so.6> →
F</gl-provider/lib/x86_64-linux-gnu/libc.so.6>.

I<TUPLES> is a reference to an array of Debian multiarch tuples.

Return the newly created directory.

=cut

sub capture_gl_provider_libc {
    my $tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $libc = File::Temp->newdir(
        TMPDIR => 1, TEMPLATE => 'capsule-test-libc-XXXXXXXX');

    diag 'Capturing GL provider libc';

    make_path("$libc/lib", verbose => 1);
    run(['touch', "$libc/GL-PROVIDER"]);

    foreach my $tuple (@multiarch_tuples) {
        make_path("$libc/lib/$tuple", verbose => 1);

        my $ldso = multiarch_tuple_to_ldso($tuple);
        my $libdir;

        if (defined $ldso) {
            (undef, $libdir, undef) = File::Spec->splitpath($ldso);
        }

        foreach my $lib (qw(BrokenLocale SegFault anl c cidn crypt dl m
            memusage mvec nsl pcprofile pthread resolv rt thread_db util)) {
            SEARCH: foreach my $search ("/lib/$tuple", $libdir) {
                next SEARCH unless defined $search;
                next SEARCH unless -d "$tree$search";

                opendir(my $dir, "$tree$search");
                SONAME: while (defined(my $soname = readdir $dir)) {
                    next SONAME unless $soname =~ m/^lib\Q$lib\E\.so\.[0-9.]+$/;

                    # Get the right implementation respecting hwcaps,
                    # and make it absolute.
                    # TODO: This doesn't work for foreign architectures:
                    # we would have to run a version of capsule-version
                    # that is appropriate for the foreign architecture.
                    my $real = get_lib_implementation($soname, $tree);
                    die "Cannot resolve $search/$soname in $tree" unless defined $real;

                    { no autodie; unlink("$libc/lib/$tuple/$soname"); }
                    diag "Creating symbolic link $libc/lib/$tuple/$soname → /gl-provider$real";
                    symlink("/gl-provider$real", "$libc/lib/$tuple/$soname");
                }
                closedir($dir);
            }
        }

        if (defined $ldso) {
            SEARCH: foreach my $search ("/lib/$tuple", $libdir) {
                next SEARCH unless defined $search;
                next SEARCH unless -d "$tree$search";

                opendir(my $dir, "$tree$search");
                MEMBER: while (defined(my $member = readdir $dir)) {
                    next MEMBER unless $member =~ m/^ld-.*\.so\./;

                    my $real;
                    run_in_container($tree,
                        ['readlink', '-f', "$search/$member"],
                        '>', \$real)
                        or die "Cannot resolve $search/$member in $tree";
                    chomp $real;

                    { no autodie; unlink("$libc/lib/$tuple/$member"); }
                    diag "Creating symbolic link $libc/lib/$tuple/$member → /gl-provider/lib/$tuple/$member";
                    symlink("/gl-provider/lib/$tuple/$member", "$libc/lib/$tuple/$member");
                }
                closedir($dir);
            }
        }
    }

    return $libc;
}

=item capture_gl_provider_libs_if_newer(I<GL_PROVIDER>, I<CONTAINER>|B<undef>, I<TUPLES>, I<DEST>, I<LIBS>)

Populate I<DEST>F</lib/>I<TUPLE> with symbolic links to the libraries
in I<GL_PROVIDER>, assuming that I<GL_PROVIDER> will be mounted on
F</gl-provider>, such that I<DEST>F</lib/>I<TUPLE> can be added
to the B<LD_LIBRARY_PATH>.

If I<CONTAINER> is B<undef>, this is done for all libraries listed.
If I<CONTAINER> is defined, libraries that appear to be strictly newer
in I<CONTAINER> are skipped.

I<TUPLES> is a reference to an array of Debian multiarch tuples.

I<LIBS> is a list of stem names of libraries (C<GL1>, C<stdc++>, C<z>),
or references to regular expressions matching an entire library basename
(C<libnvidia-.*\.so\..*>).

=cut

sub capture_gl_provider_libs_if_newer {
    my $gl_provider_tree = shift;
    my $container_tree = shift;
    my @multiarch_tuples = @{ shift() };
    my $dest = shift;
    my @libs = @{ shift() };

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
                next SEARCH unless -d "$gl_provider_tree$search";

                opendir(my $dir, "$gl_provider_tree$search");
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
                    # TODO: This doesn't work for foreign architectures:
                    # we would have to run a version of capsule-version
                    # that is appropriate for the foreign architecture.
                    my $gl_provider_impl = get_lib_implementation($soname,
                        $gl_provider_tree);

                    if (! defined $gl_provider_impl) {
                        # capsule-version might not be able to find the
                        # library if it's just a symlink to some library
                        # with a different SONAME, like libGLX_indirect.so.0
                        $gl_provider_impl = get_lib_implementation(
                            "$search/$soname", $gl_provider_tree);
                    }

                    if (! defined $gl_provider_impl) {
                        die "Unable to resolve $search/$soname in ".
                            $gl_provider_tree;
                    }

                    if (defined $container_tree) {
                        my $container_impl = get_lib_implementation($soname,
                            $container_tree);

                        my $gl_provider_version = $gl_provider_impl;
                        $gl_provider_version =~ s/.*\.so\.//;

                        my $container_version = $container_impl;
                        $container_version =~ s/.*\.so\.//
                            if defined $container_version;

                        if (! defined $container_impl) {
                            diag "${ansi_green}Using GL provider's ".
                                "$tuple $soname '$gl_provider_impl' because ".
                                "container does not have that ".
                                "library${ansi_reset}";
                        }
                        elsif ($container_version eq $gl_provider_version) {
                            # The libraries for which we call this function
                            # are fairly closely tied to the GL provider,
                            # so if in doubt we prefer the GL provider
                            diag "${ansi_green}Using GL provider's ".
                                "$tuple $soname '$gl_provider_impl'".
                                "${ansi_reset} because container's version ".
                                "appears to be the same";
                        }
                        elsif (versioncmp($container_version,
                                $gl_provider_version) > 0) {
                            diag "${ansi_cyan}Using container's ".
                                "$tuple $soname '$container_impl' because ".
                                "GL provider's $gl_provider_impl appears ".
                                "older${ansi_reset}";
                            next SONAME;
                        }
                        else {
                            diag "${ansi_green}Using GL provider's ".
                                "$tuple $soname '$gl_provider_impl' because ".
                                "container's $container_impl appears ".
                                "older${ansi_reset}";
                        }
                    }
                    else {
                        diag "${ansi_green}Using GL provider's ".
                            "$tuple $soname unconditionally${ansi_reset}";
                    }

                    { no autodie; unlink("$dest/lib/$tuple/$soname"); }
                    diag "Creating symbolic link $dest/lib/$tuple/$soname → ".
                        "/gl-provider$gl_provider_impl";
                    symlink("/gl-provider$gl_provider_impl", "$dest/lib/$tuple/$soname");
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
my $gl_stack;
my $arch = 'x86_64';

GetOptions(
    'app=s' => \$app,
    'capsule-version-tool=s' => \$CAPSULE_VERSION_TOOL,
    'container=s' => \$container_tree,
    'flatpak-app=s' => \$flatpak_app,
    'flatpak-runtime=s' => \$flatpak_runtime,
    'gl-provider=s' => \$gl_provider_tree,
    'gl-stack=s' => \$gl_stack,
    'multiarch=s' => sub {
        @multiarch_tuples = split /[\s,]+/, $_[1];
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

unless (defined $CAPSULE_VERSION_TOOL) {
    $CAPSULE_VERSION_TOOL = `pkg-config --variable=CAPSULE_VERSION_TOOL capsule`;
    chomp $CAPSULE_VERSION_TOOL;
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

my $gl_provider_libc_so = get_lib_implementation(
    'libc.so.6', $gl_provider_tree);
my $container_libc_so = get_lib_implementation(
    'libc.so.6', $container_tree);

my $tmpdir = File::Temp->newdir(
        TMPDIR => 1, TEMPLATE => 'capsule-test-temp-XXXXXXXX');
$container_tree = make_container_libc_overridable($tmpdir, $container_tree, \@multiarch_tuples);

my @bwrap = qw(
    bwrap
    --unshare-user
    --unshare-pid
    --unshare-uts --hostname bwrap
    --dev-bind /dev /dev
    --proc /proc
    --ro-bind /sys /sys
);
push @bwrap, '--dir', $ENV{HOME};

if (-d "$container_tree/etc") {
    push @bwrap, '--ro-bind', "$container_tree/etc", '/etc';
}

if (-d "$container_tree/var") {
    push @bwrap, '--ro-bind', "$container_tree/var", '/var';
}

push @bwrap, '--tmpfs', '/tmp';
push @bwrap, '--tmpfs', '/var/tmp';
push @bwrap, '--ro-bind', '/etc/machine-id', '/etc/machine-id';
push @bwrap, '--ro-bind', '/etc/passwd', '/etc/passwd';
push @bwrap, '--bind', '/tmp/.X11-unix', '/tmp/.X11-unix';

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
    push @bwrap, '--setenv', 'PATH', '/app/bin:/app/sbin:/usr/bin:/usr/sbin';
}

my $gl_provider_libc;

# We can't look for .so.VERSION here because libc's real name ends with
# VERSION.so. Take the basname instead.
my (undef, undef, $gl_provider_libc_version) =
    File::Spec->splitpath($gl_provider_libc_so);
my (undef, undef, $container_libc_version) =
    File::Spec->splitpath($container_libc_so);

if (versioncmp($container_libc_version, $gl_provider_libc_version) <= 0) {
    # We need to parachute in the GL provider's libc instead of the
    # container's, because the GL provider's libX11, libGL etc. would
    # be within their rights to use newer libc features; we also need
    # the GL provider's ld.so, because old ld.so can't necessarily load
    # new libc successfully
    diag "${ansi_bright}${ansi_green}Using GL provider's libc ".
        "$gl_provider_libc_version because container's ".
        "$container_libc_version appears older or equal${ansi_reset}";
    $gl_provider_libc = capture_gl_provider_libc($gl_provider_tree,
        \@multiarch_tuples);
    push @bwrap, '--ro-bind', $gl_provider_libc, '/libc';
}
else {
    diag "${ansi_bright}${ansi_cyan}Using container's libc ".
        "$container_libc_version because GL provider's ".
        "$gl_provider_libc_version appears older${ansi_reset}";
}

my @dri_path;

if (defined $gl_stack) {
    diag "Using standalone GL stack $gl_stack";

    push @bwrap, '--ro-bind', $gl_stack, '/gl';

    foreach my $tuple (@multiarch_tuples) {
        push @ld_path, "/gl/lib/$tuple";
    }
}
else {
    my $gl_provider_gl = "$tmpdir/gl";

    diag "Using GL stack from $gl_provider_tree";
    capture_gl_provider_libs_if_newer($gl_provider_tree, undef,
        \@multiarch_tuples, $gl_provider_gl, [
            qw(EGL GL GLESv1_CM GLESv2 GLX GLdispatch
            Xau Xdamage Xdmcp Xext Xfixes Xxf86vm
            cuda drm gbm glapi glx nvcuvid vdpau
            wayland-client wayland-server
            xcb-dri2 xcb-dri3 xcb-present xcb-sync xcb-xfixes xshmfence),
            qr{lib(EGL|GLESv1_CM|GLESv2|GLX|drm|vdpau)_.*\.so\.[0-9]+},
            # We allow any extension for these, not just a single integer
            qr{libnvidia-.*\.so\..*},
        ]);

    diag "Updating libraries from $gl_provider_tree if necessary";
    capture_gl_provider_libs_if_newer($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $gl_provider_gl, [
            qw(bsd edit elf expat ffi gcc_s
            ncurses pciaccess sensors stdc++ tinfo z),
            qr{libLLVM-.*\.so\.[0-9]+},
        ]);

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

# Parachute in the GL provider's X libraries, because they go with the
# graphics stack, and libX11/libxcb assume there is only one instance.
# Do the same for libelf which is needed by libcapsule, and for
# libcapsule itself.
my $gl_provider_updates = "$tmpdir/updates";

# TODO: Is there any circumstance under which we'd skip this block?
do {
    diag 'Adding singleton libraries from GL provider, if newer...';

    capture_gl_provider_libs_if_newer($gl_provider_tree, $container_tree,
        \@multiarch_tuples, $gl_provider_updates,
        [qw(X11 X11-xcb capsule elf xcb xcb-dri2 xcb-dri3 xcb-glx
            xcb-present xcb-sync xcb-xfixes
            Xau Xdamage Xdmcp Xext Xfixes Xxf86vm
            )]);

    foreach my $tuple (@multiarch_tuples) {
        push @ld_path, "/updates/lib/$tuple";
    }

    push @bwrap, '--ro-bind', $gl_provider_updates, '/updates';
};

if (@ld_path) {
    push @bwrap, '--setenv', 'LD_LIBRARY_PATH', join(':', @ld_path);
}

if (@dri_path) {
    push @bwrap, '--setenv', 'LIBGL_DRIVERS_PATH', join(':', @dri_path);
}

push @bwrap, '--setenv', 'DISPLAY', $ENV{DISPLAY};

push @bwrap, '--ro-bind', "$ENV{HOME}/.Xauthority",
    "$ENV{HOME}/.Xauthority" if -e "$ENV{HOME}/.Xauthority";

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
