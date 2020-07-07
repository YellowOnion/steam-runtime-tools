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
#
# Note that get_symbols_with_nm() uses some GPL-2+ code taken from dpkg.

package CapsuleTest;

use strict;
use warnings;

use Cwd qw(abs_path);
use FindBin;
use Exporter qw(import);
use IPC::Run qw(run);
use Test::More;

our @EXPORT = qw(
    assert_run_verbose
    bind_usr
    diag_multiline
    explain_wait_status
    get_symbols_with_nm
    libcapsule_uninstalled
    run_ok
    run_verbose
    skip_all_unless_bwrap
    skip_all_unless_nm
    $CAPSULE_CAPTURE_LIBS_TOOL
    $CAPSULE_INIT_PROJECT_TOOL
    $CAPSULE_SYMBOLS_TOOL
    $CAPSULE_VERSION_TOOL
    $NM
    $PKG_CONFIG
    $builddir
    $srcdir
);

=encoding utf8

=head1 NAME

CapsuleTest - utilities for libcapsule automated and manual tests

=head1 EXPORTED VARIABLES

=over

=cut

=item $PKG_CONFIG

The B<pkg-config>(1) utility.

=cut

our $PKG_CONFIG = $ENV{PKG_CONFIG};
$PKG_CONFIG = 'pkg-config' unless length $PKG_CONFIG;

=item $CAPSULE_CAPTURE_LIBS_TOOL

The B<capsule-capture-libs>(1) development tool.

=cut

our $CAPSULE_CAPTURE_LIBS_TOOL = $ENV{CAPSULE_CAPTURE_LIBS_TOOL};

if (! length $CAPSULE_CAPTURE_LIBS_TOOL) {
    $CAPSULE_CAPTURE_LIBS_TOOL = `$PKG_CONFIG --variable=CAPSULE_CAPTURE_LIBS_TOOL libcapsule-tools`;
    chomp $CAPSULE_CAPTURE_LIBS_TOOL;
}

=item $CAPSULE_INIT_PROJECT_TOOL

The B<capsule-init-project>(1) development tool.

=cut

our $CAPSULE_INIT_PROJECT_TOOL = $ENV{CAPSULE_INIT_PROJECT_TOOL};

if (! length $CAPSULE_INIT_PROJECT_TOOL) {
    $CAPSULE_INIT_PROJECT_TOOL = `$PKG_CONFIG --variable=CAPSULE_INIT_PROJECT_TOOL libcapsule-tools`;
    chomp $CAPSULE_INIT_PROJECT_TOOL;
}

=item $CAPSULE_SYMBOLS_TOOL

The B<capsule-symbols>(1) development tool.

=cut

our $CAPSULE_SYMBOLS_TOOL = $ENV{CAPSULE_SYMBOLS_TOOL};

if (! length $CAPSULE_SYMBOLS_TOOL) {
    $CAPSULE_SYMBOLS_TOOL = `$PKG_CONFIG --variable=CAPSULE_SYMBOLS_TOOL libcapsule-tools`;
    chomp $CAPSULE_SYMBOLS_TOOL;
}

=item $CAPSULE_VERSION_TOOL

The B<capsule-version>(1) development tool.

=cut

our $CAPSULE_VERSION_TOOL = $ENV{CAPSULE_VERSION_TOOL};

unless (defined $CAPSULE_VERSION_TOOL) {
    $CAPSULE_VERSION_TOOL = `$PKG_CONFIG --variable=CAPSULE_VERSION_TOOL libcapsule-tools`;
    chomp $CAPSULE_VERSION_TOOL;
}

=item $NM

The B<nm>(1) symbol-name-listing utility, configured for BSD output format.

=cut

our $NM = $ENV{NM};

if (! length $NM) {
    $NM = 'nm -B';
}

=item $srcdir

An appropriate directory to find non-generated files: the top directory
of the source tree, or the root directory of this package's GLib-style
installed-tests.

=cut

# G_TEST_* convention stolen from GLib, even though we aren't using GTest
my (undef, $here, undef) = File::Spec->splitpath($INC{'CapsuleTest.pm'});
our $srcdir = $ENV{G_TEST_SRCDIR};
$srcdir = abs_path("$here/..") unless defined $srcdir;

=item $builddir

An appropriate directory to find non-generated files: the top directory
of the build tree, or the root directory of this package's GLib-style
installed-tests.

=cut

our $builddir = $ENV{G_TEST_BUILDDIR};
$builddir = abs_path("$here/..") unless defined $builddir;

diag "Source or installation directory: $srcdir";
diag "Build or installation directory: $builddir";

=back

=head1 EXPORTED FUNCTIONS

=over

=item assert_run_verbose(I<ARGV>, ...)

Log the given command, run it, and die if it didn't return success.
I<ARGV> is an array-reference containing arguments.
Subsequent parameters are passed to C<IPC::Run::run> and can be used
to redirect output.

=cut

sub assert_run_verbose {
    my $argv = shift;
    my $debug = join(' ', @$argv);
    diag($debug);
    if (! run($argv, @_)) {
        my $explained = explain_wait_status($?);
        die "Command exited with status $? ($explained): '$debug'";
    }
}

=item diag_multiline(I<TEXT>)

Split I<TEXT> into lines and emit them as TAP diagnostics.

=cut

sub diag_multiline {
    foreach my $line (split /^/m, shift) {
        chomp $line;
        diag "    $line";
    }
}

=item explain_wait_status(I<CODE>)

Convert Unix-style wait status I<CODE> into something human-readable.

=cut

sub explain_wait_status {
    my $status = shift;
    my @ret;
    my $signal = $status & 127;
    my $code = ($status >> 8);

    if ($signal) {
        push @ret, "killed by signal $signal";
    }

    if ($status & 128) {
        push @ret, 'core dumped';
    }

    if ($code & 128) {
        my $maybe = $code & 127;
        unshift @ret,
            "exited with code $code (child process killed by signal $maybe?)";
    }
    elsif ($code || ! @ret) {
        unshift @ret, "exited with code $code";
    }

    return join(', ', @ret);
}

=item run_ok(I<ARGV>, ...)

A TAP assertion that the given command exits 0. I<ARGV> is an
array-reference containing arguments. Subsequent parameters are
passed to C<IPC::Run::run> and can be used to redirect output.

=cut

sub run_ok {
    my $argv = shift;
    my $debug = join(' ', @$argv);
    diag($debug);
    if (run($argv, @_)) {
        ok(1, qq{Command successful as expected: '$debug'});
    }
    else {
        my $explained = explain_wait_status($?);
        ok(0, "Command exited with status $? ($explained): '$debug'");
    }
}

=item run_verbose(I<ARGV>, ...)

Log the given command, run it, and return the same thing as
C<IPC::Run::run>. I<ARGV> is an array-reference containing arguments.
Subsequent parameters are passed to C<IPC::Run::run> and can be used
to redirect output.

=cut

sub run_verbose {
    my $argv = shift;
    my $debug = join(' ', @$argv);
    diag($debug);
    return run($argv, @_);
}

=item skip_all_unless_bwrap()

If we cannot run B<bwrap>(1), log a TAP report that all tests have been
skipped (as if via C<plan skip_all =E<gt> ...>), and exit.

=cut

sub skip_all_unless_bwrap {
    if (! run([qw(
                env bwrap --ro-bind / / --unshare-ipc --unshare-net
                --unshare-pid --unshare-user --unshare-uts true
            )], '>&2')) {
        plan(skip_all => 'Cannot run bwrap');
    }
}

=item skip_all_unless_nm()

If we cannot run B<nm>(1) to implement B<get_symbols_with_nm>, log a
TAP report that all tests have been skipped (as if via
C<plan skip_all =E<gt> ...>), and exit.

=cut

sub skip_all_unless_nm {
    if (! run([split(' ', $NM),
                qw(--dynamic --extern-only --defined-only
                --with-symbol-versions /bin/true)], '>/dev/null')) {
        plan(skip_all =>
            'Cannot run nm (no support for --with-symbol-versions?)');
    }
}

=item get_symbols_with_nm(I<LIBRARY>)

Return a list of symbols found in I<LIBRARY>, in the same format
that capsule-symbols would use.

=cut

sub get_symbols_with_nm {
    my $library = shift;
    my $output;

    run_ok([split(' ', $NM), '--dynamic', '--extern-only', '--defined-only',
            '--with-symbol-versions', $library], '>', \$output);
    my @symbols_produced;
    foreach my $line (split /\n/, $output) {
        if ($line =~ m/^[[:xdigit:]]+\s+[ABCDGIRSTW]+\s+([^@]+)(\@\@?.*)?/) {
            my $symbol = $1;
            my $version = $2;
            if ($version =~ m/^(\@\@?[^@]+)\1$/) {
                # Ignore doubled symbol-version suffix.
                # https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=964457
                $version = $1;
            }
            require CapsuleTestDpkg;
            next if CapsuleTestDpkg::symbol_is_blacklisted($symbol);
            next if "\@\@$symbol" eq $version;

            # Put them in the same format that capsule-symbols uses
            if (length $version && $version ne '@@Base') {
                push @symbols_produced, "$symbol $version";
            }
            else {
                push @symbols_produced, "$symbol ";
            }
        }
    }
    foreach my $sym (@symbols_produced) {
        diag "- $sym";
    }
    return sort @symbols_produced;
}

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

=item libcapsule_uninstalled()

Return true if libcapsule is uninstalled (build-time tests) and false
if we are carrying out installed-tests.

=cut

sub libcapsule_uninstalled {
    return exists $ENV{CAPSULE_TESTS_UNINSTALLED};
}

=back

=head1 ENVIRONMENT

=over

=item CAPSULE_INIT_PROJECT_TOOL

B<capsule-init-project>(1)

=item CAPSULE_SYMBOLS_TOOL

B<capsule-symbols>(1)

=item CAPSULE_TESTS_KEEP_TEMP

If set to a non-empty value, temporary directories created by this test
will not be cleaned up.

=cut

if (length $ENV{CAPSULE_TESTS_KEEP_TEMP}) {
    $File::Temp::KEEP_ALL = 1;
}

=item CAPSULE_VERSION_TOOL

B<capsule-version>(1)

=item NM

The B<nm>(1) symbol-name-listing utility, if not C<nm -B>.

=item PKG_CONFIG

B<pkg-config>(1)

=back

=head1 SEE ALSO

B<Test::More>(3pm), B<bwrap>(1)

=cut

1;
