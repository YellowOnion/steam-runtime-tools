/*
 * Copyright © 2021 Collabora Ltd.
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

#include <stdio.h>
#include <glib.h>

#include "graphics-test-defines.h"

int
main (int argc,
      char **argv)
{
  fprintf (stderr, SRT_TEST_GOOD_VULKAN_MESSAGES);

  printf ("{\"device-info\":{"
          "\"device-name\":\"" SRT_TEST_GOOD_GRAPHICS_RENDERER "\","
          "\"device-type\":2,"
          "\"api-version\":\"" SRT_TEST_GOOD_GRAPHICS_API_VERSION "\","
          "\"driver-version\":\"" G_STRINGIFY (SRT_TEST_GOOD_GRAPHICS_DRIVER_VERSION_HEX) "\","
          "\"vendor-id\":\"" SRT_TEST_GOOD_GRAPHICS_VENDOR_ID "\","
          "\"device-id\":\"" SRT_TEST_GOOD_GRAPHICS_DEVICE_ID "\"}}\n"
          "{\"device-info\":{"
          "\"device-name\":\"" SRT_TEST_SOFTWARE_GRAPHICS_RENDERER "\","
          "\"device-type\":4,"
          "\"api-version\":\"" SRT_TEST_SOFTWARE_GRAPHICS_API_VERSION "\","
          "\"driver-version\":\"" G_STRINGIFY (SRT_TEST_SOFTWARE_GRAPHICS_DRIVER_VERSION_HEX) "\","
          "\"vendor-id\":\"" SRT_TEST_SOFTWARE_GRAPHICS_VENDOR_ID "\","
          "\"device-id\":\"" SRT_TEST_SOFTWARE_GRAPHICS_DEVICE_ID "\"}}\n"
          "{\"test\":{"
          "\"index\":0,"
          "\"can-draw\":false,"
          "\"error-message\":\"" SRT_TEST_MIXED_VULKAN_MESSAGES_1 "\"}}\n"
          "{\"test\":{"
          "\"index\":1,"
          "\"can-draw\":false,"
          "\"error-message\":\"" SRT_TEST_MIXED_VULKAN_MESSAGES_2 "\"}}\n");

  return 1;
}

