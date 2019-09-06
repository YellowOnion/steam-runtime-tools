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

#include <stdio.h>

#include "../steam-runtime-tools/graphics-test-defines.h"

int
main (int argc,
      char **argv)
{
  // Give good output
  printf ("{\n\t\"$schema\": \"https://schema.khronos.org/vulkan/devsim_1_0_0.json#\",\n"
          "\t\"comments\": {\n\t\t\"desc\": \"JSON configuration file describing GPU 0. Generated using the vulkaninfo program.\","
          "\"vulkanApiVersion\": \"1.1.121\"\n\t},\n\t\"ArrayOfVkLayerProperties\": [\n\t\t{\n"
          "\t\t\t\"layerName\": \"VK_LAYER_LUNARG_standard_validation\",\n"
          "\t\t\t\"specVersion\": 4194425,\n"
          "\t\t\t\"implementationVersion\": 1,\n"
          "\t\t\t\"description\": \"LunarG Standard Validation Layer\"\n"
          "\t\t},\n"
          "\t\t{\n"
          "\t\t\t\"layerName\": \"VK_LAYER_VALVE_steam_fossilize_32\",\n"
          "\t\t\t\"specVersion\": 4198473,\n"
          "\t\t\t\"implementationVersion\": 1,\n"
          "\t\t\t\"description\": \"Steam Pipeline Caching Layer\"\n"
          "\t\t},\n"
          "\t\t{\n"
          "\t\t\t\"layerName\": \"VK_LAYER_VALVE_steam_fossilize_64\",\n"
          "\t\t\t\"specVersion\": 4198473,\n"
          "\t\t\t\"implementationVersion\": 1,\n"
          "\t\t\t\"description\": \"Steam Pipeline Caching Layer\"\n"
          "\t\t},\n"
          "\t\t{\n"
          "\t\t\t\"layerName\": \"VK_LAYER_VALVE_steam_overlay_32\",\n"
          "\t\t\t\"specVersion\": 4198473,\n"
          "\t\t\t\"implementationVersion\": 1,\n"
          "\t\t\t\"description\": \"Steam Overlay Layer\"\n"
          "\t\t},\n"
          "\t\t{\n"
          "\t\t\t\"layerName\": \"VK_LAYER_VALVE_steam_overlay_64\",\n"
          "\t\t\t\"specVersion\": 4198473,\n"
          "\t\t\t\"implementationVersion\": 1,\n"
          "\t\t\t\"description\": \"Steam Overlay Layer\"\n"
          "\t\t}\n"
          "\t],\n"
          "\t\"VkPhysicalDeviceProperties\": {\n"
          "\t\t\"apiVersion\": 4198502,\n"
          "\t\t\"driverVersion\": "
          SRT_TEST_GOOD_VULKAN_DRIVER_VERSION
          ",\n"
          "\t\t\"vendorID\": 32902,\n"
          "\t\t\"deviceID\": 1042,\n"
          "\t\t\"deviceType\": 1,\n"
          "\t\t\"deviceName\": \""
          SRT_TEST_GOOD_GRAPHICS_RENDERER
          "\"\n"
          "\t}\n"
          "}\n");
  return 0;
}

