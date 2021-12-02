/*< internal_header >*/
/*
 * Copyright © 2019-2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

// Test strings for use in mock and graphics test
#define SRT_TEST_GOOD_GRAPHICS_RENDERER "AMD RADV NAVI10 (ACO)"
#define SRT_TEST_GOOD_GRAPHICS_API_VERSION "1.2.145"
#define SRT_TEST_GOOD_GRAPHICS_DRIVER_ID 3
#define SRT_TEST_GOOD_GRAPHICS_DRIVER_NAME "radv"
#define SRT_TEST_GOOD_GRAPHICS_DRIVER_VERSION "Mesa 20.3.3 (ACO)"
#define SRT_TEST_GOOD_GRAPHICS_DRIVER_VERSION_HEX 0x5003003
#define SRT_TEST_GOOD_GRAPHICS_VENDOR_ID "0x1002"
#define SRT_TEST_GOOD_GRAPHICS_DEVICE_ID "0x731f"

#define SRT_TEST_SOFTWARE_GRAPHICS_RENDERER "llvmpipe (LLVM 8.0, 256 bits)"
#define SRT_TEST_SOFTWARE_GRAPHICS_API_VERSION "1.0.2"
#define SRT_TEST_SOFTWARE_GRAPHICS_DRIVER_VERSION "0x1 (0.0.1?)"
#define SRT_TEST_SOFTWARE_GRAPHICS_DRIVER_VERSION_HEX 0x1
#define SRT_TEST_SOFTWARE_GRAPHICS_VENDOR_ID "0x10005"
#define SRT_TEST_SOFTWARE_GRAPHICS_DEVICE_ID "0"

#define SRT_TEST_GOOD_GRAPHICS_VERSION "3.0 Mesa 19.1.3"
#define SRT_TEST_SOFTWARE_GRAPHICS_VERSION "3.1 Mesa 19.1.3"
#define SRT_TEST_GOOD_VULKAN_MESSAGES "WARNING: lavapipe is not a conformant vulkan implementation, testing use only."
#define SRT_TEST_GOOD_VULKAN_VERSION SRT_TEST_GOOD_GRAPHICS_DRIVER_VERSION

#define SRT_TEST_MIXED_VULKAN_MESSAGES_1 "Failed to open file “./_build/helpers/shaders/vert.spv”: No such file or directory"
#define SRT_TEST_MIXED_VULKAN_MESSAGES_2 "vkWaitForFences (dev_props->device, 1, &dev_props->in_flight_fences[dev_props->current_frame], VK_TRUE, 1) failed: TIMEOUT (2)"
#define SRT_TEST_BAD_VULKAN_MESSAGES "failed to create window surface!\n"

#define SRT_TEST_GOOD_VDPAU_RENDERER "G3DVL VDPAU Driver Shared Library version 1.0\n"
#define SRT_TEST_BAD_VDPAU_MESSAGES "Failed to open VDPAU backend libvdpau_nvidia.so: cannot open shared object file: No such file or directory\n\
vdp_device_create_x11 (display, screen, &device, &vdp_get_proc_address) failed: 1\n"
#define SRT_TEST_GOOD_VAAPI_RENDERER "Mesa Gallium driver 20.0.4 for AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.3-arch1-1, LLVM 9.0.1)\n"
#define SRT_TEST_BAD_VAAPI_MESSAGES "libva error: vaGetDriverNameByIndex() failed with unknown libva error, driver_name = (null)\n\
vaInitialize (va_display, &major_version, &minor_version) failed: unknown libva error (-1)\n"
