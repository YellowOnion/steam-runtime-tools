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
import textwrap

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

supported_abis = ['aarch64-linux-gnu', 'i386-linux-gnu', 'x86_64-linux-gnu']

dirs = ''

# Only the leaf directories need to be listed here.
for abi in supported_abis:
    dirs = dirs + textwrap.dedent('''\
        debian10/usr/lib/{0}/dri
        debian10/usr/lib/{0}/vdpau
        fake-steam-runtime/usr/lib/steamrt/expectations/{0}
        steamrt/overrides/lib/{0}
        steamrt-overrides-issues/usr/lib/pressure-vessel/overrides/lib/{0}
        ''').format(abi)


dirs = dirs + '''
debian10/custom_path
debian10/expectations
debian10/home/debian/.local/share/vulkan/implicit_layer.d
debian10/usr/local/etc/vulkan/explicit_layer.d
debian10/usr/share/vulkan/implicit_layer.d
debian10/run/systemd
debian-unstable/etc
fake-icds/home/.config/vulkan/icd.d
fake-icds/home/.local/share/vulkan/icd.d
fake-icds/egl2
fake-icds/confdir/vulkan/icd.d
fake-icds/confhome/vulkan/icd.d
fake-icds/etc/egl/egl_external_platform.d
fake-icds/etc/glvnd/egl_vendor.d
fake-icds/etc/vulkan/icd.d
fake-icds/etc/xdg/vulkan/icd.d
fake-icds/datadir/vulkan/icd.d
fake-icds/egl1
fake-icds/opt
fake-icds/datahome/vulkan/icd.d
fake-icds/usr/lib/i386-mock-abi
fake-icds/usr/lib/x86_64-mock-abi/vulkan/icd.d
fake-icds/usr/lib/x86_64-mock-abi/GL/glvnd/egl_vendor.d
fake-icds/usr/lib/x86_64-mock-abi/GL/vulkan/icd.d
fake-icds/usr/local/share/vulkan/icd.d
fake-icds/usr/share/egl/egl_external_platform.d
fake-icds/usr/share/glvnd/egl_vendor.d
fake-icds/usr/share/vulkan/icd.d
fake-icds-flatpak/etc/glvnd/egl_vendor.d
fake-icds-flatpak/etc/vulkan/icd.d
fake-icds-flatpak/etc/xdg/vulkan/icd.d
fake-icds-flatpak/home/.local/share/vulkan/icd.d
fake-icds-flatpak/usr/lib/extensions/vulkan/share/vulkan/explicit_layer.d
fake-icds-flatpak/usr/lib/extensions/vulkan/share/vulkan/implicit_layer.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/explicit_layer.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/icd.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/implicit_layer.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/glvnd/egl_vendor.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/vulkan/explicit_layer.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/vulkan/icd.d
fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/vulkan/implicit_layer.d
fake-icds-flatpak/usr/lib/i386-mock-abi
fake-icds-flatpak/usr/local/share/vulkan/icd.d
fake-icds-flatpak/usr/share/glvnd/egl_vendor.d
fake-icds-flatpak/usr/share/vulkan/icd.d
fedora/custom_path
fedora/custom_path2
fedora/custom_path3
fedora/usr/lib/dri
fedora/usr/lib/vdpau
fedora/usr/lib64/dri
fedora/usr/lib64/vdpau
fedora/usr/share/vulkan/implicit_layer.d
fedora/run/systemd
fedora/sys/class/dmi/id
flatpak-example/usr/lib/dri
flatpak-example/usr/lib/x86_64-mock-abi/GL/lib/dri
flatpak-example/usr/lib/x86_64-mock-abi/dri
flatpak-example/usr/lib/x86_64-mock-abi/dri/intel-vaapi-driver
flatpak-example/usr/lib/x86_64-mock-abi/vdpau
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
podman-example/run/host
steamrt/etc
steamrt/overrides/bin
steamrt/usr/lib
steamrt/run/pressure-vessel
steamrt-overrides-issues/etc
steamrt-overrides-issues/usr/lib/pressure-vessel/overrides/bin
steamrt-overrides-issues/usr/lib
steamrt-unofficial/etc
steamrt-unofficial/usr/lib
steamrt-unofficial/proc/1
ubuntu16/usr/lib/dri
ubuntu16/usr/lib/x86_64-mock-ubuntu/dri
ubuntu16/usr/lib/x86_64-mock-ubuntu/mesa
ubuntu16/usr/lib/x86_64-mock-ubuntu/vdpau
ubuntu16/sys/class/dmi/id
'''

for name in dirs.split():
    os.makedirs(name, mode=0o755, exist_ok=True)

files = ''

for abi in supported_abis:
    files = files + textwrap.dedent('''\
        debian10/usr/lib/{0}/ld.so
        debian10/usr/lib/{0}/dri/i965_dri.so
        debian10/usr/lib/{0}/dri/r600_drv_video.so
        debian10/usr/lib/{0}/libEGL_mesa.so.0
        debian10/usr/lib/{0}/libGLX_mesa.so.0
        debian10/usr/lib/{0}/libva.so.2
        debian10/usr/lib/{0}/libvdpau.so.1
        debian10/usr/lib/{0}/vdpau/libvdpau_radeonsi.so.1.0.0
        ''').format(abi)

files = files + '''
debian10/usr/lib/i386-linux-gnu/dri/r300_dri.so
debian10/usr/lib/i386-linux-gnu/dri/radeonsi_dri.so
debian10/usr/lib/i386-linux-gnu/libGLX_nvidia.so.0
debian10/usr/lib/i386-linux-gnu/vdpau/libvdpau_r600.so
debian10/usr/lib/x86_64-linux-gnu/dri/r600_dri.so
debian10/usr/lib/x86_64-linux-gnu/dri/radeon_dri.so
debian10/usr/lib/x86_64-linux-gnu/dri/radeonsi_drv_video.so
debian10/usr/lib/x86_64-linux-gnu/libGL.so.1
debian10/usr/lib/x86_64-linux-gnu/vdpau/libvdpau_r600.so.1.0.0
debian-unstable/.dockerenv
fake-icds/egl1/a.json
fake-icds/egl1/b.json
fake-icds/egl1/BBB.json
fake-icds/etc/egl/egl_external_platform.d/invalid.json
fake-icds/etc/glvnd/egl_vendor.d/invalid.json
fake-icds/etc/xdg/vulkan/icd.d/invalid.json
fake-icds/etc/xdg/vulkan/icd.d/invalid.txt
fake-icds/opt/libEGL_myvendor.so
fake-icds/usr/lib/i386-mock-abi/libvulkan_intel.so
fake-icds/usr/lib/x86_64-mock-abi/libEGL_mesa.so.0
fake-icds/usr/lib/x86_64-mock-abi/libvulkan_basename.so
fake-icds/usr/lib/x86_64-mock-abi/libvulkan_intel.so
fake-icds/usr/lib/x86_64-mock-abi/libz.so.1


fake-icds-flatpak/etc/glvnd/egl_vendor.d/invalid.json
fake-icds-flatpak/etc/xdg/vulkan/icd.d/invalid.json
fake-icds-flatpak/etc/xdg/vulkan/icd.d/invalid.txt
fake-icds-flatpak/usr/lib/i386-mock-abi/libvulkan_intel.so
fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/libvulkan_relative.so
fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/glvnd/libEGL_relative.so
fake-icds-flatpak/usr/lib/x86_64-mock-abi/libvulkan_intel.so
fake-icds-flatpak/.flatpak-info
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
fedora/usr/lib32/libMangoHud.so
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
flatpak-example/usr/lib/dri/r300_dri.so
flatpak-example/usr/lib/dri/r600_drv_video.so
flatpak-example/usr/lib/x86_64-mock-abi/GL/lib/dri/i965_dri.so
flatpak-example/usr/lib/x86_64-mock-abi/GL/lib/dri/r600_drv_video.so
flatpak-example/usr/lib/x86_64-mock-abi/dri/intel-vaapi-driver/i965_drv_video.so
flatpak-example/usr/lib/x86_64-mock-abi/dri/radeonsi_drv_video.so
flatpak-example/usr/lib/x86_64-mock-abi/libEGL_mesa.so.0
flatpak-example/usr/lib/x86_64-mock-abi/libva.so.2
flatpak-example/usr/lib/x86_64-mock-abi/libvdpau.so.1
flatpak-example/usr/lib/x86_64-mock-abi/vdpau/libvdpau_radeonsi.so.1
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
steamrt-overrides-issues/usr/lib/pressure-vessel/overrides/bin/.keep
steamrt-overrides-issues/usr/lib/pressure-vessel/overrides/lib/i386-linux-gnu/.keep
ubuntu16/lib64/ld-linux-x86-64.so.2
ubuntu16/usr/lib/dri/radeonsi_dri.so
ubuntu16/usr/lib/x86_64-mock-ubuntu/dri/i965_dri.so
ubuntu16/usr/lib/x86_64-mock-ubuntu/dri/radeon_dri.so
ubuntu16/usr/lib/x86_64-mock-ubuntu/dri/radeonsi_drv_video.so
ubuntu16/usr/lib/x86_64-mock-ubuntu/libva.so.1
ubuntu16/usr/lib/x86_64-mock-ubuntu/mesa/libGL.so.1
ubuntu16/usr/lib/x86_64-mock-ubuntu/vdpau/libvdpau_r600.so.1.0.0
ubuntu16/usr/lib/x86_64-mock-ubuntu/vdpau/libvdpau_radeonsi.so.1.0.0
'''

for name in files.split():
    os.makedirs(os.path.dirname(name), mode=0o755, exist_ok=True)
    open(name, 'w').close()

for name, target in {
    'debian10/lib/ld-linux.so.2':
        '/usr/lib/i386-linux-gnu/ld.so',
    'debian10/lib64/ld-linux-x86-64.so.2':
        '../usr/lib/x86_64-linux-gnu/ld.so',
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
    ('steamrt-overrides-issues/usr/lib/pressure-vessel/'
     + 'overrides/lib/x86_64-linux-gnu/libgcc_s.so.1'):
        '/run/host/usr/lib/libgcc_s.so.1',
    'steamrt-unofficial/etc/os-release':
        '../usr/lib/os-release',
    'ubuntu16/usr/lib/x86_64-mock-ubuntu/vdpau/libvdpau_r600.so.1':
        'libvdpau_r600.so.1.0.0',
    'ubuntu16/usr/lib/x86_64-mock-ubuntu/vdpau/libvdpau_radeonsi.so':
        'libvdpau_radeonsi.so.1.0.0',
    'ubuntu16/usr/lib/x86_64-mock-ubuntu/vdpau/libvdpau_radeonsi.so.1':
        'libvdpau_radeonsi.so.1.0.0',
}.items():
    os.makedirs(os.path.dirname(name), mode=0o755, exist_ok=True)
    try:
        os.symlink(target, name)
    except FileExistsError:
        pass

with open('flatpak-example/.flatpak-info', 'w') as writer:
    writer.write('''\
[Application]
name=com.valvesoftware.Steam
runtime=runtime/org.freedesktop.Platform/x86_64/20.08

[Instance]
branch=stable
arch=x86_64
flatpak-version=1.10.2
session-bus-proxy=true
system-bus-proxy=true
devel=true''')

with open('podman-example/run/host/container-manager', 'w') as writer:
    writer.write("podman")

with open('debian10/custom_path/Single-good-layer.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.2.0",
  "layer" : {
    "name" : "VK_LAYER_LUNARG_overlay",
    "type" : "INSTANCE",
    "library_path" : "vkOverlayLayer.so",
    "api_version" : "1.1.5",
    "implementation_version" : "2",
    "description" : "LunarG HUD layer",
    "functions" : {
      "vkNegotiateLoaderLayerInterfaceVersion" : "OverlayLayer_NegotiateLoaderLayerInterfaceVersion"
    },
    "instance_extensions" : [
      {
        "name" : "VK_EXT_debug_report",
        "spec_version" : "1"
      },
      {
        "name" : "VK_VENDOR_ext_x",
        "spec_version" : "3"
      }
    ],
    "device_extensions" : [
      {
        "name" : "VK_EXT_debug_marker",
        "spec_version" : "1",
        "entrypoints" : [
          "vkCmdDbgMarkerBegin",
          "vkCmdDbgMarkerEnd"
        ]
      }
    ],
    "enable_environment" : {
      "ENABLE_LAYER_OVERLAY_1" : "1"
    },
    "disable_environment" : {
      "DISABLE_LAYER_OVERLAY_1" : ""
    }
  }
}''')

with open('debian10/expectations/MultiLayers_part1.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.1",
  "layer" : {
    "name" : "VK_LAYER_first",
    "type" : "INSTANCE",
    "library_path" : "libFirst.so",
    "api_version" : "1.0.13",
    "implementation_version" : "1",
    "description" : "Vulkan first layer"
  }
}''')

with open('debian10/expectations/MultiLayers_part2.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.1",
  "layer" : {
    "name" : "VK_LAYER_second",
    "type" : "INSTANCE",
    "library_path" : "libSecond.so",
    "api_version" : "1.0.13",
    "implementation_version" : "1",
    "description" : "Vulkan second layer"
  }
}''')

# MangoHUD uses a library_path of "/usr/\$LIB/libMangoHud.so"
# JSON-GLib will parse it as "/usr/$LIB/libMangoHud.so", so if we write again
# the JSON, the '$' will not be escaped anymore.
# With some manual testing I can confirm that escaping '$' has no real
# effect, so this should not be a problem in practice.
with open('debian10/custom_path/MangoHud.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_MANGOHUD_overlay",
    "type" : "GLOBAL",
    "library_path" : "/usr/\\$LIB/libMangoHud.so",
    "api_version" : "1.2.135",
    "implementation_version" : "1",
    "description" : "Vulkan Hud Overlay",
    "functions" : {
      "vkGetInstanceProcAddr" : "overlay_GetInstanceProcAddr",
      "vkGetDeviceProcAddr" : "overlay_GetDeviceProcAddr"
    },
    "enable_environment" : {
      "MANGOHUD" : "1"
    },
    "disable_environment" : {
      "DISABLE_MANGOHUD" : "1"
    }
  }
}''')

with open('debian10/expectations/MangoHud.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_MANGOHUD_overlay",
    "type" : "GLOBAL",
    "library_path" : "/usr/$LIB/libMangoHud.so",
    "api_version" : "1.2.135",
    "implementation_version" : "1",
    "description" : "Vulkan Hud Overlay",
    "functions" : {
      "vkGetInstanceProcAddr" : "overlay_GetInstanceProcAddr",
      "vkGetDeviceProcAddr" : "overlay_GetDeviceProcAddr"
    },
    "enable_environment" : {
      "MANGOHUD" : "1"
    },
    "disable_environment" : {
      "DISABLE_MANGOHUD" : "1"
    }
  }
}''')

with open('debian10/usr/local/etc/vulkan/explicit_layer.d/VkLayer_MESA_overlay.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_MESA_overlay",
    "type" : "GLOBAL",
    "library_path" : "libVkLayer_MESA_overlay.so",
    "api_version" : "1.1.73",
    "implementation_version" : "1",
    "description" : "Mesa Overlay layer"
  }
}''')

with open('debian10/usr/share/vulkan/implicit_layer.d/MultiLayers.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.1",
  "layers" : [
    {
      "name" : "VK_LAYER_first",
      "type" : "INSTANCE",
      "api_version" : "1.0.13",
      "library_path" : "libFirst.so",
      "implementation_version" : "1",
      "description" : "Vulkan first layer"
    },
    {
      "name" : "VK_LAYER_second",
      "type" : "INSTANCE",
      "api_version" : "1.0.13",
      "library_path" : "libSecond.so",
      "implementation_version" : "1",
      "description" : "Vulkan second layer"
    }
  ]
}''')

with open('debian10/home/debian/.local/share/vulkan/implicit_layer.d/steamoverlay_x86_64.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_VALVE_steam_overlay_64",
    "type" : "GLOBAL",
    "library_path" : "/home/debian/.local/share/Steam/ubuntu12_64/steamoverlayvulkanlayer.so",
    "api_version" : "1.2.136",
    "implementation_version" : "1",
    "description" : "Steam Overlay Layer",
    "enable_environment" : {
      "ENABLE_VK_LAYER_VALVE_steam_overlay_1" : "1"
    },
    "disable_environment" : {
      "DISABLE_VK_LAYER_VALVE_steam_overlay_1" : "1"
    }
  }
}''')

with open('fake-icds/false.json', 'w') as writer:
    writer.write('''false''')

with open('fake-icds/no-api-version.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "library_path": "libfoo.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/added.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "libadded.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/no-library.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/null.json', 'w') as writer:
    writer.write('''null''')

with open('fake-icds/str.json', 'w') as writer:
    writer.write('''"hello, world!"''')

with open('fake-icds/confdir/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/x86_64-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.1.0"
}''')

with open('fake-icds/confhome/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/x86_64-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.1.0"
}''')

with open('fake-icds/datadir/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/x86_64-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "2.0.0"
}''')

with open('fake-icds/datahome/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "library_path": "/usr/lib/x86_64-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/home/.config/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''{ }''')

with open('fake-icds/home/.local/share/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''{ }''')

with open('fake-icds/egl2/absolute.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "/opt/libEGL_myvendor.so"
    }
}''')

with open('fake-icds/egl1/AAA.json', 'w') as writer:
    writer.write('''hello world!''')

with open('fake-icds/egl1/soname_zlib_dup.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libz.so.1"
    }
}''')

with open('fake-icds/egl1/z.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "9000.0.0"
}''')

with open('fake-icds/egl2/soname_zlib.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libz.so.1"
    }
}''')

with open('fake-icds/etc/vulkan/icd.d/basename.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.2.3",
        "library_path": "libvulkan_basename.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/usr/lib/x86_64-mock-abi/vulkan/icd.d/relative.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.1",
        "library_path": "../libvulkan_relative.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/usr/lib/x86_64-mock-abi/GL/glvnd/egl_vendor.d/relative.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "library_path": "../libEGL_relative.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/usr/lib/x86_64-mock-abi/GL/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''[]''')

with open('fake-icds/usr/local/share/vulkan/icd.d/intel_icd.i686.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/i386-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds/usr/share/egl/egl_external_platform.d/10_nvidia_wayland.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libnvidia-egl-wayland.so.1"
    }
}''')

with open('fake-icds/usr/share/glvnd/egl_vendor.d/50_mesa.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libEGL_mesa.so.0"
    }
}''')

with open('fake-icds/usr/share/vulkan/icd.d/intel_icd.x86_64.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/x86_64-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds-flatpak/home/.local/share/vulkan/icd.d/relative_new.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.2",
        "library_path": "/usr/lib/x86_64-mock-abi/vulkan/icd.d/../libvulkan_relative.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds-flatpak/etc/vulkan/icd.d/basename.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.2.3",
        "library_path": "libvulkan_basename.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds-flatpak/false.json', 'w') as writer:
    writer.write('''false''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/icd.d/relative.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.1",
        "library_path": "../libvulkan_relative.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds-flatpak/usr/lib/extensions/vulkan/share/vulkan/explicit_layer.d/mr3398.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_MESA_overlay",
        "type": "GLOBAL",
        "api_version": "1.1.73",
        "library_path": "libVkLayer_MESA_overlay.so",
        "implementation_version": "1",
        "description": "Mesa Overlay layer"
    }
}''')

with open('fake-icds-flatpak/usr/lib/extensions/vulkan/share/vulkan/implicit_layer.d/mr3398.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_MANGOHUD_overlay",
        "type": "GLOBAL",
        "api_version": "1.2.135",
        "library_path": "/usr/lib/extensions/vulkan/\$LIB/mangohud/libMangoHud.so",
        "description": "Vulkan Hud Overlay",
        "implementation_version": "1",
        "functions": {
             "vkGetInstanceProcAddr": "overlay_GetInstanceProcAddr",
             "vkGetDeviceProcAddr": "overlay_GetDeviceProcAddr"
        },
        "enable_environment": {
            "MANGOHUD": "1"
        },
        "disable_environment": {
            "DISABLE_MANGOHUD": "1"
        }
    }
}''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/explicit_layer.d/runtime.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_RUNTIME_explicit",
        "type": "GLOBAL",
        "api_version": "1.1.73",
        "library_path": "libVkLayer_RUNTIME_explicit.so",
        "implementation_version": "1",
        "description": "Runtime's explicit layer"
    }
}''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/vulkan/implicit_layer.d/runtime.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_RUNTIME_implicit",
        "type": "GLOBAL",
        "api_version": "1.2.135",
        "library_path": "/usr/\$LIB/implicit/libLayer.so",
        "description": "Runtime's implicit layer",
        "implementation_version": "1",
        "functions": {
             "vkGetInstanceProcAddr": "overlay_GetInstanceProcAddr",
             "vkGetDeviceProcAddr": "overlay_GetDeviceProcAddr"
        },
        "enable_environment": {
            "IMPLICIT": "1"
        },
        "disable_environment": {
            "DISABLE_IMPLICIT": "1"
        }
    }
}''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/glvnd/egl_vendor.d/relative.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "library_path": "../libEGL_relative.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/vulkan/explicit_layer.d/glext.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_GLEXT_explicit",
        "type": "GLOBAL",
        "api_version": "1.1.73",
        "library_path": "libVkLayer_GLEXT_explicit.so",
        "implementation_version": "1",
        "description": "GL extension's explicit layer"
    }
}''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/vulkan/icd.d/invalid.json', 'w') as writer:
    writer.write('''[]''')

with open('fake-icds-flatpak/usr/lib/x86_64-mock-abi/GL/vulkan/implicit_layer.d/glext.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version": "1.0.0",
    "layer": {
        "name": "VK_LAYER_GLEXT_implicit",
        "type": "GLOBAL",
        "api_version": "1.2.135",
        "library_path": "/usr/\$LIB/GL/implicit/libLayer.so",
        "description": "GL extension's implicit layer",
        "implementation_version": "1",
        "functions": {
             "vkGetInstanceProcAddr": "overlay_GetInstanceProcAddr",
             "vkGetDeviceProcAddr": "overlay_GetDeviceProcAddr"
        },
        "enable_environment": {
            "IMPLICIT": "1"
        },
        "disable_environment": {
            "DISABLE_IMPLICIT": "1"
        }
    }
}''')

with open('fake-icds-flatpak/usr/local/share/vulkan/icd.d/intel_icd.i686.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/i386-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.0.0"
}''')

with open('fake-icds-flatpak/usr/share/glvnd/egl_vendor.d/50_mesa.json', 'w') as writer:
    writer.write('''\
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libEGL_mesa.so.0"
    }
}''')

with open('fake-icds-flatpak/usr/share/vulkan/icd.d/intel_icd.x86_64.json', 'w') as writer:
    writer.write('''\
{
    "ICD": {
        "api_version": "1.1.102",
        "library_path": "/usr/lib/x86_64-mock-abi/libvulkan_intel.so"
    },
    "file_format_version": "1.0.0"
}''')

for abi in supported_abis:
    symbols = 'fake-steam-runtime/usr/lib/steamrt/expectations/{}/libglib2.0-0.symbols'.format(abi)
    with open(symbols, 'w') as writer:
        writer.write(textwrap.dedent('''\
            # Cut-down version of libglib2.0-0:amd64.symbols, to illustrate what we expect
            # to find here
            libgio-2.0.so.0 libglib2.0-0 #MINVER#
            * Build-Depends-Package: libglib2.0-dev
             g_action_activate@Base 2.28.0
             g_action_change_state@Base 2.30.0
            libglib-2.0.so.0 libglib2.0-0 #MINVER#
            * Build-Depends-Package: libglib2.0-dev
             g_access@Base 2.12.0
             g_allocator_free@Base 2.12.0
             g_allocator_new@Base 2.12.0
            '''))

    symbols = 'fake-steam-runtime/usr/lib/steamrt/expectations/{}/libtheora0.symbols'.format(abi)
    with open(symbols, 'w') as writer:
        writer.write(textwrap.dedent('''\
            # Cut-down version of libtheora0:amd64.symbols, to illustrate what we expect
            # to find here
            libtheoraenc.so.1 libtheora0 #MINVER#
            # No symbols listed here yet
            '''))

    symbols = 'fake-steam-runtime/usr/lib/steamrt/expectations/{}/zlib1g.symbols'.format(abi)
    with open(symbols, 'w') as writer:
        writer.write(textwrap.dedent('''\
            # Cut-down version of zlib1g:amd64.symbols, to illustrate what we expect
            # to find here
            libz.so.1 zlib1g #MINVER#
             adler32@Base 1:1.1.4
            '''))

with open('fake-steam-runtime/usr/lib/steamrt/steam-runtime-abi.json', 'w') as writer:
    writer.write('''\
{
 "architectures": {
  "i386-linux-gnu": {
   "dpkg_name": "i386"
  },
  "x86_64-linux-gnu": {
   "dpkg_name": "amd64"
  }
 },
 "extra_debs": {
  "libs": [
   "dconf-gsettings-backend",
   "gtk2-engines"
  ]
 },
 "private_libraries": [
  {
   "libVkLayer_*.so": {
    "deb": "libvulkan1"
   }
  }
 ],
 "shared_libraries": [
  {
   "libglut.so.3": {
    "deb": "freeglut3"
   }
  },
  "libacl.so.1",
  {
   "libtheoraenc.so.1": {
    "deb": "libtheora0",
    "hidden_dependencies": [
     "libtheoradec.so.1"
    ]
   }
  },
  {
   "libtWithHiddens.so.1": {
    "deb": "libtWithHiddens0",
    "hidden_dependencies": [
     "firstHidden.so.0",
     "secondHidden.so.3"
    ]
   }
  }
 ]
}''')

with open('fedora/usr/share/vulkan/implicit_layer.d/incomplete_layer.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.1.2",
  "layer" : {
    "name" : "VK_LAYER_VALVE_steam_overlay_64",
    "type" : "GLOBAL",
    "library_path" : "/home/debian/.local/share/Steam/ubuntu12_64/steamoverlayvulkanlayer.so",
    "implementation_version" : "1",
    "description" : "Steam Overlay Layer"
  }
}''')

with open('fedora/usr/share/vulkan/implicit_layer.d/newer_layer.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "99.1.2",
  "layer" : {
    "name" : "VK_LAYER_from_a_distant_future"
  }
}''')

with open('fedora/custom_path/meta_layer.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.1.1",
  "layer" : {
    "name" : "VK_LAYER_META_layer",
    "type" : "GLOBAL",
    "api_version" : "1.0.9000",
    "implementation_version" : "1",
    "description" : "Meta-layer example",
    "component_layers" : [
      "VK_LAYER_KHRONOS_validation",
      "VK_LAYER_LUNARG_api_dump"
    ]
  }
}''')

with open('fedora/custom_path2/MangoHud.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_MANGOHUD_overlay",
    "type" : "GLOBAL",
    "library_path" : "/usr/\\$LIB/libMangoHud.so",
    "api_version" : "1.2.135",
    "implementation_version" : "1",
    "description" : "Vulkan Hud Overlay"
  }
}''')

with open('fedora/custom_path3/MangoHud_i386.json', 'w') as writer:
    writer.write('''\
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_MANGOHUD_overlay",
    "type" : "GLOBAL",
    "library_path" : "/usr/lib32/libMangoHud.so",
    "api_version" : "1.2.135",
    "implementation_version" : "1",
    "description" : "Vulkan Hud Overlay"
  }
}''')

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

with open('fedora/sys/class/dmi/id/product_name', 'w') as writer:
    writer.write('VirtualBox machine\n')

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

with open('ubuntu16/sys/class/dmi/id/sys_vendor', 'w') as writer:
    writer.write('QEMU\n')
