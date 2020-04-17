/*< internal_header >*/
/*
 * Copyright © 2019 Collabora Ltd.
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
#define SRT_TEST_GOOD_GRAPHICS_RENDERER "Mesa DRI Intel(R) Haswell Desktop "
#define SRT_TEST_SOFTWARE_GRAPHICS_RENDERER "llvmpipe (LLVM 8.0, 256 bits)"
#define SRT_TEST_GOOD_GRAPHICS_VERSION "3.0 Mesa 19.1.3"
#define SRT_TEST_SOFTWARE_GRAPHICS_VERSION "3.1 Mesa 19.1.3"
#define SRT_TEST_GOOD_VULKAN_DRIVER_VERSION "79695877"
#define SRT_TEST_GOOD_VULKAN_VERSION "1.1.102 (device 8086:0412) (driver 19.1.5)"
#define SRT_TEST_GOOD_VDPAU_RENDERER "G3DVL VDPAU Driver Shared Library version 1.0\n"
#define SRT_TEST_BAD_VDPAU_MESSAGES "Failed to open VDPAU backend libvdpau_nvidia.so: cannot open shared object file: No such file or directory\n\
vdp_device_create_x11 (display, screen, &device, &vdp_get_proc_address) failed: 1\n"
#define SRT_TEST_GOOD_VAAPI_RENDERER "Mesa Gallium driver 20.0.4 for AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.3-arch1-1, LLVM 9.0.1)\n"
#define SRT_TEST_BAD_VAAPI_MESSAGES "libva error: vaGetDriverNameByIndex() failed with unknown libva error, driver_name = (null)\n\
vaInitialize (va_display, &major_version, &minor_version) failed: unknown libva error (-1)\n"
