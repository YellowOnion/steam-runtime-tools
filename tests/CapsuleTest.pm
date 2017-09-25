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

package CapsuleTest;

use strict;
use warnings;

use Cwd qw(abs_path);
use FindBin;
use Exporter qw(import);
use IPC::Run qw(run);
use Test::More;

our @EXPORT = qw(
    diag_multiline
    run_ok
    run_verbose
    skip_all_unless_bwrap
    $builddir
    $srcdir
);

=encoding utf8

=head1 NAME

CapsuleTest - utilities for libcapsule automated and manual tests

=head1 EXPORTED VARIABLES

=over

=cut

=item $srcdir

An appropriate directory to find non-generated files: the top directory
of the source tree, or the root directory of this package's GLib-style
installed-tests.

=cut

# G_TEST_* convention stolen from GLib, even though we aren't using GTest
our $srcdir = $ENV{G_TEST_SRCDIR};
$srcdir = abs_path($FindBin::Bin) unless defined $srcdir;

=item $builddir

An appropriate directory to find non-generated files: the top directory
of the build tree, or the root directory of this package's GLib-style
installed-tests.

=cut

our $builddir = $ENV{G_TEST_BUILDDIR};
$builddir = abs_path($FindBin::Bin) unless defined $builddir;

diag "Source or installation directory: $srcdir";
diag "Build or installation directory: $builddir";

=back

=head1 EXPORTED FUNCTIONS

=over

=item diag_multiline(I<TEXT>)

Split I<TEXT> into lines and emit them as TAP diagnostics.

=cut

sub diag_multiline {
    foreach my $line (split /^/m, shift) {
        chomp $line;
        diag "    $line";
    }
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
    ok(run($argv, @_), qq{"$debug" should succeed});
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
                bwrap --ro-bind / / --unshare-ipc --unshare-net
                --unshare-pid --unshare-user --unshare-uts true
            )], '>&2')) {
        plan(skip_all => 'Cannot run bwrap');
    }
}

=back

=head1 SEE ALSO

B<Test::More>(3pm), B<bwrap>(1)

=cut

1;
