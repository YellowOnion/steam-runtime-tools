/*
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 * Copyright © 2015 Intel Corporation
 * Copyright © 2019-2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Parts of this code were adapted from vulkan-tutorial.com which is
 * Licensed as CC0 1.0 Universal. Other parts were adapted from vkcube which
 * is licensed as Apache 2.0
 */

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/utils-internal.h>

static const int WIDTH = 200;
static const int HEIGHT = 200;

static gboolean opt_print_version = FALSE;
static gboolean opt_visible = FALSE;

const int MAX_FRAMES_IN_FLIGHT = 2;

static const char *argv0;

#define do_vk(expr, error) (_do_vk (#expr, expr, error))

static gboolean
_do_vk (const char *description,
        VkResult result,
        GError **error)
{
  if (result != VK_SUCCESS)
    return glnx_throw (error, "%s failed: %#X", description, result);

  return TRUE;
}

static const char *device_extensions[] =
{
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct _QueueFamilyIndices
{
  uint32_t graphicsFamily;
  uint32_t presentFamily;
  gboolean hasGraphicsFamily;
  gboolean hasPresentFamily;
};

typedef struct _QueueFamilyIndices QueueFamilyIndices;

struct _SwapChainSupportDetails
{
  VkSurfaceCapabilitiesKHR capabilities;
  uint32_t format_count;
  uint32_t mode_count;
  VkSurfaceFormatKHR *formats;
  VkPresentModeKHR *present_modes;
};

typedef struct _SwapChainSupportDetails SwapChainSupportDetails;

struct _VulkanPhysicalDeviceProperties
{
  VkCommandBuffer *command_buffers;
  VkCommandPool command_pool;
  VkFence *in_flight_fences;
  VkSurfaceKHR surface;
  VkDevice device;
  VkExtent2D swapchain_extent;
  VkFormat swapchain_image_format;
  VkImage *swapchain_images;
  VkImageView *swapchain_image_views;
  VkPipeline graphics_pipeline;
  VkPipelineLayout pipeline_layout;
  VkRenderPass render_pass;
  VkSemaphore *image_available_semaphores;
  VkSemaphore *render_finished_semaphores;
  VkSwapchainKHR swapchain;
  VkQueue graphics_queue;
  VkQueue present_queue;
  uint32_t framebuffer_size;
  uint32_t current_frame;
  VkFramebuffer *swapchain_framebuffers;
};

typedef struct _VulkanPhysicalDeviceProperties VulkanPhysicalDeviceProperties;

static gboolean
queue_family_indices_is_complete (QueueFamilyIndices indices)
{
  return indices.hasGraphicsFamily && indices.hasPresentFamily;
}

static xcb_atom_t
get_atom (struct xcb_connection_t *conn,
          const char *name)
{
  xcb_intern_atom_cookie_t cookie;
  g_autofree xcb_intern_atom_reply_t *reply = NULL;
  xcb_atom_t atom;

  cookie = xcb_intern_atom (conn, 0, strlen (name), name);
  reply = xcb_intern_atom_reply (conn, cookie, NULL);
  if (reply)
    atom = reply->atom;
  else
    atom = XCB_NONE;

  return atom;
}

static gboolean
create_instance (VkInstance *vk_instance,
                 GError **error)
{
  g_return_val_if_fail (vk_instance != NULL, FALSE);

  *vk_instance = VK_NULL_HANDLE;

  static const char *required_extensions[] =
  {
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME,
  };

  const VkApplicationInfo app =
  {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = argv0,
    .applicationVersion = VK_MAKE_VERSION (1, 0, 0),
    .pEngineName = "No Engine",
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo inst_info =
  {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = NULL,
    .pApplicationInfo = &app,
    .enabledLayerCount = 0,
    .enabledExtensionCount = G_N_ELEMENTS (required_extensions),
    .ppEnabledExtensionNames = required_extensions,
  };

  if (!do_vk (vkCreateInstance (&inst_info, NULL, vk_instance), error))
    return FALSE;

  return TRUE;
}

static gboolean
create_surface (VkInstance vk_instance,
                VkPhysicalDevice physical_device,
                VulkanPhysicalDeviceProperties *dev_props,
                GError **error)
{
  xcb_connection_t *xcb_connection;
  xcb_screen_iterator_t iter;
  static const char title[] = "Vulkan Test";

  xcb_connection = xcb_connect (0, 0);
  if (xcb_connection_has_error (xcb_connection))
    return glnx_throw (error, "Unable to initialize xcb connection");

  uint32_t xcb_window = xcb_generate_id (xcb_connection);

  uint32_t window_values[] = {
    XCB_EVENT_MASK_EXPOSURE |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY |
    XCB_EVENT_MASK_KEY_PRESS
  };

  iter = xcb_setup_roots_iterator (xcb_get_setup (xcb_connection));

  xcb_create_window (xcb_connection,
                     XCB_COPY_FROM_PARENT,
                     xcb_window,
                     iter.data->root,
                     0, 0,
                     WIDTH,
                     HEIGHT,
                     0,
                     XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     iter.data->root_visual,
                     XCB_CW_EVENT_MASK, window_values);

  xcb_atom_t atom_wm_protocols = get_atom (xcb_connection, "WM_PROTOCOLS");
  xcb_atom_t atom_wm_delete_window = get_atom (xcb_connection, "WM_DELETE_WINDOW");
  xcb_change_property (xcb_connection,
                       XCB_PROP_MODE_REPLACE,
                       xcb_window,
                       atom_wm_protocols,
                       XCB_ATOM_ATOM,
                       32,
                       1, &atom_wm_delete_window);

  xcb_change_property (xcb_connection,
                       XCB_PROP_MODE_REPLACE,
                       xcb_window,
                       get_atom (xcb_connection, "_NET_WM_NAME"),
                       get_atom (xcb_connection, "UTF8_STRING"),
                       8,
                       strlen (title), title);

  // we don't normally want this test to be visible to the user
  if (opt_visible)
    xcb_map_window (xcb_connection, xcb_window);

  xcb_flush (xcb_connection);

  PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR get_xcb_presentation_support =
    (PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)
    vkGetInstanceProcAddr (vk_instance, "vkGetPhysicalDeviceXcbPresentationSupportKHR");
  PFN_vkCreateXcbSurfaceKHR create_xcb_surface =
    (PFN_vkCreateXcbSurfaceKHR)
    vkGetInstanceProcAddr (vk_instance, "vkCreateXcbSurfaceKHR");

  if (!get_xcb_presentation_support (physical_device, 0,
                                     xcb_connection,
                                     iter.data->root_visual))
    return glnx_throw (error, "Vulkan not supported on given X window");

  VkXcbSurfaceCreateInfoKHR createSurfaceInfo =
  {
    .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .connection = xcb_connection,
    .window = xcb_window,
  };

  if (!do_vk (create_xcb_surface (vk_instance, &createSurfaceInfo, NULL, &dev_props->surface),
              error))
    return FALSE;

  return TRUE;
}

static gboolean
get_physical_devices (VkInstance instance,
                      uint32_t *physical_device_count,
                      VkPhysicalDevice **physical_devices,
                      GError **error)
{
  g_return_val_if_fail (physical_device_count != NULL, FALSE);
  g_return_val_if_fail (physical_devices != NULL && *physical_devices == NULL, FALSE);

  if (!do_vk (vkEnumeratePhysicalDevices (instance, physical_device_count, NULL), error))
    return FALSE;

  g_debug ("Physical device count: %i\n", *physical_device_count);
  if (*physical_device_count == 0)
    return glnx_throw (error, "Failed to find GPUs with Vulkan support!");

  *physical_devices = g_new0 (VkPhysicalDevice, *physical_device_count);

  if (!do_vk (vkEnumeratePhysicalDevices (instance, physical_device_count, *physical_devices),
              error))
    return FALSE;

  return TRUE;
}

QueueFamilyIndices find_queue_families (VkPhysicalDevice dev,
                                        VkSurfaceKHR surface);

static gboolean
create_logical_device (VkPhysicalDevice physical_device,
                       VulkanPhysicalDeviceProperties *dev_props,
                       GError **error)
{
  uint32_t i;
  QueueFamilyIndices indices = find_queue_families (physical_device, dev_props->surface);
  uint32_t unique_queue_families[] =
  {
    indices.graphicsFamily,
    indices.presentFamily
  };
  VkDeviceQueueCreateInfo *queueCreateInfos = g_new0 (VkDeviceQueueCreateInfo,
                                                      G_N_ELEMENTS (unique_queue_families));
  float queuePriority = 1.0f;

  g_return_val_if_fail (dev_props != NULL, FALSE);

  for (i = 0; i < G_N_ELEMENTS (unique_queue_families); i++)
    {
      VkDeviceQueueCreateInfo queueCreateInfo =
      {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = unique_queue_families[i],
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
      };
      queueCreateInfos[i] = queueCreateInfo;
    }

  VkPhysicalDeviceFeatures deviceFeatures = { .robustBufferAccess = VK_FALSE };

  VkDeviceCreateInfo createInfo =
  {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = G_N_ELEMENTS (unique_queue_families),
    .pQueueCreateInfos = queueCreateInfos,
    .pEnabledFeatures = &deviceFeatures,
    .enabledExtensionCount = G_N_ELEMENTS (device_extensions),
    .ppEnabledExtensionNames = device_extensions,
    .enabledLayerCount = 0,
  };

  if (!do_vk (vkCreateDevice (physical_device, &createInfo, NULL, &dev_props->device), error))
    return FALSE;

  vkGetDeviceQueue (dev_props->device, indices.graphicsFamily, 0, &dev_props->graphics_queue);
  vkGetDeviceQueue (dev_props->device, indices.presentFamily, 0, &dev_props->present_queue);

  return TRUE;
}

static VkSurfaceFormatKHR
choose_swap_surface_format (VkSurfaceFormatKHR *formats,
                            uint32_t format_count)
{
  uint32_t i;

  for (i = 0; i < format_count; i++)
    {
      if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM
          && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        return formats[i];
    }

  return formats[0];
}

static VkPresentModeKHR
choose_swap_present_mode (VkPresentModeKHR *present_modes,
                          uint32_t mode_count)
{
  uint32_t i;

  g_return_val_if_fail (present_modes != NULL, VK_PRESENT_MODE_MAX_ENUM_KHR);

  for (i = 0; i < mode_count; i++)
    {
      if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        return present_modes[i];
    }

  return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D
choose_swap_extent (const VkSurfaceCapabilitiesKHR capabilities)
{
  if (capabilities.currentExtent.width == UINT32_MAX)
    {
      VkExtent2D actualExtent = { WIDTH, HEIGHT };

      actualExtent.width = MAX (capabilities.minImageExtent.width,
                                MIN (capabilities.maxImageExtent.width, actualExtent.width));
      actualExtent.height = MAX (capabilities.minImageExtent.height,
                                 MIN (capabilities.maxImageExtent.height, actualExtent.height));

      return actualExtent;
    }

  return capabilities.currentExtent;
}

static SwapChainSupportDetails
query_swapchain_support (VkPhysicalDevice dev,
                         VkSurfaceKHR surface)
{
  SwapChainSupportDetails details = { .format_count = 0, };

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR (dev, surface, &details.capabilities);
  vkGetPhysicalDeviceSurfaceFormatsKHR (dev, surface, &details.format_count, NULL);

  if (details.format_count != 0)
    {
      details.formats = g_new0 (VkSurfaceFormatKHR, details.format_count);
      vkGetPhysicalDeviceSurfaceFormatsKHR (dev, surface, &details.format_count,
                                            details.formats);
    }

  vkGetPhysicalDeviceSurfacePresentModesKHR (dev, surface, &details.mode_count, NULL);

  if (details.mode_count != 0)
    {
      details.present_modes = g_new0 (VkPresentModeKHR, details.mode_count);
      vkGetPhysicalDeviceSurfacePresentModesKHR (dev, surface, &details.mode_count,
                                                 details.present_modes);
    }

  return details;
}

static gboolean
create_image_view (VkDevice device,
                   VkImage image,
                   VkFormat format,
                   VkImageView *image_view,
                   GError **error)
{
  VkImageViewCreateInfo create_info =
  {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    .components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.a = VK_COMPONENT_SWIZZLE_IDENTITY,
    .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .subresourceRange.baseMipLevel = 0,
    .subresourceRange.levelCount = 1,
    .subresourceRange.baseArrayLayer = 0,
    .subresourceRange.layerCount = 1,
  };

  if (!do_vk (vkCreateImageView (device, &create_info, NULL, image_view), error))
    return FALSE;

  return TRUE;
}

static gboolean
create_swapchain (VkPhysicalDevice physical_device,
                  VulkanPhysicalDeviceProperties *dev_props,
                  GError **error)
{
  uint32_t i;

  SwapChainSupportDetails swapchain_support = query_swapchain_support (physical_device,
                                                                       dev_props->surface);
  VkSurfaceFormatKHR surface_format = choose_swap_surface_format (swapchain_support.formats,
                                                                  swapchain_support.format_count);
  VkPresentModeKHR present_mode = choose_swap_present_mode (swapchain_support.present_modes,
                                                            swapchain_support.mode_count);
  VkExtent2D extent = choose_swap_extent (swapchain_support.capabilities);

  dev_props->framebuffer_size = swapchain_support.capabilities.minImageCount + 1;
  if (swapchain_support.capabilities.maxImageCount > 0
      && dev_props->framebuffer_size > swapchain_support.capabilities.maxImageCount)
    dev_props->framebuffer_size = swapchain_support.capabilities.maxImageCount;

  VkSwapchainCreateInfoKHR create_info =
  {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = dev_props->surface,
    .minImageCount = dev_props->framebuffer_size,
    .imageFormat = surface_format.format,
    .imageColorSpace = surface_format.colorSpace,
    .imageExtent = extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = swapchain_support.capabilities.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = present_mode,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE,
  };

  QueueFamilyIndices indices = find_queue_families (physical_device, dev_props->surface);
  uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};

  if (indices.graphicsFamily != indices.presentFamily)
    {
      create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      create_info.queueFamilyIndexCount = 2;
      create_info.pQueueFamilyIndices = queueFamilyIndices;
    }
  else
    {
      create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

  if (!do_vk (vkCreateSwapchainKHR (dev_props->device, &create_info, NULL,
                                    &dev_props->swapchain), error))
    return FALSE;

  vkGetSwapchainImagesKHR (dev_props->device, dev_props->swapchain,
                           &dev_props->framebuffer_size, NULL);
  dev_props->swapchain_images = g_new0 (VkImage, dev_props->framebuffer_size);
  vkGetSwapchainImagesKHR (dev_props->device, dev_props->swapchain,
                           &dev_props->framebuffer_size, dev_props->swapchain_images);

  dev_props->swapchain_image_format = surface_format.format;
  dev_props->swapchain_extent = extent;

  dev_props->swapchain_image_views = g_new0 (VkImageView, dev_props->framebuffer_size);
  for (i = 0; i < dev_props->framebuffer_size; i++)
    {
      if (!create_image_view (dev_props->device, dev_props->swapchain_images[i],
                              dev_props->swapchain_image_format,
                              &dev_props->swapchain_image_views[i],
                              error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_render_pass (VulkanPhysicalDeviceProperties *dev_props,
                    GError **error)
{
  VkAttachmentDescription color_attachment =
  {
    .format = dev_props->swapchain_image_format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkAttachmentReference color_attachment_ref =
  {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass =
  {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment_ref,
  };

  VkSubpassDependency dependency =
  {
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };

  VkRenderPassCreateInfo render_pass_info =
  {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &color_attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &dependency,
  };

  if (!do_vk (vkCreateRenderPass (dev_props->device, &render_pass_info, NULL,
                                  &dev_props->render_pass), error))
    return FALSE;

  return TRUE;
}

static gboolean
create_shader_module (VkDevice device,
                      const char *filename,
                      VkShaderModule *shader_module,
                      GError **error)
{
  g_autofree gchar *shader_data = NULL;
  gsize size;

  g_return_val_if_fail (filename != NULL, FALSE);
  g_return_val_if_fail (shader_module != NULL, FALSE);

  if (!g_file_get_contents (filename, &shader_data, &size, error))
    return FALSE;

  VkShaderModuleCreateInfo create_info =
  {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = size,
    .pCode = (uint32_t *) shader_data,
  };

  if (!do_vk (vkCreateShaderModule (device, &create_info, NULL, shader_module), error))
    return FALSE;

  return TRUE;
}

static gboolean
create_graphics_pipeline (VulkanPhysicalDeviceProperties *dev_props,
                          GError **error)
{
  g_autofree gchar *vert_path = NULL;
  g_autofree gchar *frag_path = NULL;
  g_autofree gchar *base_path = NULL;

  g_return_val_if_fail (dev_props != NULL, FALSE);

  base_path = g_strdup (getenv ("SRT_DATA_PATH"));
  if (base_path == NULL)
    base_path = g_path_get_dirname (argv0);

  vert_path = g_build_filename (base_path, "shaders", "/vert.spv", NULL);
  frag_path = g_build_filename (base_path, "shaders", "/frag.spv", NULL);

  VkShaderModule vert_shader_module;
  VkShaderModule frag_shader_module;
  if (!create_shader_module (dev_props->device, vert_path, &vert_shader_module, error))
    return FALSE;
  if (!create_shader_module (dev_props->device, frag_path, &frag_shader_module, error))
    return FALSE;

  VkPipelineShaderStageCreateInfo vert_shader_stage_info =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vert_shader_module,
    .pName = "main",
  };

  VkPipelineShaderStageCreateInfo frag_shader_stage_info =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = frag_shader_module,
    .pName = "main",
  };

  VkPipelineShaderStageCreateInfo shader_stages[] =
  {
    vert_shader_stage_info,
    frag_shader_stage_info,
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_info =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 0,
    .vertexAttributeDescriptionCount = 0,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewport =
  {
    .x = 0.0f,
    .y = 0.0f,
    .width = (float) dev_props->swapchain_extent.width,
    .height = (float) dev_props->swapchain_extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  VkRect2D scissor =
  {
    .offset = {0, 0},
    .extent = dev_props->swapchain_extent,
  };

  VkPipelineViewportStateCreateInfo viewport_state =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports = &viewport,
    .scissorCount = 1,
    .pScissors = &scissor,
  };

  VkPipelineRasterizationStateCreateInfo rasterizer =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .lineWidth = 1.0f,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
  };

  VkPipelineMultisampleStateCreateInfo multisampling =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .sampleShadingEnable = VK_FALSE,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineColorBlendAttachmentState color_blend_attachment =
  {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    .blendEnable = VK_FALSE,
  };

  VkPipelineColorBlendStateCreateInfo color_blending =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .logicOp = VK_LOGIC_OP_COPY,
    .attachmentCount = 1,
    .pAttachments = &color_blend_attachment,
    .blendConstants[0] = 0.0f,
    .blendConstants[1] = 0.0f,
    .blendConstants[2] = 0.0f,
    .blendConstants[3] = 0.0f,
  };

  VkPipelineLayoutCreateInfo pipeline_layout_info =
  {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 0,
    .pushConstantRangeCount = 0,
  };

  if (!do_vk (vkCreatePipelineLayout (dev_props->device, &pipeline_layout_info, NULL,
                                      &dev_props->pipeline_layout), error))
    return FALSE;

  VkGraphicsPipelineCreateInfo pipeline_info =
  {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = 2,
    .pStages = shader_stages,
    .pVertexInputState = &vertex_input_info,
    .pInputAssemblyState = &input_assembly,
    .pViewportState = &viewport_state,
    .pRasterizationState = &rasterizer,
    .pMultisampleState = &multisampling,
    .pColorBlendState = &color_blending,
    .layout = dev_props->pipeline_layout,
    .renderPass = dev_props->render_pass,
    .subpass = 0,
    .basePipelineHandle = VK_NULL_HANDLE,
  };

  if (!do_vk (vkCreateGraphicsPipelines (dev_props->device, VK_NULL_HANDLE, 1,
                                         &pipeline_info, NULL,
                                         &dev_props->graphics_pipeline), error))
    return FALSE;

  vkDestroyShaderModule (dev_props->device, frag_shader_module, NULL);
  vkDestroyShaderModule (dev_props->device, vert_shader_module, NULL);

  return TRUE;
}

static gboolean
create_framebuffers (VulkanPhysicalDeviceProperties *dev_props,
                     GError **error)
{
  uint32_t i = 0;
  dev_props->swapchain_framebuffers = g_new0 (VkFramebuffer, dev_props->framebuffer_size);

  g_return_val_if_fail (dev_props != NULL, FALSE);

  for (i = 0; i < dev_props->framebuffer_size; i++)
    {
      VkImageView attachments[] = {
        dev_props->swapchain_image_views[i]
      };

      VkFramebufferCreateInfo framebuffer_info =
      {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = dev_props->render_pass,
        .attachmentCount = 1,
        .pAttachments = attachments,
        .width = dev_props->swapchain_extent.width,
        .height = dev_props->swapchain_extent.height,
        .layers = 1,
      };

      if (!do_vk (vkCreateFramebuffer (dev_props->device, &framebuffer_info, NULL,
                                       &dev_props->swapchain_framebuffers[i]), error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_command_pool (VkPhysicalDevice physical_device,
                     VulkanPhysicalDeviceProperties *dev_props,
                     GError **error)
{
  QueueFamilyIndices queue_family_indices = find_queue_families (physical_device,
                                                                 dev_props->surface);

  VkCommandPoolCreateInfo pool_info =
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = queue_family_indices.graphicsFamily,
  };

  if (!do_vk (vkCreateCommandPool (dev_props->device, &pool_info, NULL,
                                   &dev_props->command_pool), error))
    return FALSE;

  return TRUE;
}

static gboolean
create_command_buffers (VulkanPhysicalDeviceProperties *dev_props,
                        GError **error)
{
  uint32_t i;

  g_return_val_if_fail (dev_props != NULL, FALSE);

  VkCommandBufferAllocateInfo alloc_info =
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = dev_props->command_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = dev_props->framebuffer_size,
  };

  dev_props->command_buffers = g_new0 (VkCommandBuffer, dev_props->framebuffer_size);

  if (!do_vk (vkAllocateCommandBuffers (dev_props->device, &alloc_info,
                                        dev_props->command_buffers), error))
    return FALSE;

  for (i = 0; i < dev_props->framebuffer_size; i++)
    {
      VkCommandBufferBeginInfo begin_info =
      {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };

      if (!do_vk (vkBeginCommandBuffer (dev_props->command_buffers[i], &begin_info), error))
        return FALSE;

      VkClearValue clear_color = { { {0.0f, 0.0f, 0.0f, 1.0f} } };
      VkRenderPassBeginInfo render_pass_info =
      {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = dev_props->render_pass,
        .framebuffer = dev_props->swapchain_framebuffers[i],
        .renderArea.offset = {0, 0},
        .renderArea.extent = dev_props->swapchain_extent,
        .clearValueCount = 1,
        .pClearValues = &clear_color,
      };

      vkCmdBeginRenderPass (dev_props->command_buffers[i], &render_pass_info,
                            VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline (dev_props->command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                         dev_props->graphics_pipeline);
      vkCmdDraw (dev_props->command_buffers[i], 3, 1, 0, 0);
      vkCmdEndRenderPass (dev_props->command_buffers[i]);
      if (!do_vk (vkEndCommandBuffer (dev_props->command_buffers[i]), error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_sync_objects (VulkanPhysicalDeviceProperties *dev_props,
                     GError **error)
{
  uint32_t i;

  g_return_val_if_fail (dev_props != NULL, FALSE);

  dev_props->image_available_semaphores = g_new0 (VkSemaphore, MAX_FRAMES_IN_FLIGHT);
  dev_props->render_finished_semaphores = g_new0 (VkSemaphore, MAX_FRAMES_IN_FLIGHT);
  dev_props->in_flight_fences = g_new0 (VkFence, MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphore_info =
  {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };

  VkFenceCreateInfo fence_info =
  {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };

  for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
      if (!do_vk (vkCreateSemaphore (dev_props->device, &semaphore_info, NULL,
                                     (&dev_props->image_available_semaphores[i])), error))
        return FALSE;
      if (!do_vk (vkCreateSemaphore (dev_props->device, &semaphore_info, NULL,
                                     (&dev_props->render_finished_semaphores[i])), error))
        return FALSE;
      if (!do_vk (vkCreateFence (dev_props->device, &fence_info, NULL,
                                 (&dev_props->in_flight_fences[i])), error))
        return FALSE;
    }
  return TRUE;
}

static gboolean
draw_frame (VulkanPhysicalDeviceProperties *dev_props,
            GError **error)
{
  g_return_val_if_fail (dev_props != NULL, FALSE);

  if (!do_vk (vkWaitForFences (dev_props->device, 1,
                               &dev_props->in_flight_fences[dev_props->current_frame],
                               VK_TRUE, UINT64_MAX), error))
    return FALSE;

  if (!do_vk (vkResetFences (dev_props->device, 1,
                             &dev_props->in_flight_fences[dev_props->current_frame]),
                             error))
    return FALSE;

  uint32_t image_index;
  if (!do_vk (vkAcquireNextImageKHR (dev_props->device, dev_props->swapchain, UINT64_MAX,
                                     dev_props->image_available_semaphores[dev_props->current_frame],
                                     VK_NULL_HANDLE, &image_index), error))
    return FALSE;

  VkSemaphore wait_semaphores[] =
  {
    dev_props->image_available_semaphores[dev_props->current_frame],
  };
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore signal_semaphores[] =
  {
    dev_props->render_finished_semaphores[dev_props->current_frame],
  };

  VkSubmitInfo submit_info =
  {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = wait_semaphores,
    .pWaitDstStageMask = wait_stages,
    .commandBufferCount = 1,
    .pCommandBuffers = &dev_props->command_buffers[image_index],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = signal_semaphores,
  };

  if (!do_vk (vkQueueSubmit (dev_props->graphics_queue, 1, &submit_info,
                             dev_props->in_flight_fences[dev_props->current_frame]), error))
    return FALSE;

  VkSwapchainKHR swapchains[] = {dev_props->swapchain};
  VkPresentInfoKHR present_info =
  {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = signal_semaphores,
    .swapchainCount = 1,
    .pSwapchains = swapchains,
    .pImageIndices = &image_index,
  };

  if (!do_vk (vkQueuePresentKHR (dev_props->present_queue, &present_info), error))
    return FALSE;

  dev_props->current_frame = (dev_props->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

  return TRUE;
}


QueueFamilyIndices
find_queue_families (VkPhysicalDevice dev,
                     VkSurfaceKHR surface)
{
  QueueFamilyIndices indices =
  {
    .hasGraphicsFamily = FALSE,
    .hasPresentFamily = FALSE,
  };

  uint32_t i;
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties (dev, &queue_family_count, NULL);

  VkQueueFamilyProperties *queue_families = g_new0 (VkQueueFamilyProperties,
                                                    queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties (dev, &queue_family_count, queue_families);

  for (i = 0; i < queue_family_count; i++)
    {
      VkQueueFamilyProperties queue_family = queue_families[i];
      if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
          indices.graphicsFamily = i;
          indices.hasGraphicsFamily = TRUE;
        }

      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR (dev, i, surface, &presentSupport);

      if (queue_family.queueCount > 0 && presentSupport)
        {
          indices.presentFamily = i;
          indices.hasPresentFamily = TRUE;
        }

      if (queue_family_indices_is_complete (indices))
        break;
    }

  return indices;
}

static gboolean
device_wait_idle (VulkanPhysicalDeviceProperties *dev_props,
                  GError **error)
{
  if (!do_vk (vkDeviceWaitIdle (dev_props->device), error))
    return FALSE;

  return TRUE;
}

static gboolean
draw_test_triangle (VkInstance vk_instance,
                    VkPhysicalDevice physical_device,
                    GError **error)
{
  gsize i;
  gboolean ret = FALSE;

  VulkanPhysicalDeviceProperties dev_props = { .framebuffer_size = 0, };

  if (!create_surface (vk_instance, physical_device, &dev_props, error))
    goto out;
  if (!create_logical_device (physical_device, &dev_props, error))
    goto out;
  if (!create_swapchain (physical_device, &dev_props, error))
    goto out;
  if (!create_render_pass (&dev_props, error))
    goto out;
  if (!create_graphics_pipeline (&dev_props, error))
    goto out;
  if (!create_framebuffers (&dev_props, error))
    goto out;
  if (!create_command_pool (physical_device, &dev_props, error))
    goto out;
  if (!create_command_buffers (&dev_props, error))
    goto out;
  if (!create_sync_objects (&dev_props, error))
    goto out;

  dev_props.current_frame = 0;

  for (i = 0; i < (opt_visible ? 10000 : 10); i++)
    {
      if (!draw_frame (&dev_props, error))
        goto out;
    }

  if (!device_wait_idle (&dev_props, error))
    goto out;

  ret = TRUE;

out:
  /* Cleanup */
  if (dev_props.device != VK_NULL_HANDLE)
    {
      for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
          if (dev_props.render_finished_semaphores != NULL
              && dev_props.render_finished_semaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore (dev_props.device, dev_props.render_finished_semaphores[i], NULL);
          if (dev_props.image_available_semaphores != NULL
              && dev_props.image_available_semaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore (dev_props.device, dev_props.image_available_semaphores[i], NULL);
          if (dev_props.in_flight_fences != NULL
              && dev_props.in_flight_fences[i] != VK_NULL_HANDLE)
            vkDestroyFence (dev_props.device, dev_props.in_flight_fences[i], NULL);
        }
      for (i = 0; i < dev_props.framebuffer_size; i++)
        {
          if (dev_props.swapchain_framebuffers != NULL
              && dev_props.swapchain_framebuffers[i] != VK_NULL_HANDLE)
            vkDestroyFramebuffer (dev_props.device, dev_props.swapchain_framebuffers[i], NULL);
          if (dev_props.swapchain_image_views != NULL
              && dev_props.swapchain_image_views[i] != VK_NULL_HANDLE)
          vkDestroyImageView (dev_props.device, dev_props.swapchain_image_views[i], NULL);
        }
      vkDestroyCommandPool (dev_props.device, dev_props.command_pool, NULL);
      if (dev_props.graphics_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline (dev_props.device, dev_props.graphics_pipeline, NULL);
      if (dev_props.pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout (dev_props.device, dev_props.pipeline_layout, NULL);
      if (dev_props.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass (dev_props.device, dev_props.render_pass, NULL);
      vkDestroySwapchainKHR (dev_props.device, dev_props.swapchain, NULL);
      vkDestroyDevice (dev_props.device, NULL);
    }

  if (vk_instance != VK_NULL_HANDLE)
    vkDestroySurfaceKHR (vk_instance, dev_props.surface, NULL);

  return ret;
}

static const GOptionEntry option_entries[] =
{
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { "visible", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_visible,
    "Make test window visible", NULL },
  { NULL }
};

int main (int argc,
          char** argv)
{
  g_autoptr(FILE) original_stdout = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GOptionContext) option_context = NULL;
  g_autofree VkPhysicalDevice *physical_devices = NULL;
  VkInstance vk_instance;
  uint32_t physical_device_count = 0;
  GError **error = &local_error;
  int ret = EXIT_FAILURE;

  argv0 = argv[0];

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, NULL))
    return 2;

  if (opt_print_version)
    {
      /* Output version number as YAML for machine-readability,
       * inspired by `ostree --version` and `docker version` */
      g_print ("%s:\n"
               " Package: steam-runtime-tools\n"
               " Version: %s\n",
               argv[0], VERSION);
      return EXIT_SUCCESS;
    }

  if (!create_instance (&vk_instance, error))
    goto out;

  if (!get_physical_devices (vk_instance, &physical_device_count, &physical_devices,
                             error))
    goto out;

  if (!draw_test_triangle (vk_instance, physical_devices[0], error))
    goto out;

  ret = EXIT_SUCCESS;

out:
  if (local_error != NULL)
    g_printerr ("%s", local_error->message);

  vkDestroyInstance (vk_instance, NULL);

  return ret;
}
