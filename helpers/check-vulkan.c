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
#include <json-glib/json-glib.h>

#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/json-glib-backports-internal.h>
#include <steam-runtime-tools/utils-internal.h>

static const int WIDTH = 200;
static const int HEIGHT = 200;

static gboolean opt_pretty_print = FALSE;
static gboolean opt_print_version = FALSE;
static gboolean opt_visible = FALSE;

const int MAX_FRAMES_IN_FLIGHT = 2;

static const char *argv0;

#define do_vk(expr, error) (_do_vk (#expr, expr, error))

static const char *
get_vk_error_string (VkResult error_code)
{
  switch (error_code)
    {
#define STR(r) case VK_##r: return #r
        STR(SUCCESS);
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_FRAGMENTED_POOL);
#if VK_HEADER_VERSION >= 131
        STR(ERROR_UNKNOWN);
#endif
        STR(ERROR_OUT_OF_POOL_MEMORY);
        STR(ERROR_INVALID_EXTERNAL_HANDLE);
        STR(ERROR_FRAGMENTATION_EXT);
#if VK_HEADER_VERSION >= 97
        STR(ERROR_INVALID_DEVICE_ADDRESS_EXT);
#endif
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
#if VK_HEADER_VERSION >= 135
#if VK_HEADER_VERSION < 162
        STR(ERROR_INCOMPATIBLE_VERSION_KHR);
#endif
        STR(THREAD_IDLE_KHR);
        STR(THREAD_DONE_KHR);
        STR(OPERATION_DEFERRED_KHR);
        STR(OPERATION_NOT_DEFERRED_KHR);
        STR(ERROR_PIPELINE_COMPILE_REQUIRED_EXT);
#endif
#if VK_HEADER_VERSION >= 89
        STR(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
#endif
        STR(ERROR_NOT_PERMITTED_EXT);
#if VK_HEADER_VERSION >= 105
        STR(ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
#endif
#if VK_HEADER_VERSION < 140
        STR(RESULT_RANGE_SIZE);
#endif
        STR(RESULT_MAX_ENUM);
#undef STR
      default:
        return "UNKNOWN_ERROR";
    }
}

static gboolean
_do_vk (const char *description,
        VkResult result,
        GError **error)
{
  if (result != VK_SUCCESS)
    return glnx_throw (error, "%s failed: %s (%d)", description,
                       get_vk_error_string (result), result);

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

typedef struct
{
  struct xcb_connection_t *xcb_connection;
  uint32_t xcb_window;
  VkInstance instance;
  VkPhysicalDevice physical_device;
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
} Renderer;

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
create_surface (Renderer *renderer,
                GError **error)
{
  xcb_screen_iterator_t iter;
  static const char title[] = "Vulkan Test";

  renderer->xcb_connection = xcb_connect (0, 0);
  if (xcb_connection_has_error (renderer->xcb_connection))
    return glnx_throw (error, "Unable to initialize xcb connection");

  renderer->xcb_window = xcb_generate_id (renderer->xcb_connection);

  uint32_t window_values[] = {
    XCB_EVENT_MASK_EXPOSURE |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY |
    XCB_EVENT_MASK_KEY_PRESS
  };

  iter = xcb_setup_roots_iterator (xcb_get_setup (renderer->xcb_connection));

  xcb_create_window (renderer->xcb_connection,
                     XCB_COPY_FROM_PARENT,
                     renderer->xcb_window,
                     iter.data->root,
                     0, 0,
                     WIDTH,
                     HEIGHT,
                     0,
                     XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     iter.data->root_visual,
                     XCB_CW_EVENT_MASK, window_values);

  xcb_atom_t atom_wm_protocols = get_atom (renderer->xcb_connection, "WM_PROTOCOLS");
  xcb_atom_t atom_wm_delete_window = get_atom (renderer->xcb_connection, "WM_DELETE_WINDOW");
  xcb_change_property (renderer->xcb_connection,
                       XCB_PROP_MODE_REPLACE,
                       renderer->xcb_window,
                       atom_wm_protocols,
                       XCB_ATOM_ATOM,
                       32,
                       1, &atom_wm_delete_window);

  xcb_change_property (renderer->xcb_connection,
                       XCB_PROP_MODE_REPLACE,
                       renderer->xcb_window,
                       get_atom (renderer->xcb_connection, "_NET_WM_NAME"),
                       get_atom (renderer->xcb_connection, "UTF8_STRING"),
                       8,
                       strlen (title), title);

  // we don't normally want this test to be visible to the user
  if (opt_visible)
    xcb_map_window (renderer->xcb_connection, renderer->xcb_window);

  xcb_flush (renderer->xcb_connection);

  PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR get_xcb_presentation_support =
    (PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)
    vkGetInstanceProcAddr (renderer->instance, "vkGetPhysicalDeviceXcbPresentationSupportKHR");
  PFN_vkCreateXcbSurfaceKHR create_xcb_surface =
    (PFN_vkCreateXcbSurfaceKHR)
    vkGetInstanceProcAddr (renderer->instance, "vkCreateXcbSurfaceKHR");

  if (!get_xcb_presentation_support (renderer->physical_device, 0,
                                     renderer->xcb_connection,
                                     iter.data->root_visual))
    return glnx_throw (error, "Vulkan not supported on given X window");

  VkXcbSurfaceCreateInfoKHR createSurfaceInfo =
  {
    .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .connection = renderer->xcb_connection,
    .window = renderer->xcb_window,
  };

  if (!do_vk (create_xcb_surface (renderer->instance, &createSurfaceInfo, NULL, &renderer->surface),
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
    {
      g_clear_pointer (physical_devices, g_free);
      return FALSE;
    }

  return TRUE;
}

QueueFamilyIndices find_queue_families (Renderer *renderer);

static gboolean
create_logical_device (Renderer *renderer,
                       GError **error)
{
  uint32_t i;
  QueueFamilyIndices indices = find_queue_families (renderer);
  uint32_t unique_queue_families[] =
  {
    indices.graphicsFamily,
    indices.presentFamily
  };
  VkDeviceQueueCreateInfo queueCreateInfos[G_N_ELEMENTS (unique_queue_families)];
  float queuePriority = 1.0f;

  g_return_val_if_fail (renderer != NULL, FALSE);

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

  if (!do_vk (vkCreateDevice (renderer->physical_device, &createInfo, NULL, &renderer->device), error))
    return FALSE;

  vkGetDeviceQueue (renderer->device, indices.graphicsFamily, 0, &renderer->graphics_queue);
  vkGetDeviceQueue (renderer->device, indices.presentFamily, 0, &renderer->present_queue);

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
create_swapchain (Renderer *renderer,
                  GError **error)
{
  uint32_t i;

  SwapChainSupportDetails swapchain_support = query_swapchain_support (renderer->physical_device,
                                                                       renderer->surface);
  VkSurfaceFormatKHR surface_format = choose_swap_surface_format (swapchain_support.formats,
                                                                  swapchain_support.format_count);
  VkPresentModeKHR present_mode = choose_swap_present_mode (swapchain_support.present_modes,
                                                            swapchain_support.mode_count);
  VkExtent2D extent = choose_swap_extent (swapchain_support.capabilities);

  renderer->framebuffer_size = swapchain_support.capabilities.minImageCount + 1;
  if (swapchain_support.capabilities.maxImageCount > 0
      && renderer->framebuffer_size > swapchain_support.capabilities.maxImageCount)
    renderer->framebuffer_size = swapchain_support.capabilities.maxImageCount;

  VkSwapchainCreateInfoKHR create_info =
  {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = renderer->surface,
    .minImageCount = renderer->framebuffer_size,
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

  g_clear_pointer (&swapchain_support.formats, g_free);
  g_clear_pointer (&swapchain_support.present_modes, g_free);

  QueueFamilyIndices indices = find_queue_families (renderer);
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

  if (!do_vk (vkCreateSwapchainKHR (renderer->device, &create_info, NULL,
                                    &renderer->swapchain), error))
    return FALSE;

  vkGetSwapchainImagesKHR (renderer->device, renderer->swapchain,
                           &renderer->framebuffer_size, NULL);
  renderer->swapchain_images = g_new0 (VkImage, renderer->framebuffer_size);
  vkGetSwapchainImagesKHR (renderer->device, renderer->swapchain,
                           &renderer->framebuffer_size, renderer->swapchain_images);

  renderer->swapchain_image_format = surface_format.format;
  renderer->swapchain_extent = extent;

  renderer->swapchain_image_views = g_new0 (VkImageView, renderer->framebuffer_size);
  for (i = 0; i < renderer->framebuffer_size; i++)
    {
      if (!create_image_view (renderer->device, renderer->swapchain_images[i],
                              renderer->swapchain_image_format,
                              &renderer->swapchain_image_views[i],
                              error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_render_pass (Renderer *renderer,
                    GError **error)
{
  VkAttachmentDescription color_attachment =
  {
    .format = renderer->swapchain_image_format,
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

  if (!do_vk (vkCreateRenderPass (renderer->device, &render_pass_info, NULL,
                                  &renderer->render_pass), error))
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
create_graphics_pipeline (Renderer *renderer,
                          GError **error)
{
  g_autofree gchar *vert_path = NULL;
  g_autofree gchar *frag_path = NULL;
  g_autofree gchar *base_path = NULL;

  g_return_val_if_fail (renderer != NULL, FALSE);

  base_path = g_strdup (getenv ("SRT_DATA_PATH"));
  if (base_path == NULL)
    base_path = g_path_get_dirname (argv0);

  vert_path = g_build_filename (base_path, "shaders", "/vert.spv", NULL);
  frag_path = g_build_filename (base_path, "shaders", "/frag.spv", NULL);

  VkShaderModule vert_shader_module;
  VkShaderModule frag_shader_module;
  if (!create_shader_module (renderer->device, vert_path, &vert_shader_module, error))
    return FALSE;
  if (!create_shader_module (renderer->device, frag_path, &frag_shader_module, error))
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
    .width = (float) renderer->swapchain_extent.width,
    .height = (float) renderer->swapchain_extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  VkRect2D scissor =
  {
    .offset = {0, 0},
    .extent = renderer->swapchain_extent,
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

  if (!do_vk (vkCreatePipelineLayout (renderer->device, &pipeline_layout_info, NULL,
                                      &renderer->pipeline_layout), error))
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
    .layout = renderer->pipeline_layout,
    .renderPass = renderer->render_pass,
    .subpass = 0,
    .basePipelineHandle = VK_NULL_HANDLE,
  };

  if (!do_vk (vkCreateGraphicsPipelines (renderer->device, VK_NULL_HANDLE, 1,
                                         &pipeline_info, NULL,
                                         &renderer->graphics_pipeline), error))
    return FALSE;

  vkDestroyShaderModule (renderer->device, frag_shader_module, NULL);
  vkDestroyShaderModule (renderer->device, vert_shader_module, NULL);

  return TRUE;
}

static gboolean
create_framebuffers (Renderer *renderer,
                     GError **error)
{
  uint32_t i = 0;
  renderer->swapchain_framebuffers = g_new0 (VkFramebuffer, renderer->framebuffer_size);

  g_return_val_if_fail (renderer != NULL, FALSE);

  for (i = 0; i < renderer->framebuffer_size; i++)
    {
      VkImageView attachments[] = {
        renderer->swapchain_image_views[i]
      };

      VkFramebufferCreateInfo framebuffer_info =
      {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = renderer->render_pass,
        .attachmentCount = 1,
        .pAttachments = attachments,
        .width = renderer->swapchain_extent.width,
        .height = renderer->swapchain_extent.height,
        .layers = 1,
      };

      if (!do_vk (vkCreateFramebuffer (renderer->device, &framebuffer_info, NULL,
                                       &renderer->swapchain_framebuffers[i]), error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_command_pool (Renderer *renderer,
                     GError **error)
{
  QueueFamilyIndices queue_family_indices = find_queue_families (renderer);

  VkCommandPoolCreateInfo pool_info =
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = queue_family_indices.graphicsFamily,
  };

  if (!do_vk (vkCreateCommandPool (renderer->device, &pool_info, NULL,
                                   &renderer->command_pool), error))
    return FALSE;

  return TRUE;
}

static gboolean
create_command_buffers (Renderer *renderer,
                        GError **error)
{
  uint32_t i;

  g_return_val_if_fail (renderer != NULL, FALSE);

  VkCommandBufferAllocateInfo alloc_info =
  {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = renderer->command_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = renderer->framebuffer_size,
  };

  renderer->command_buffers = g_new0 (VkCommandBuffer, renderer->framebuffer_size);

  if (!do_vk (vkAllocateCommandBuffers (renderer->device, &alloc_info,
                                        renderer->command_buffers), error))
    return FALSE;

  for (i = 0; i < renderer->framebuffer_size; i++)
    {
      VkCommandBufferBeginInfo begin_info =
      {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };

      if (!do_vk (vkBeginCommandBuffer (renderer->command_buffers[i], &begin_info), error))
        return FALSE;

      VkClearValue clear_color = { { {0.0f, 0.0f, 0.0f, 1.0f} } };
      VkRenderPassBeginInfo render_pass_info =
      {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderer->render_pass,
        .framebuffer = renderer->swapchain_framebuffers[i],
        .renderArea.offset = {0, 0},
        .renderArea.extent = renderer->swapchain_extent,
        .clearValueCount = 1,
        .pClearValues = &clear_color,
      };

      vkCmdBeginRenderPass (renderer->command_buffers[i], &render_pass_info,
                            VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline (renderer->command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                         renderer->graphics_pipeline);
      vkCmdDraw (renderer->command_buffers[i], 3, 1, 0, 0);
      vkCmdEndRenderPass (renderer->command_buffers[i]);
      if (!do_vk (vkEndCommandBuffer (renderer->command_buffers[i]), error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_sync_objects (Renderer *renderer,
                     GError **error)
{
  uint32_t i;

  g_return_val_if_fail (renderer != NULL, FALSE);

  renderer->image_available_semaphores = g_new0 (VkSemaphore, MAX_FRAMES_IN_FLIGHT);
  renderer->render_finished_semaphores = g_new0 (VkSemaphore, MAX_FRAMES_IN_FLIGHT);
  renderer->in_flight_fences = g_new0 (VkFence, MAX_FRAMES_IN_FLIGHT);

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
      if (!do_vk (vkCreateSemaphore (renderer->device, &semaphore_info, NULL,
                                     (&renderer->image_available_semaphores[i])), error))
        return FALSE;
      if (!do_vk (vkCreateSemaphore (renderer->device, &semaphore_info, NULL,
                                     (&renderer->render_finished_semaphores[i])), error))
        return FALSE;
      if (!do_vk (vkCreateFence (renderer->device, &fence_info, NULL,
                                 (&renderer->in_flight_fences[i])), error))
        return FALSE;
    }
  return TRUE;
}

static gboolean
draw_frame (Renderer *renderer,
            GError **error)
{
  g_return_val_if_fail (renderer != NULL, FALSE);

  if (!do_vk (vkWaitForFences (renderer->device, 1,
                               &renderer->in_flight_fences[renderer->current_frame],
                               VK_TRUE, UINT64_MAX), error))
    return FALSE;

  if (!do_vk (vkResetFences (renderer->device, 1,
                             &renderer->in_flight_fences[renderer->current_frame]),
                             error))
    return FALSE;

  uint32_t image_index;
  if (!do_vk (vkAcquireNextImageKHR (renderer->device, renderer->swapchain, UINT64_MAX,
                                     renderer->image_available_semaphores[renderer->current_frame],
                                     VK_NULL_HANDLE, &image_index), error))
    return FALSE;

  VkSemaphore wait_semaphores[] =
  {
    renderer->image_available_semaphores[renderer->current_frame],
  };
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore signal_semaphores[] =
  {
    renderer->render_finished_semaphores[renderer->current_frame],
  };

  VkSubmitInfo submit_info =
  {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = wait_semaphores,
    .pWaitDstStageMask = wait_stages,
    .commandBufferCount = 1,
    .pCommandBuffers = &renderer->command_buffers[image_index],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = signal_semaphores,
  };

  if (!do_vk (vkQueueSubmit (renderer->graphics_queue, 1, &submit_info,
                             renderer->in_flight_fences[renderer->current_frame]), error))
    return FALSE;

  VkSwapchainKHR swapchains[] = {renderer->swapchain};
  VkPresentInfoKHR present_info =
  {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = signal_semaphores,
    .swapchainCount = 1,
    .pSwapchains = swapchains,
    .pImageIndices = &image_index,
  };

  if (!do_vk (vkQueuePresentKHR (renderer->present_queue, &present_info), error))
    return FALSE;

  renderer->current_frame = (renderer->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

  return TRUE;
}


QueueFamilyIndices
find_queue_families (Renderer *renderer)
{
  QueueFamilyIndices indices =
  {
    .hasGraphicsFamily = FALSE,
    .hasPresentFamily = FALSE,
  };

  uint32_t i;
  uint32_t queue_family_count = 0;
  g_autofree VkQueueFamilyProperties *queue_families = NULL;

  vkGetPhysicalDeviceQueueFamilyProperties (renderer->physical_device, &queue_family_count, NULL);
  queue_families = g_new0 (VkQueueFamilyProperties, queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties (renderer->physical_device, &queue_family_count, queue_families);

  for (i = 0; i < queue_family_count; i++)
    {
      VkQueueFamilyProperties queue_family = queue_families[i];
      if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
          indices.graphicsFamily = i;
          indices.hasGraphicsFamily = TRUE;
        }

      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR (renderer->physical_device, i, renderer->surface, &presentSupport);

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
device_wait_idle (Renderer *renderer,
                  GError **error)
{
  if (!do_vk (vkDeviceWaitIdle (renderer->device), error))
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
  Renderer renderer =
  {
    .instance = vk_instance,
    .physical_device = physical_device,
    .framebuffer_size = 0,
  };

  if (!create_surface (&renderer, error))
    goto out;
  if (!create_logical_device (&renderer, error))
    goto out;
  if (!create_swapchain (&renderer, error))
    goto out;
  if (!create_render_pass (&renderer, error))
    goto out;
  if (!create_graphics_pipeline (&renderer, error))
    goto out;
  if (!create_framebuffers (&renderer, error))
    goto out;
  if (!create_command_pool (&renderer, error))
    goto out;
  if (!create_command_buffers (&renderer, error))
    goto out;
  if (!create_sync_objects (&renderer, error))
    goto out;

  renderer.current_frame = 0;

  for (i = 0; i < (opt_visible ? 10000 : 10); i++)
    {
      if (!draw_frame (&renderer, error))
        goto out;
    }

  if (!device_wait_idle (&renderer, error))
    goto out;

  ret = TRUE;

out:
  /* Cleanup */
  if (renderer.device != VK_NULL_HANDLE)
    {
      for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
          if (renderer.render_finished_semaphores != NULL
              && renderer.render_finished_semaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore (renderer.device, renderer.render_finished_semaphores[i], NULL);
          if (renderer.image_available_semaphores != NULL
              && renderer.image_available_semaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore (renderer.device, renderer.image_available_semaphores[i], NULL);
          if (renderer.in_flight_fences != NULL
              && renderer.in_flight_fences[i] != VK_NULL_HANDLE)
            vkDestroyFence (renderer.device, renderer.in_flight_fences[i], NULL);
        }

      g_free (renderer.render_finished_semaphores);
      g_free (renderer.image_available_semaphores);
      g_free (renderer.in_flight_fences);

      for (i = 0; i < renderer.framebuffer_size; i++)
        {
          if (renderer.swapchain_framebuffers != NULL
              && renderer.swapchain_framebuffers[i] != VK_NULL_HANDLE)
            vkDestroyFramebuffer (renderer.device, renderer.swapchain_framebuffers[i], NULL);
          if (renderer.swapchain_image_views != NULL
              && renderer.swapchain_image_views[i] != VK_NULL_HANDLE)
          vkDestroyImageView (renderer.device, renderer.swapchain_image_views[i], NULL);
        }

      g_free (renderer.swapchain_framebuffers);
      g_free (renderer.swapchain_image_views);
      g_free (renderer.swapchain_images);

      if (renderer.command_buffers != NULL)
        {
          vkFreeCommandBuffers (renderer.device, renderer.command_pool,
                                renderer.framebuffer_size,
                                renderer.command_buffers);
          g_free (renderer.command_buffers);
        }

      vkDestroyCommandPool (renderer.device, renderer.command_pool, NULL);
      if (renderer.graphics_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline (renderer.device, renderer.graphics_pipeline, NULL);
      if (renderer.pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout (renderer.device, renderer.pipeline_layout, NULL);
      if (renderer.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass (renderer.device, renderer.render_pass, NULL);
      vkDestroySwapchainKHR (renderer.device, renderer.swapchain, NULL);
      vkDestroyDevice (renderer.device, NULL);
    }

  if (renderer.instance != VK_NULL_HANDLE)
    vkDestroySurfaceKHR (renderer.instance, renderer.surface, NULL);

  if (renderer.xcb_window && renderer.xcb_connection != NULL)
    xcb_destroy_window (renderer.xcb_connection, renderer.xcb_window);

  if (renderer.xcb_connection != NULL)
    xcb_flush (renderer.xcb_connection);

  g_clear_pointer (&renderer.xcb_connection, xcb_disconnect);

  return ret;
}

static void
print_json_builder (JsonBuilder *builder,
                    FILE *original_stdout)
{
  g_autoptr(JsonNode) root = NULL;
  g_autoptr(JsonGenerator) generator = NULL;
  g_autofree gchar *json = NULL;

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_pretty (generator, opt_pretty_print);
  json_generator_set_root (generator, root);
  json = json_generator_to_data (generator, NULL);
  if (fputs (json, original_stdout) < 0)
    g_warning ("Unable to write output: %s", g_strerror (errno));

  if (fputs ("\n", original_stdout) < 0)
    g_warning ("Unable to write final newline: %s", g_strerror (errno));
}

static void
print_physical_device_info (VkPhysicalDevice physical_device,
                            FILE *original_stdout)
{
  g_autoptr(JsonBuilder) builder = NULL;
  g_autofree gchar *api_version = NULL;
  g_autofree gchar *driver_version = NULL;
  g_autofree gchar *vendor_id = NULL;
  g_autofree gchar *device_id = NULL;
  VkPhysicalDeviceProperties device_properties;

  builder = json_builder_new ();
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "device-info");
  json_builder_begin_object (builder);

  vkGetPhysicalDeviceProperties (physical_device, &device_properties);

  json_builder_set_member_name (builder, "device-name");
  json_builder_add_string_value (builder, device_properties.deviceName);

  json_builder_set_member_name (builder, "device-type");
  json_builder_add_int_value (builder, device_properties.deviceType);

  json_builder_set_member_name (builder, "api-version");
  api_version = g_strdup_printf ("%u.%u.%u",
                                 VK_VERSION_MAJOR (device_properties.apiVersion),
                                 VK_VERSION_MINOR (device_properties.apiVersion),
                                 VK_VERSION_PATCH (device_properties.apiVersion));
  json_builder_add_string_value (builder, api_version);

  json_builder_set_member_name (builder, "driver-version");
  driver_version = g_strdup_printf ("%u.%u.%u",
                                    VK_VERSION_MAJOR (device_properties.driverVersion),
                                    VK_VERSION_MINOR (device_properties.driverVersion),
                                    VK_VERSION_PATCH (device_properties.driverVersion));
  json_builder_add_string_value (builder, driver_version);

  json_builder_set_member_name (builder, "vendor-id");
  vendor_id = g_strdup_printf ("%#x", device_properties.vendorID);
  json_builder_add_string_value (builder, vendor_id);

  json_builder_set_member_name (builder, "device-id");
  device_id = g_strdup_printf ("%#x", device_properties.deviceID);
  json_builder_add_string_value (builder, device_id);

  json_builder_end_object (builder);
  json_builder_end_object (builder);

  print_json_builder (builder, original_stdout);
}

static void
print_draw_test_result (gsize index,
                        gboolean result,
                        const GError *error,
                        FILE *original_stdout)
{
  g_autoptr(JsonBuilder) builder = NULL;

  builder = json_builder_new ();
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "test");
  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "index");
  json_builder_add_int_value (builder, index);

  json_builder_set_member_name (builder, "can-draw");
  json_builder_add_boolean_value (builder, result);

  if (error != NULL)
    {
      json_builder_set_member_name (builder, "error-message");
      json_builder_add_string_value (builder, error->message);
    }

  json_builder_end_object (builder);
  json_builder_end_object (builder);

  print_json_builder (builder, original_stdout);
}

static const GOptionEntry option_entries[] =
{
  { "pretty-print", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_pretty_print, "The generated JSON will be pretty printed instead "
    "of being one object per line", NULL },
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
  gboolean result;
  gsize i;

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

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      g_printerr ("Unable to divert stdout to stderr: %s", local_error->message);
      return EXIT_FAILURE;
    }

  if (!create_instance (&vk_instance, error))
    goto out;

  if (!get_physical_devices (vk_instance, &physical_device_count, &physical_devices,
                             error))
    goto out;

  for (i = 0; i < physical_device_count; i++)
    print_physical_device_info (physical_devices[i], original_stdout);

  for (i = 0; i < physical_device_count; i++)
    {
      result = draw_test_triangle (vk_instance, physical_devices[i], error);
      print_draw_test_result (i, result, local_error, original_stdout);

      /* The eventual error has already been included in the drawing test JSON */
      g_clear_error (error);

      /* Return exit success if we are able to draw with at least one device */
      if (result)
        ret = EXIT_SUCCESS;
    }

out:
  if (local_error != NULL)
    g_printerr ("%s", local_error->message);

  vkDestroyInstance (vk_instance, NULL);

  return ret;
}
