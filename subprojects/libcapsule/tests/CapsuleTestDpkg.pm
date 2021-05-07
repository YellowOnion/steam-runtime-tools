# Exerpts from Dpkg::Shlibs::SymbolFile
#
# Copyright © 2007 Raphaël Hertzog <hertzog@debian.org>
# Copyright © 2009-2010 Modestas Vainius <modax@debian.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

package CapsuleTestDpkg;

use strict;
use warnings;

my %blacklist = (
    __bss_end__ => 1,                   # arm
    __bss_end => 1,                     # arm
    _bss_end__ => 1,                    # arm
    __bss_start => 1,                   # ALL
    __bss_start__ => 1,                 # arm
    __data_start => 1,                  # arm
    __do_global_ctors_aux => 1,         # ia64
    __do_global_dtors_aux => 1,         # ia64
    __do_jv_register_classes => 1,      # ia64
    _DYNAMIC => 1,                      # ALL
    _edata => 1,                        # ALL
    _end => 1,                          # ALL
    __end__ => 1,                       # arm
    __exidx_end => 1,                   # armel
    __exidx_start => 1,                 # armel
    _fbss => 1,                         # mips, mipsel
    _fdata => 1,                        # mips, mipsel
    _fini => 1,                         # ALL
    _ftext => 1,                        # mips, mipsel
    _GLOBAL_OFFSET_TABLE_ => 1,         # hppa, mips, mipsel
    __gmon_start__ => 1,                # hppa
    __gnu_local_gp => 1,                # mips, mipsel
    _gp => 1,                           # mips, mipsel
    _init => 1,                         # ALL
    _PROCEDURE_LINKAGE_TABLE_ => 1,     # sparc, alpha
    _SDA2_BASE_ => 1,                   # powerpc
    _SDA_BASE_ => 1,                    # powerpc
);

for my $i (14 .. 31) {
    # Many powerpc specific symbols
    $blacklist{"_restfpr_$i"} = 1;
    $blacklist{"_restfpr_$i\_x"} = 1;
    $blacklist{"_restgpr_$i"} = 1;
    $blacklist{"_restgpr_$i\_x"} = 1;
    $blacklist{"_savefpr_$i"} = 1;
    $blacklist{"_savegpr_$i"} = 1;
}

sub symbol_is_blacklisted {
    my ($symbol, $include_groups) = @_;

    return 1 if exists $blacklist{$symbol};

    # The ARM Embedded ABI spec states symbols under this namespace as
    # possibly appearing in output objects.
    return 1 if not ${$include_groups}{aeabi} and $symbol =~ /^__aeabi_/;

    # The GNU implementation of the OpenMP spec, specifies symbols under
    # this namespace as possibly appearing in output objects.
    return 1 if not ${$include_groups}{gomp}
                and $symbol =~ /^\.gomp_critical_user_/;

    return 0;
}

1;
