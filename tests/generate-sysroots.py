#!/usr/bin/env python3
#
# Copyright © 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import os
import argparse
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("path")
parser.add_argument('-i', '--install', action='store_true',
                    help='Install the sysroot in the provided [path], using $MESON_INSTALL_DESTDIR_PREFIX as a prefix')
args = parser.parse_args()

if args.install:
    full_path = os.path.join(os.environ['MESON_INSTALL_DESTDIR_PREFIX'], args.path.lstrip("/"))
else:
    full_path = args.path

# We recreate the chosen destination 'sysroot', to avoid potential issues with
# old files
try:
    shutil.rmtree(full_path)
except FileNotFoundError:
    pass
os.makedirs(full_path, mode=0o755, exist_ok=True)
os.chdir(full_path)

# Only the leaf directories need to be listed here.
for name in '''
debian10/usr/lib/i386-linux-gnu/dri
debian10/usr/lib/i386-linux-gnu/vdpau
debian10/usr/lib/x86_64-linux-gnu/dri
debian10/usr/lib/x86_64-linux-gnu/vdpau
debian10/run/systemd
debian-unstable/etc
fedora/usr/lib/dri
fedora/usr/lib/vdpau
fedora/usr/lib64/dri
fedora/usr/lib64/vdpau
fedora/run/systemd
flatpak-example/usr/lib/dri
flatpak-example/usr/lib/mock-abi/GL/lib/dri
flatpak-example/usr/lib/mock-abi/dri
flatpak-example/usr/lib/mock-abi/dri/intel-vaapi-driver
flatpak-example/usr/lib/mock-abi/vdpau
flatpak-example/run/host
invalid-os-release/usr/lib
invalid-os-release/run/host
no-os-release/another_custom_path
no-os-release/custom_path32/dri
no-os-release/custom_path32/va
no-os-release/custom_path32/vdpau
no-os-release/custom_path32_2/dri
no-os-release/custom_path32_2/va
no-os-release/custom_path64/dri
no-os-release/custom_path64/va
no-os-release/usr/lib/dri
no-os-release/usr/lib/vdpau
steamrt/etc
steamrt/overrides/bin
steamrt/overrides/lib/x86_64-linux-gnu
steamrt/overrides/lib/i386-linux-gnu
steamrt/usr/lib
steamrt/run/pressure-vessel
steamrt-overrides-issues/etc
steamrt-overrides-issues/overrides/bin
steamrt-overrides-issues/overrides/lib/i386-linux-gnu
steamrt-overrides-issues/overrides/lib/x86_64-linux-gnu
steamrt-overrides-issues/usr/lib
steamrt-unofficial/etc
steamrt-unofficial/usr/lib
steamrt-unofficial/proc/1
ubuntu16/usr/lib/dri
ubuntu16/usr/lib/mock-ubuntu-64-bit/dri
ubuntu16/usr/lib/mock-ubuntu-64-bit/mesa
ubuntu16/usr/lib/mock-ubuntu-64-bit/vdpau
'''.split():
    os.makedirs(name, mode=0o755, exist_ok=True)

for name in '''
debian10/usr/lib/i386-linux-gnu/dri/i965_dri.so
debian10/usr/lib/i386-linux-gnu/dri/r300_dri.so
debian10/usr/lib/i386-linux-gnu/dri/r600_drv_video.so
debian10/usr/lib/i386-linux-gnu/dri/radeonsi_dri.so
debian10/usr/lib/i386-linux-gnu/libEGL_mesa.so.0
debian10/usr/lib/i386-linux-gnu/libva.so.2
debian10/usr/lib/i386-linux-gnu/libvdpau.so.1
debian10/usr/lib/i386-linux-gnu/vdpau/libvdpau_r600.so
debian10/usr/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so.1.0.0
debian10/usr/lib/x86_64-linux-gnu/dri/i965_dri.so
debian10/usr/lib/x86_64-linux-gnu/dri/r600_dri.so
debian10/usr/lib/x86_64-linux-gnu/dri/r600_drv_video.so
debian10/usr/lib/x86_64-linux-gnu/dri/radeon_dri.so
debian10/usr/lib/x86_64-linux-gnu/dri/radeonsi_drv_video.so
debian10/usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0
debian10/usr/lib/x86_64-linux-gnu/libGL.so.1
debian10/usr/lib/x86_64-linux-gnu/libva.so.2
debian10/usr/lib/x86_64-linux-gnu/libvdpau.so.1
debian10/usr/lib/x86_64-linux-gnu/vdpau/libvdpau_r600.so.1.0.0
debian10/usr/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so.1.0.0
debian-unstable/.dockerenv
fedora/usr/lib/dri/i965_dri.so
fedora/usr/lib/dri/r300_dri.so
fedora/usr/lib/dri/r600_drv_video.so
fedora/usr/lib/dri/radeonsi_dri.so
fedora/usr/lib/libEGL_mesa.so.0
fedora/usr/lib/libGL.so.1
fedora/usr/lib/libva.so.1
fedora/usr/lib/libvdpau.so.1
fedora/usr/lib/vdpau/libvdpau_nouveau.so.1
fedora/usr/lib/vdpau/libvdpau_r600.so
fedora/usr/lib/vdpau/libvdpau_radeonsi.so.1.0.0
fedora/usr/lib64/dri/i965_dri.so
fedora/usr/lib64/dri/r600_dri.so
fedora/usr/lib64/dri/r600_drv_video.so
fedora/usr/lib64/dri/radeon_dri.so
fedora/usr/lib64/dri/radeonsi_drv_video.so
fedora/usr/lib64/libEGL_mesa.so.0
fedora/usr/lib64/libva.so.2
fedora/usr/lib64/libvdpau.so.1
fedora/usr/lib64/vdpau/libvdpau_r300.so
fedora/usr/lib64/vdpau/libvdpau_radeonsi.so
flatpak-example/.flatpak-info
flatpak-example/usr/lib/dri/r300_dri.so
flatpak-example/usr/lib/dri/r600_drv_video.so
flatpak-example/usr/lib/mock-abi/GL/lib/dri/i965_dri.so
flatpak-example/usr/lib/mock-abi/GL/lib/dri/r600_drv_video.so
flatpak-example/usr/lib/mock-abi/dri/intel-vaapi-driver/i965_drv_video.so
flatpak-example/usr/lib/mock-abi/dri/radeonsi_drv_video.so
flatpak-example/usr/lib/mock-abi/libEGL_mesa.so.0
flatpak-example/usr/lib/mock-abi/libva.so.2
flatpak-example/usr/lib/mock-abi/libvdpau.so.1
flatpak-example/usr/lib/mock-abi/vdpau/libvdpau_radeonsi.so.1
flatpak-example/run/host/.exists
invalid-os-release/run/host/.exists
no-os-release/another_custom_path/libvdpau_custom.so
no-os-release/custom_path32/dri/r600_dri.so
no-os-release/custom_path32/dri/radeon_dri.so
no-os-release/custom_path32/va/r600_drv_video.so
no-os-release/custom_path32/va/radeonsi_drv_video.so
no-os-release/custom_path32/vdpau/libvdpau_r600.so.1
no-os-release/custom_path32/vdpau/libvdpau_radeonsi.so.1
no-os-release/custom_path32_2/dri/r300_dri.so
no-os-release/custom_path32_2/va/nouveau_drv_video.so
no-os-release/custom_path64/dri/i965_dri.so
no-os-release/custom_path64/va/radeonsi_drv_video.so
no-os-release/usr/lib/dri/i965_dri.so
no-os-release/usr/lib/dri/r600_drv_video.so
no-os-release/usr/lib/dri/radeonsi_dri.so
no-os-release/usr/lib/libGL.so.1
no-os-release/usr/lib/libva.so.1
no-os-release/usr/lib/libvdpau.so.1
no-os-release/usr/lib/libvdpau_r9000.so
no-os-release/usr/lib/vdpau/libvdpau_nouveau.so.1
steamrt/overrides/bin/.keep
steamrt/overrides/lib/x86_64-linux-gnu/libGLX_custom.so.0
steamrt/overrides/lib/x86_64-linux-gnu/libGLX_mesa.so.0
steamrt/overrides/lib/i386-linux-gnu/libGLX_nvidia.so.0
steamrt/run/pressure-vessel/.exists
steamrt-overrides-issues/overrides/bin/.keep
steamrt-overrides-issues/overrides/lib/i386-linux-gnu/.keep
ubuntu16/usr/lib/dri/radeonsi_dri.so
ubuntu16/usr/lib/mock-ubuntu-64-bit/dri/i965_dri.so
ubuntu16/usr/lib/mock-ubuntu-64-bit/dri/radeon_dri.so
ubuntu16/usr/lib/mock-ubuntu-64-bit/dri/radeonsi_drv_video.so
ubuntu16/usr/lib/mock-ubuntu-64-bit/libva.so.1
ubuntu16/usr/lib/mock-ubuntu-64-bit/mesa/libGL.so.1
ubuntu16/usr/lib/mock-ubuntu-64-bit/vdpau/libvdpau_r600.so.1.0.0
ubuntu16/usr/lib/mock-ubuntu-64-bit/vdpau/libvdpau_radeonsi.so.1.0.0
'''.split():
    os.makedirs(os.path.dirname(name), mode=0o755, exist_ok=True)
    open(name, 'w').close()

for name, target in {
    'debian10/usr/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so':
        'libvdpau_radeonsi.so.1.0.0',
    'debian10/usr/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so.1':
        'libvdpau_radeonsi.so.1.0.0',
    'debian10/usr/lib/x86_64-linux-gnu/vdpau/libvdpau_r600.so.1':
        'libvdpau_r600.so.1.0.0',
    'debian10/usr/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so':
        'libvdpau_radeonsi.so.1.0.0',
    'debian10/usr/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so.1':
        'libvdpau_radeonsi.so.1.0.0',
    'fedora/usr/lib/vdpau/libvdpau_radeonsi.so':
        'libvdpau_radeonsi.so.1.0.0',
    'fedora/usr/lib/vdpau/libvdpau_radeonsi.so.1':
        'libvdpau_radeonsi.so.1.0.0',
    'fedora/usr/lib/vdpau/libvdpau_radeonsi.so.1.0':
        'libvdpau_radeonsi.so.1.0.0',
    'fedora/usr/lib64/vdpau/libvdpau_r300.so.1':
        'libvdpau_r300.so',
    'fedora/usr/lib64/vdpau/libvdpau_radeonsi.so.1':
        'libvdpau_radeonsi.so',
    'steamrt/etc/os-release':
        '../usr/lib/os-release',
    'steamrt/overrides/lib/x86_64-linux-gnu/libgcc_s.so.1':
        '/run/host/usr/lib/libgcc_s.so.1',
    'steamrt-overrides-issues/etc/os-release':
        '../usr/lib/os-release',
    'steamrt-overrides-issues/overrides/lib/x86_64-linux-gnu/libgcc_s.so.1':
        '/run/host/usr/lib/libgcc_s.so.1',
    'steamrt-unofficial/etc/os-release':
        '../usr/lib/os-release',
    'ubuntu16/usr/lib/mock-ubuntu-64-bit/vdpau/libvdpau_r600.so.1':
        'libvdpau_r600.so.1.0.0',
    'ubuntu16/usr/lib/mock-ubuntu-64-bit/vdpau/libvdpau_radeonsi.so':
        'libvdpau_radeonsi.so.1.0.0',
    'ubuntu16/usr/lib/mock-ubuntu-64-bit/vdpau/libvdpau_radeonsi.so.1':
        'libvdpau_radeonsi.so.1.0.0',
}.items():
    os.makedirs(os.path.dirname(name), mode=0o755, exist_ok=True)
    try:
        os.symlink(target, name)
    except FileExistsError:
        pass

with open('debian10/usr/lib/os-release', 'w') as writer:
    writer.write('''\
PRETTY_NAME="Debian GNU/Linux 10 (buster)"
NAME="Debian GNU/Linux"
VERSION_ID="10"
VERSION="10 (buster)"
VERSION_CODENAME=buster
ID=debian
HOME_URL="https://www.debian.org/"
SUPPORT_URL="https://www.debian.org/support"
BUG_REPORT_URL="https://bugs.debian.org/"
''')

with open('debian10/run/systemd/container', 'w') as writer:
    writer.write('whatever\n')

for name in (
    'debian-unstable/etc/os-release',
    'flatpak-example/run/host/os-release',
):
    with open(name, 'w') as writer:
        writer.write('''\
PRETTY_NAME="Debian GNU/Linux bullseye/sid"
NAME="Debian GNU/Linux"
ID=debian
HOME_URL="https://www.debian.org/"
SUPPORT_URL="https://www.debian.org/support"
BUG_REPORT_URL="https://bugs.debian.org/"
''')

with open('fedora/run/systemd/container', 'w') as writer:
    writer.write('docker\n')

with open('invalid-os-release/usr/lib/os-release', 'w') as writer:
    writer.write('''\
ID=steamrt
PRETTY_NAME="The first name"
VERSION_CODENAME
VERSION_ID="foo
PRETTY_NAME="The second name"
NAME="This file does not end with a newline"''')

for name in (
    'steamrt/usr/lib/os-release',
    'steamrt-overrides-issues/usr/lib/os-release',
):
    with open(name, 'w') as writer:
        writer.write('''\
NAME="Steam Runtime"
VERSION="1 (scout)"
ID=steamrt
ID_LIKE=ubuntu
PRETTY_NAME="Steam Runtime 1 (scout)"
VERSION_ID="1"
BUILD_ID="0.20190924.0"
VARIANT=Platform
VARIANT_ID="com.valvesoftware.steamruntime.platform-amd64_i386-scout"
''')

with open('steamrt-unofficial/usr/lib/os-release', 'w') as writer:
    writer.write('''\
NAME="Steam Runtime"
VERSION="1 (scout)"
ID=steamrt
ID_LIKE=ubuntu
PRETTY_NAME="Steam Runtime 1 (scout)"
VERSION_ID="1"
BUILD_ID="unofficial-0.20190924.0"
VARIANT=Platform
VARIANT_ID="com.valvesoftware.steamruntime.platform-amd64_i386-scout"
''')

with open('steamrt-unofficial/proc/1/cgroup', 'w') as writer:
    writer.write('''\
11:perf_event:/docker/9999999999999999999999999999999999999999999999999999999999999999
10:freezer:/docker/9999999999999999999999999999999999999999999999999999999999999999
9:memory:/docker/9999999999999999999999999999999999999999999999999999999999999999
8:rdma:/
7:devices:/docker/9999999999999999999999999999999999999999999999999999999999999999
6:blkio:/docker/9999999999999999999999999999999999999999999999999999999999999999
5:net_cls,net_prio:/docker/9999999999999999999999999999999999999999999999999999999999999999
4:cpu,cpuacct:/docker/9999999999999999999999999999999999999999999999999999999999999999
3:cpuset:/docker/9999999999999999999999999999999999999999999999999999999999999999
2:pids:/docker/9999999999999999999999999999999999999999999999999999999999999999
1:name=systemd:/docker/9999999999999999999999999999999999999999999999999999999999999999
0::/system.slice/docker.service
''')
