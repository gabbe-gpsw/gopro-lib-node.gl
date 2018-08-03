/*
 * Copyright 2018 GoPro Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#if defined(TARGET_LINUX)
# define VK_USE_PLATFORM_XLIB_KHR
#elif defined(TARGET_ANDROID)
# define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(TARGET_DARWIN)
# define VK_USE_PLATFORM_MACOS_MVK
#elif defined(TARGET_IPHONE)
# define VK_USE_PLATFORM_IOS_MVK
#elif defined(TARGET_MINGW_W64)
// TODO
# define VK_USE_PLATFORM_WIN32_KHR
#endif

#if defined(TARGET_DARWIN) || defined(TARGET_IPHONE)
#define USE_MOLTENVK 1
#else
#define USE_MOLTENVK 0
#endif

#include <vulkan/vulkan.h>

#include "nodes.h"
#include "backend.h"
#include "vkcontext.h"
#include "gctx.h"
#include "gctx_vk.h"
#include "log.h"
#include "memory.h"
#include "pgcache.h"

#include "texture.h"
#include "texture_vk.h"
#include "rendertarget.h"

#if USE_MOLTENVK || defined(TARGET_ANDROID)
#define ENABLE_DEBUG 0
#else
#define ENABLE_DEBUG 1
#endif

static const VkApplicationInfo app_info = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pEngineName = "node.gl",
    .engineVersion = NODEGL_VERSION_INT,
#if !defined(VK_API_VERSION_1_1) || USE_MOLTENVK
    .apiVersion = VK_API_VERSION_1_0,
#else
    .apiVersion = VK_API_VERSION_1_1,
#endif
};

static const char *my_device_extension_names[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static const char *vk_res2str(VkResult res)
{
    switch (res) {
        case VK_SUCCESS:                        return "sucess";
        case VK_NOT_READY:                      return "not ready";
        case VK_TIMEOUT:                        return "timeout";
        case VK_EVENT_SET:                      return "event set";
        case VK_EVENT_RESET:                    return "event reset";
        case VK_INCOMPLETE:                     return "incomplete";
        case VK_ERROR_OUT_OF_HOST_MEMORY:       return "out of host memory";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "out of device memory";
        case VK_ERROR_INITIALIZATION_FAILED:    return "initialization failed";
        case VK_ERROR_DEVICE_LOST:              return "device lost";
        case VK_ERROR_MEMORY_MAP_FAILED:        return "memory map failed";
        case VK_ERROR_LAYER_NOT_PRESENT:        return "layer not present";
        case VK_ERROR_EXTENSION_NOT_PRESENT:    return "extension not present";
        case VK_ERROR_FEATURE_NOT_PRESENT:      return "feature not present";
        case VK_ERROR_INCOMPATIBLE_DRIVER:      return "incompatible driver";
        case VK_ERROR_TOO_MANY_OBJECTS:         return "too many objects";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "format not supported";
        case VK_ERROR_FRAGMENTED_POOL:          return "fragmented pool";
#ifdef VK_ERROR_OUT_OF_POOL_MEMORY
        case VK_ERROR_OUT_OF_POOL_MEMORY:       return "out of pool memory";
#endif
#ifdef VK_ERROR_INVALID_EXTERNAL_HANDLE
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:  return "invalid external handle";
#endif
        case VK_ERROR_SURFACE_LOST_KHR:         return "surface lost (KHR)";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "native window in use (KHR)";
        case VK_SUBOPTIMAL_KHR:                 return "suboptimal (KHR)";
        case VK_ERROR_OUT_OF_DATE_KHR:          return "out of date (KHR)";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "incompatible display (KHR)";
        case VK_ERROR_VALIDATION_FAILED_EXT:    return "validation failed ext";
        case VK_ERROR_INVALID_SHADER_NV:        return "invalid shader nv";
#ifdef VK_ERROR_FRAGMENTATION_EXT
        case VK_ERROR_FRAGMENTATION_EXT:        return "fragmentation ext";
#endif
#ifdef VK_ERROR_NOT_PERMITTED_EXT
        case VK_ERROR_NOT_PERMITTED_EXT:        return "not permitted ext";
#endif
        default:                                return "unknown";
    }
}

#if ENABLE_DEBUG
static const char *my_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT type,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                                                     void *user_data)
{
    int log_level = NGL_LOG_INFO;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        log_level = NGL_LOG_WARNING;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        log_level = NGL_LOG_ERROR;

    const char *msg_type = "GENERAL";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        msg_type = "VALIDATION";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        msg_type = "PERFORMANCE";

    ngli_log_print(log_level, __FILE__, __LINE__, "debug_callback", "%s: %s", msg_type, callback_data->pMessage);
    return VK_FALSE;
}
#endif

static VkSurfaceFormatKHR select_swapchain_surface_format(const VkSurfaceFormatKHR *formats,
                                                          uint32_t nb_formats)
{
    VkSurfaceFormatKHR target_fmt = {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };
    if (nb_formats == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
        return target_fmt;
    for (uint32_t i = 0; i < nb_formats; i++)
        if (formats[i].format == target_fmt.format &&
            formats[i].colorSpace == target_fmt.colorSpace)
            return formats[i];
    return formats[0];
}

static VkPresentModeKHR select_swapchain_present_mode(const VkPresentModeKHR *present_modes,
                                                      uint32_t nb_present_modes)
{
    VkPresentModeKHR target_mode = VK_PRESENT_MODE_FIFO_KHR;

    for (uint32_t i = 0; i < nb_present_modes; i++) {

        /* triple buffering, best mode possible */
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            return present_modes[i];

        /* some drivers may not actually have VK_PRESENT_MODE_FIFO_KHR */
        if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
            target_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return target_mode;
}

static uint32_t clip_u32(uint32_t x, uint32_t min, uint32_t max)
{
    if (x < min)
        return min;
    if (x > max)
        return max;
    return x;
}

static VkExtent2D select_swapchain_current_extent(const struct vkcontext *vk,
                                                  VkSurfaceCapabilitiesKHR caps)
{
    if (caps.currentExtent.width != UINT32_MAX) {
        LOG(DEBUG, "current extent: %dx%d", caps.currentExtent.width, caps.currentExtent.height);
        return caps.currentExtent;
    }

    VkExtent2D ext = {
        .width  = clip_u32(vk->config.width,  caps.minImageExtent.width,  caps.maxImageExtent.width),
        .height = clip_u32(vk->config.height, caps.minImageExtent.height, caps.maxImageExtent.height),
    };
    LOG(DEBUG, "swapchain extent %dx%d", ext.width, ext.height);
    return ext;
}

static VkResult query_swapchain_support(struct vk_swapchain_support *swap,
                                        VkSurfaceKHR surface,
                                        VkPhysicalDevice phy_device)
{
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phy_device, surface, &swap->caps);

    ngli_free(swap->formats);
    swap->formats = NULL;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy_device, surface, &swap->nb_formats, NULL);
    if (swap->nb_formats) {
        swap->formats = ngli_calloc(swap->nb_formats, sizeof(*swap->formats));
        if (!swap->formats)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        vkGetPhysicalDeviceSurfaceFormatsKHR(phy_device, surface, &swap->nb_formats, swap->formats);
    }

    ngli_free(swap->present_modes);
    swap->present_modes = NULL;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phy_device, surface, &swap->nb_present_modes, NULL);
    if (swap->nb_present_modes) {
        swap->present_modes = ngli_calloc(swap->nb_present_modes, sizeof(*swap->present_modes));
        if (!swap->present_modes)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        vkGetPhysicalDeviceSurfacePresentModesKHR(phy_device, surface, &swap->nb_present_modes, swap->present_modes);
    }

    return VK_SUCCESS;
}

static VkResult probe_vulkan_extensions(struct vkcontext *vk)
{
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    VkExtensionProperties *ext_props = ngli_calloc(ext_count, sizeof(*ext_props));
    if (!ext_props)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, ext_props);
    LOG(DEBUG, "Vulkan extensions available:");
    for (uint32_t i = 0; i < ext_count; i++) {
        LOG(DEBUG, "  %d/%d: %s v%d", i+1, ext_count,
               ext_props[i].extensionName, ext_props[i].specVersion);
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        if (!strcmp(ext_props[i].extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
            vk->surface_create_type = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        if (!strcmp(ext_props[i].extensionName, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME))
            vk->surface_create_type = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
        if (!strcmp(ext_props[i].extensionName, VK_MVK_MACOS_SURFACE_EXTENSION_NAME))
            vk->surface_create_type = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
#elif defined(VK_USE_PLATFORM_IOS_MVK)
        if (!strcmp(ext_props[i].extensionName, VK_MVK_IOS_SURFACE_EXTENSION_NAME))
            vk->surface_create_type = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
#endif
    }
    ngli_free(ext_props);
    return VK_SUCCESS;
}

static VkFormat find_supported_format(struct vkcontext *vk,
                                      const VkFormat *formats,
                                      VkImageTiling tiling,
                                      VkFormatFeatureFlags features)
{
    int i = 0;
    while (formats[i]) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(vk->physical_device, formats[i], &properties);
        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
            return formats[i];
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
            return formats[i];
        i++;
    }
    return VK_FORMAT_UNDEFINED;
}

static VkResult probe_prefered_depth_stencil_format(struct vkcontext *vk)
{
    const VkFormat formats[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        0
    };
    const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    const VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    vk->prefered_vk_depth_stencil_format = find_supported_format(vk, formats, tiling, features);
    if (!vk->prefered_vk_depth_stencil_format)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    switch (vk->prefered_vk_depth_stencil_format) {
    case VK_FORMAT_D24_UNORM_S8_UINT:
        vk->prefered_depth_stencil_format = NGLI_FORMAT_D24_UNORM_S8_UINT;
        break;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        vk->prefered_depth_stencil_format = NGLI_FORMAT_D32_SFLOAT_S8_UINT;
        break;
    case VK_FORMAT_D32_SFLOAT:
        vk->prefered_depth_stencil_format = NGLI_FORMAT_D32_SFLOAT;
        break;
    default:
        ngli_assert(0);
    };

    return VK_SUCCESS;
}

static VkResult list_vulkan_layers(void)
{
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    VkLayerProperties *layer_props = ngli_calloc(layer_count, sizeof(*layer_props));
    if (!layer_props)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkEnumerateInstanceLayerProperties(&layer_count, layer_props);
    LOG(DEBUG, "Vulkan layers available:");
    for (uint32_t i = 0; i < layer_count; i++)
        LOG(DEBUG, "  %d/%d: %s", i+1, layer_count, layer_props[i].layerName);
    ngli_free(layer_props);
    return VK_SUCCESS;
}

static const char *platform_ext_names[] = {
    [NGL_PLATFORM_XLIB]    = "VK_KHR_xlib_surface",
    [NGL_PLATFORM_ANDROID] = "VK_KHR_android_surface",
    [NGL_PLATFORM_MACOS]   = "VK_MVK_macos_surface",
    [NGL_PLATFORM_IOS]     = "VK_MVK_ios_surface",
    [NGL_PLATFORM_WINDOWS] = "VK_KHR_win32_surface",
};

static VkResult create_vulkan_instance(struct vkcontext *vk)
{
    const char *surface_ext_name = platform_ext_names[vk->config.platform];
    const char *my_extension_names[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        surface_ext_name,
#if ENABLE_DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

    LOG(DEBUG, "surface extension name: %s", surface_ext_name);

    VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = NGLI_ARRAY_NB(my_extension_names),
        .ppEnabledExtensionNames = my_extension_names,
#if ENABLE_DEBUG
        .enabledLayerCount = NGLI_ARRAY_NB(my_layers),
        .ppEnabledLayerNames = my_layers,
#endif
    };
    VkResult ret = vkCreateInstance(&instance_create_info, NULL, &vk->instance);
    return ret;
}

static void *vulkan_get_proc_addr(struct vkcontext *vk, const char *name)
{
    void *proc_addr = vkGetInstanceProcAddr(vk->instance, name);
    if (!proc_addr) {
        LOG(ERROR, "can not find %s extension", name);
        return NULL;
    }
    return proc_addr;
}

#if ENABLE_DEBUG
static VkResult setup_vulkan_debug_callback(struct vkcontext *vk)
{
    void *proc_addr = vulkan_get_proc_addr(vk, "vkCreateDebugUtilsMessengerEXT");
    if (!proc_addr)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    PFN_vkCreateDebugUtilsMessengerEXT func = proc_addr;
    VkDebugUtilsMessengerCreateInfoEXT callback_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = NULL,
    };
    return func(vk->instance, &callback_create_info, NULL, &vk->report_callback);
}
#endif

static int string_in(const char *target, const char * const *str_list, int nb_str)
{
    for (int i = 0; i < nb_str; i++)
        if (!strcmp(target, str_list[i]))
            return 1;
    return 0;
}

static VkResult get_filtered_ext_props(const VkExtensionProperties *src_props, uint32_t nb_src_props,
                                       VkExtensionProperties **dst_props_p, uint32_t *nb_dst_props_p,
                                       const char * const *filtered_names, int nb_filtered_names)
{
    *dst_props_p = NULL;
    *nb_dst_props_p = 0;

    VkExtensionProperties *dst_props = ngli_calloc(nb_src_props, sizeof(*dst_props));
    if (!dst_props_p)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    int nb_dst_props = 0;
    for (int j = 0; j < nb_src_props; j++) {
        if (!string_in(src_props[j].extensionName, filtered_names, nb_filtered_names))
            continue;
        dst_props[nb_dst_props++] = src_props[j];
        if (nb_dst_props == nb_filtered_names)
            break;
    }

    *dst_props_p = dst_props;
    *nb_dst_props_p = nb_dst_props;
    return VK_SUCCESS;
}

static VkResult select_vulkan_physical_device(struct ngl_ctx *s, struct vkcontext *vk)
{
    uint32_t phydevice_count = 0;
    vkEnumeratePhysicalDevices(vk->instance, &phydevice_count, NULL);
    if (!phydevice_count) {
        LOG(ERROR, "no physical device available");
        return VK_ERROR_DEVICE_LOST;
    }
    VkPhysicalDevice *phy_devices = ngli_calloc(phydevice_count, sizeof(*phy_devices));
    if (!phy_devices)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkEnumeratePhysicalDevices(vk->instance, &phydevice_count, phy_devices);
    LOG(INFO, "Vulkan physical devices available:");
    for (uint32_t i = 0; i < phydevice_count; i++) {
        VkPhysicalDevice phy_device = phy_devices[i];

        /* Device properties */
        static const char *physical_device_types[] = {
            [VK_PHYSICAL_DEVICE_TYPE_OTHER] = "other",
            [VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU] = "integrated",
            [VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU] = "discrete",
            [VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU] = "virtual",
            [VK_PHYSICAL_DEVICE_TYPE_CPU] = "cpu",
        };
        VkPhysicalDeviceProperties dev_props;
        vkGetPhysicalDeviceProperties(phy_device, &dev_props);
        LOG(INFO, "  %d/%d: %s (%s)", i+1, phydevice_count,
               dev_props.deviceName,
               physical_device_types[dev_props.deviceType]);

        /* Device features */
        VkPhysicalDeviceFeatures dev_features;
        vkGetPhysicalDeviceFeatures(phy_device, &dev_features);

        /* Device queue families */
        int queue_family_graphics_id = -1;
        int queue_family_present_id = -1;
        uint32_t qfamily_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phy_device, &qfamily_count, NULL);
        VkQueueFamilyProperties *qfamily_props = ngli_calloc(qfamily_count, sizeof(*qfamily_props));
        if (!qfamily_props)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        vkGetPhysicalDeviceQueueFamilyProperties(phy_device, &qfamily_count, qfamily_props);
        LOG(DEBUG, "  queue props:");
        for (uint32_t j = 0; j < qfamily_count; j++) {
            VkQueueFamilyProperties props = qfamily_props[j];
            LOG(DEBUG, "    family %d/%d:%s%s%s%s%s (count: %d)", j+1, qfamily_count,
                   props.queueFlags & VK_QUEUE_GRAPHICS_BIT       ? " Graphics"      : "",
                   props.queueFlags & VK_QUEUE_COMPUTE_BIT        ? " Compute"       : "",
                   props.queueFlags & VK_QUEUE_TRANSFER_BIT       ? " Transfer"      : "",
                   props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT ? " SparseBinding" : "",
#ifdef VK_QUEUE_PROTECTED_BIT
                   props.queueFlags & VK_QUEUE_PROTECTED_BIT      ? " Protected"     : "",
#else
                   "",
#endif
                   props.queueCount);
            if ((props.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (props.queueFlags & VK_QUEUE_COMPUTE_BIT))
                queue_family_graphics_id = j;

            if (!s->config.offscreen) {
            VkBool32 surface_support;
            vkGetPhysicalDeviceSurfaceSupportKHR(phy_device, j, vk->surface, &surface_support);
            if (surface_support)
                queue_family_present_id = j;
            }
        }
        ngli_free(qfamily_props);

        /* Device extensions */
        uint32_t extprops_count;
        vkEnumerateDeviceExtensionProperties(phy_device, NULL, &extprops_count, NULL);
        VkExtensionProperties *ext_props = ngli_calloc(extprops_count, sizeof(*ext_props));
        if (!ext_props)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        vkEnumerateDeviceExtensionProperties(phy_device, NULL, &extprops_count, ext_props);
        LOG(DEBUG, "  extensions available:");
        for (uint32_t j = 0; j < extprops_count; j++) {
            LOG(DEBUG, "    %d/%d: %s v%d", j+1, extprops_count,
                   ext_props[j].extensionName, ext_props[j].specVersion);
        }

        /* Filtered device extensions */
        VkExtensionProperties *my_ext_props;
        uint32_t my_ext_props_count;
        uint32_t my_ext_props_target_count = NGLI_ARRAY_NB(my_device_extension_names);
        VkResult ret = get_filtered_ext_props(ext_props, extprops_count,
                                              &my_ext_props, &my_ext_props_count,
                                              my_device_extension_names, NGLI_ARRAY_NB(my_device_extension_names));
        ngli_free(ext_props);
        ngli_free(my_ext_props);
        if (ret != VK_SUCCESS)
            return ret;

        /* Swapchain support */
        if (!s->config.offscreen) {
        ret = query_swapchain_support(&vk->swapchain_support, vk->surface, phy_device);
        if (ret != VK_SUCCESS)
            return ret;
        LOG(DEBUG, "  Swapchain: %d formats, %d presentation modes",
               vk->swapchain_support.nb_formats, vk->swapchain_support.nb_present_modes);
        }

        /* Device selection criterias */
        LOG(DEBUG, "  Graphics/Compute:%d Present:%d DeviceEXT:%d/%d",
               queue_family_graphics_id, queue_family_present_id,
               my_ext_props_count, my_ext_props_target_count);
        if (!vk->physical_device &&
            queue_family_graphics_id != -1 &&
            (s->config.offscreen || queue_family_present_id != -1) &&
            my_ext_props_count == my_ext_props_target_count &&
            (s->config.offscreen ||
            (vk->swapchain_support.nb_formats &&
            vk->swapchain_support.nb_present_modes))) {
            LOG(DEBUG, "  -> device selected");
            vk->physical_device = phy_device;
            vk->queue_family_graphics_id = queue_family_graphics_id;
            vk->queue_family_present_id = queue_family_present_id;
        }
    }
    ngli_free(phy_devices);
    if (!vk->physical_device) {
        LOG(ERROR, "no valid physical device found");
        return VK_ERROR_DEVICE_LOST;
    }

    vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &vk->phydev_mem_props);

    return VK_SUCCESS;
}

static VkResult create_vulkan_device(struct vkcontext *vk)
{
    /* Device Queue info */
    int nb_queues = 1;
    float queue_priority = 1.0;

    VkDeviceQueueCreateInfo graphic_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk->queue_family_graphics_id,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkDeviceQueueCreateInfo queues_create_info[2];
    queues_create_info[0] = graphic_queue_create_info;
    if (vk->queue_family_graphics_id != vk->queue_family_present_id &&
        vk->queue_family_present_id != -1) {
        // XXX: needed?
        VkDeviceQueueCreateInfo present_queue_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk->queue_family_present_id,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };
        queues_create_info[nb_queues++] = present_queue_create_info;
    }

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queues_create_info,
        .queueCreateInfoCount = nb_queues,
        .enabledExtensionCount = NGLI_ARRAY_NB(my_device_extension_names),
        .ppEnabledExtensionNames = my_device_extension_names,
    };
    VkResult ret = vkCreateDevice(vk->physical_device, &device_create_info, NULL, &vk->device);
    if (ret != VK_SUCCESS)
        return ret;

    vkGetDeviceQueue(vk->device, vk->queue_family_graphics_id, 0, &vk->graphic_queue);
    if (vk->queue_family_present_id != -1)
    vkGetDeviceQueue(vk->device, vk->queue_family_present_id, 0, &vk->present_queue);
    return ret;
}

static VkResult create_swapchain(struct vkcontext *vk)
{
    // set maximum in flight frames
    vk->nb_in_flight_frames = 2;

    // re-query the swapchain to get current extent
    VkResult ret = query_swapchain_support(&vk->swapchain_support, vk->surface, vk->physical_device);
    if (ret != VK_SUCCESS)
        return ret;

    const struct vk_swapchain_support *swap = &vk->swapchain_support;

    vk->surface_format = select_swapchain_surface_format(swap->formats, swap->nb_formats);
    vk->present_mode = select_swapchain_present_mode(swap->present_modes, swap->nb_present_modes);
    vk->extent = select_swapchain_current_extent(vk, swap->caps);
    vk->config.width = vk->extent.width;
    vk->config.height = vk->extent.height;
    LOG(DEBUG, "current extent: %dx%d", vk->extent.width, vk->extent.height);

    uint32_t img_count = swap->caps.minImageCount + 1;
    if (swap->caps.maxImageCount && img_count > swap->caps.maxImageCount)
        img_count = swap->caps.maxImageCount;
    LOG(INFO, "swapchain image count: %d [%d-%d]", img_count,
           swap->caps.minImageCount, swap->caps.maxImageCount);

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk->surface,
        .minImageCount = img_count,
        .imageFormat = vk->surface_format.format,
        .imageColorSpace = vk->surface_format.colorSpace,
        .imageExtent = vk->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = swap->caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = vk->present_mode,
        .clipped = VK_TRUE,
    };

    const uint32_t queue_family_indices[2] = {
        vk->queue_family_graphics_id,
        vk->queue_family_present_id,
    };
    if (queue_family_indices[0] != queue_family_indices[1]) {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = NGLI_ARRAY_NB(queue_family_indices);
        swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
    }

    return vkCreateSwapchainKHR(vk->device, &swapchain_create_info, NULL, &vk->swapchain);
}

// XXX: re-entrant
static VkResult create_swapchain_images(struct vkcontext *vk)
{
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->nb_images, NULL);
    VkImage *imgs = ngli_realloc(vk->images, vk->nb_images * sizeof(*vk->images));
    if (!imgs)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    vk->images = imgs;
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->nb_images, vk->images);
    return VK_SUCCESS;
}

static VkResult create_swapchain_image_views(struct vkcontext *vk)
{
    vk->nb_image_views = vk->nb_images;
    vk->image_views = ngli_calloc(vk->nb_image_views, sizeof(*vk->image_views));
    if (!vk->image_views)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < vk->nb_images; i++) {
        VkImageViewCreateInfo image_view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk->surface_format.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        VkResult ret = vkCreateImageView(vk->device, &image_view_create_info,
                                         NULL, &vk->image_views[i]);
        if (ret != VK_SUCCESS)
            return ret;
    }

    return VK_SUCCESS;
}

int ngli_vk_begin_transient_command(struct vkcontext *vk, VkCommandBuffer *command_buffer)
{
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = vk->transient_command_buffer_pool,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(vk->device, &alloc_info, command_buffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(*command_buffer, &beginInfo);

    return 0;
}

int ngli_vk_execute_transient_command(struct vkcontext *vk, VkCommandBuffer command_buffer)
{
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };

    vkResetFences(vk->device, 1, &vk->transient_command_buffer_fence);

    VkQueue graphic_queue;
    vkGetDeviceQueue(vk->device, vk->queue_family_graphics_id, 0, &graphic_queue);

    VkResult ret = vkQueueSubmit(graphic_queue, 1, &submit_info, vk->transient_command_buffer_fence);
    if (ret != VK_SUCCESS)
        return ret;
    vkWaitForFences(vk->device, 1, &vk->transient_command_buffer_fence, 1, UINT64_MAX);

    vkQueueWaitIdle(graphic_queue);
    vkFreeCommandBuffers(vk->device, vk->transient_command_buffer_pool, 1, &command_buffer);

    return ret;
}

static VkResult create_depth_images(struct ngl_ctx *ctx, struct vkcontext *vk)
{
    vk->nb_depth_images = vk->nb_images;
    vk->depth_images = ngli_calloc(vk->nb_depth_images, sizeof(*vk->depth_images));
    if (!vk->depth_images)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    // FIXME: memory leak on error
    vk->depth_image_views = ngli_calloc(vk->nb_depth_images, sizeof(*vk->depth_image_views));
    if (!vk->depth_image_views)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    vk->depth_device_memories = ngli_calloc(vk->nb_depth_images, sizeof(*vk->depth_device_memories));
    if (!vk->depth_device_memories)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    for (uint32_t i = 0; i < vk->nb_depth_images; i++) {
        VkImageCreateInfo image_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .extent.width = vk->config.width,
            .extent.height = vk->config.height,
            .extent.depth = 1,
            .mipLevels = 1,
            .arrayLayers = 1,
            .format = vk->prefered_vk_depth_stencil_format,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VkImage *imagep = &vk->depth_images[i];
        VkResult ret = vkCreateImage(vk->device, &image_create_info, NULL, imagep);
        if (ret != VK_SUCCESS)
            return ret;
        VkImage image = *imagep;

        VkMemoryRequirements mem_requirements;
        vkGetImageMemoryRequirements(vk->device, image, &mem_requirements);

        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mem_requirements.size,
            .memoryTypeIndex = ngli_vk_find_memory_type(vk, mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };

        ret = vkAllocateMemory(vk->device, &alloc_info, NULL, &vk->depth_device_memories[i]);
        if (ret != VK_SUCCESS)
            return ret;

        vkBindImageMemory(vk->device, image, vk->depth_device_memories[i], 0);

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        ret = ngli_vk_begin_transient_command(vk, &command_buffer);
        if (ret < 0)
            return ret;

        const VkImageSubresourceRange subres_range = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };

        ngli_vk_transition_image_layout(vk, command_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, &subres_range);
        ret = ngli_vk_execute_transient_command(vk, command_buffer);
        if (ret < 0)
            return ret;

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk->prefered_vk_depth_stencil_format,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
        };

        if (vkCreateImageView(vk->device, &view_info, NULL, &vk->depth_image_views[i]) != VK_SUCCESS) {
            return -1;
        }
    }

    return VK_SUCCESS;
}

static VkResult create_render_pass(struct vkcontext *vk)
{
    VkAttachmentDescription color_attachment = {
        .format = vk->surface_format.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference color_attachment_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription depth_attachment = {
        .format = vk->prefered_vk_depth_stencil_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depth_attachment_ref = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pDepthStencilAttachment = &depth_attachment_ref,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkAttachmentDescription attachments[] = { color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = NGLI_ARRAY_NB(attachments),
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkResult vkret = vkCreateRenderPass(vk->device, &render_pass_create_info, NULL, &vk->render_pass);
    if (vkret != VK_SUCCESS)
        return vkret;

    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

    vkret = vkCreateRenderPass(vk->device, &render_pass_create_info, NULL, &vk->conservative_render_pass);
    if (vkret != VK_SUCCESS)
        return vkret;

    return VK_SUCCESS;
}

static VkResult create_swapchain_framebuffers(struct vkcontext *vk)
{
    vk->nb_framebuffers = vk->nb_image_views;
    vk->framebuffers = ngli_calloc(vk->nb_framebuffers, sizeof(*vk->framebuffers));
    if (!vk->framebuffers)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < vk->nb_framebuffers; i++) {
        VkImageView attachments[] = {
            vk->image_views[i],
            vk->depth_image_views[i],
        };

        VkFramebufferCreateInfo framebuffer_create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk->render_pass,
            .attachmentCount = NGLI_ARRAY_NB(attachments),
            .pAttachments = attachments,
            .width = vk->extent.width,
            .height = vk->extent.height,
            .layers = 1,
        };

        VkResult ret = vkCreateFramebuffer(vk->device, &framebuffer_create_info,
                                           NULL, &vk->framebuffers[i]);
        if (ret != VK_SUCCESS)
            return ret;
    }

    return VK_SUCCESS;
}

static int create_command_pool_and_buffers(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk->queue_family_graphics_id,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // FIXME
    };

    VkResult vkret = vkCreateCommandPool(vk->device, &command_pool_create_info, NULL, &vk->command_buffer_pool);
    if (vkret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    vk->command_buffers = ngli_calloc(vk->nb_framebuffers, sizeof(*vk->command_buffers));
    if (!vk->command_buffers)
        return NGL_ERROR_MEMORY;

    VkCommandBufferAllocateInfo command_buffers_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->command_buffer_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vk->nb_framebuffers,
    };

    VkResult ret = vkAllocateCommandBuffers(vk->device, &command_buffers_allocate_info, vk->command_buffers);
    if (ret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    return 0;
}

static void destroy_command_pool_and_buffers(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;

    vkFreeCommandBuffers(vk->device, vk->command_buffer_pool, vk->nb_framebuffers, vk->command_buffers);
    ngli_free(vk->command_buffers);

    vkDestroyCommandPool(vk->device, vk->command_buffer_pool, NULL);
}

static VkResult create_semaphores(struct vkcontext *vk)
{
    VkResult ret;

    vk->sem_img_avail = ngli_calloc(vk->nb_in_flight_frames, sizeof(VkSemaphore));
    if (!vk->sem_img_avail)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    vk->sem_render_finished = ngli_calloc(vk->nb_in_flight_frames, sizeof(VkSemaphore));
    if (!vk->sem_render_finished)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    vk->fences = ngli_calloc(vk->nb_in_flight_frames, sizeof(VkFence));
    if (!vk->fences)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (int i = 0; i < vk->nb_in_flight_frames; i++) {
        if ((ret = vkCreateSemaphore(vk->device, &semaphore_create_info, NULL,
                                    &vk->sem_img_avail[i])) != VK_SUCCESS ||
            (ret = vkCreateSemaphore(vk->device, &semaphore_create_info, NULL,
                                    &vk->sem_render_finished[i])) != VK_SUCCESS ||
            (ret = vkCreateFence(vk->device, &fence_create_info, NULL,
                                 &vk->fences[i])) != VK_SUCCESS) {
            return ret;
        }
    }
    return VK_SUCCESS;
}

static VkResult create_window_surface(struct vkcontext *vk,
                                      uintptr_t display,
                                      uintptr_t window,
                                      VkSurfaceKHR *surface)
{
    VkResult ret = VK_ERROR_FEATURE_NOT_PRESENT;

    if (vk->surface_create_type == VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR) {
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        VkXlibSurfaceCreateInfoKHR surface_create_info = {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = (Display *)display,
            .window = window,
        };

        if (!surface_create_info.dpy) {
            surface_create_info.dpy = XOpenDisplay(NULL);
            if (!surface_create_info.dpy) {
                LOG(ERROR, "could not retrieve X display");
                return -1;
            }
            // TODO ngli_free display
            //x11->own_display = 1;
        }

        PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR = vulkan_get_proc_addr(vk, "vkCreateXlibSurfaceKHR");
        if (!vkCreateXlibSurfaceKHR)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        ret = vkCreateXlibSurfaceKHR(vk->instance, &surface_create_info, NULL, surface);
#endif
    } else if (vk->surface_create_type == VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR) {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        VkAndroidSurfaceCreateInfoKHR surface_create_info = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .window = (void *)window,
        };

        PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR = vulkan_get_proc_addr(vk, "vkCreateAndroidSurfaceKHR");
        if (!vkCreateAndroidSurfaceKHR)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        ret = vkCreateAndroidSurfaceKHR(vk->instance, &surface_create_info, NULL, surface);
#endif
    } else if (vk->surface_create_type == VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK) {
#if defined(VK_USE_PLATFORM_MACOS_MVK)
        VkMacOSSurfaceCreateInfoMVK surface_create_info = {
            .sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK,
            .pView = (const void *)window,
        };

        PFN_vkCreateMacOSSurfaceMVK vkCreateMacOSSurfaceMVK = vulkan_get_proc_addr(vk, "vkCreateMacOSSurfaceMVK");
        if (!vkCreateMacOSSurfaceMVK)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        ret = vkCreateMacOSSurfaceMVK(vk->instance, &surface_create_info, NULL, surface);
#endif
    } else if (vk->surface_create_type == VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK) {
#if defined(VK_USE_PLATFORM_IOS_MVK)
        VkIOSSurfaceCreateInfoMVK surface_create_info = {
            .sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK,
            .pView = (const void *)window,
        };

        PFN_vkCreateIOSSurfaceMVK vkCreateIOSSurfaceMVK = vulkan_get_proc_addr(vk, "vkCreateIOSSurfaceMVK");
        if (!vkCreateIOSSurfaceMVK)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        ret = vkCreateIOSSurfaceMVK(vk->instance, &surface_create_info, NULL, surface);
#endif
    } else {
        // TODO
        ngli_assert(0);
    }

    return ret;
}

static int vulkan_init(struct ngl_ctx *s, struct vkcontext *vk, uintptr_t display, uintptr_t window)
{
    VkResult ret;
    struct ngl_config *config = &s->config;

    ngli_darray_init(&vk->wait_semaphores, sizeof(VkSemaphore), 0);
    ngli_darray_init(&vk->wait_stages, sizeof(VkPipelineStageFlagBits), 0);
    ngli_darray_init(&vk->signal_semaphores, sizeof(VkSemaphore), 0);

    vk->spirv_compiler      = shaderc_compiler_initialize();
    vk->spirv_compiler_opts = shaderc_compile_options_initialize();
    if (!vk->spirv_compiler || !vk->spirv_compiler_opts)
        return -1;

    //const shaderc_env_version env_version = app_info.apiVersion == VK_API_VERSION_1_0 ? shaderc_env_version_vulkan_1_0
    //                                                                                  : shaderc_env_version_vulkan_1_1;
    const shaderc_env_version env_version = shaderc_env_version_vulkan_1_0;
    shaderc_compile_options_set_target_env(vk->spirv_compiler_opts, shaderc_target_env_vulkan, env_version);
    shaderc_compile_options_set_invert_y(vk->spirv_compiler_opts, 1);

    //shaderc_compile_options_set_optimization_level(vk->spirv_compiler_opts,
    //                                               shaderc_optimization_level_performance);

    //ngli_darray_init(&vk->command_buffers, sizeof(VkCommandBuffer), 0);

    if ((ret = probe_vulkan_extensions(vk)) != VK_SUCCESS ||
        (ret = list_vulkan_layers()) != VK_SUCCESS ||
        (ret = create_vulkan_instance(vk)) != VK_SUCCESS)
        return -1;

#if ENABLE_DEBUG
    if ((ret = setup_vulkan_debug_callback(vk)) != VK_SUCCESS)
        return -1;
#endif

    if (!s->config.offscreen) {
        if ((ret = create_window_surface(vk, display, window, &vk->surface)) != VK_SUCCESS)
            return -1;
    }

    if ((ret = select_vulkan_physical_device(s, vk)) != VK_SUCCESS ||
        (ret = create_vulkan_device(vk)) != VK_SUCCESS)
        return -1;

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk->queue_family_graphics_id,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    /* FIXME: check return */
    vkCreateCommandPool(vk->device, &command_pool_create_info, NULL, &vk->transient_command_buffer_pool);

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };

    vkCreateFence(vk->device, &fence_create_info, NULL, &vk->transient_command_buffer_fence);

    ret = probe_prefered_depth_stencil_format(vk);
    if (ret != VK_SUCCESS)
        return -1;

    if (config->offscreen) {
        struct texture_params texture_params = NGLI_TEXTURE_PARAM_DEFAULTS;
        texture_params.width = config->width;
        texture_params.height = config->height;
        texture_params.format = NGLI_FORMAT_R8G8B8A8_UNORM;
        ngli_texture_init(&vk->rt_color, s, &texture_params);

        texture_params.format = vk->prefered_depth_stencil_format;
        ngli_texture_init(&vk->rt_depth_stencil, s, &texture_params);

        struct rendertarget_params rt_params = {
            .width = config->width,
            .height = config->height,
            .colors[0] = &vk->rt_color,
            .nb_colors = 1,
            .depth_stencil = &vk->rt_depth_stencil,
            .readable = 1,
        };
        ngli_rendertarget_init(&vk->rt, s, &rt_params);
        vk->nb_framebuffers = 1;
        vk->nb_in_flight_frames = 1;
    } else {
        if ((ret = create_swapchain(vk)) != VK_SUCCESS ||
            (ret = create_swapchain_images(vk)) != VK_SUCCESS ||
            (ret = create_swapchain_image_views(vk)) != VK_SUCCESS ||
            (ret = create_depth_images(s, vk)) != VK_SUCCESS ||
            (ret = create_render_pass(vk)) != VK_SUCCESS ||
            (ret = create_swapchain_framebuffers(vk)) != VK_SUCCESS)
            return -1;
    }

    if ((ret = create_semaphores(vk)) != VK_SUCCESS) {
        return -1;
    }

    create_command_pool_and_buffers(s);

    return 0;
}

static VkResult vulkan_swap_buffers(struct vkcontext *vk)
{
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = ngli_darray_count(&vk->signal_semaphores),
        .pWaitSemaphores = ngli_darray_data(&vk->signal_semaphores),
        .swapchainCount = 1,
        .pSwapchains = &vk->swapchain,
        .pImageIndices = &vk->img_index,
    };

    int ret = vkQueuePresentKHR(vk->present_queue, &present_info);

    vk->signal_semaphores.count = 0;

    if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
        LOG(ERROR, "PRESENT OUT OF DATE");
    } else if (ret == VK_SUBOPTIMAL_KHR) {
        LOG(ERROR, "PRESENT SUBOPTIMAL");
    } else if (ret != VK_SUCCESS) {
        LOG(ERROR, "failed to present image %s", vk_res2str(ret));
    }

    vk->current_frame = (vk->current_frame + 1) % vk->nb_in_flight_frames;
    return ret;
}

static void cleanup_swapchain(struct vkcontext *vk)
{
    for (uint32_t i = 0; i < vk->nb_framebuffers; i++)
        vkDestroyFramebuffer(vk->device, vk->framebuffers[i], NULL);

    vkDestroyRenderPass(vk->device, vk->render_pass, NULL);
    vkDestroyRenderPass(vk->device, vk->conservative_render_pass, NULL);

    for (uint32_t i = 0; i < vk->nb_image_views; i++)
        vkDestroyImageView(vk->device, vk->image_views[i], NULL);

    for (uint32_t i = 0; i < vk->nb_depth_images; i++) {
        vkDestroyImageView(vk->device, vk->depth_image_views[i], NULL);
        vkDestroyImage(vk->device, vk->depth_images[i], NULL);
        vkFreeMemory(vk->device, vk->depth_device_memories[i], NULL);
    }

    vkDestroySwapchainKHR(vk->device, vk->swapchain, NULL);
}

// XXX: window minimizing? (fb gets zero width or height)
static int reset_swapchain(struct ngl_ctx *ctx, struct vkcontext *vk)
{
    VkResult ret;

    vkDeviceWaitIdle(vk->device);
    cleanup_swapchain(vk);
    if ((ret = create_swapchain(vk)) != VK_SUCCESS ||
        (ret = create_swapchain_images(vk)) != VK_SUCCESS ||
        (ret = create_swapchain_image_views(vk)) != VK_SUCCESS ||
        (ret = create_depth_images(ctx, vk)) != VK_SUCCESS ||
        (ret = create_render_pass(vk)) != VK_SUCCESS ||
        (ret = create_swapchain_framebuffers(vk)) != VK_SUCCESS)
        return -1;

    return 0;
}

static void vulkan_uninit(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;
    vkDeviceWaitIdle(vk->device);

    destroy_command_pool_and_buffers(s);

    for (int i = 0; i < vk->nb_in_flight_frames; i++) {
        vkDestroySemaphore(vk->device, vk->sem_render_finished[i], NULL);
        vkDestroySemaphore(vk->device, vk->sem_img_avail[i], NULL);
        vkDestroyFence(vk->device, vk->fences[i], NULL);
    }
    ngli_free(vk->sem_render_finished);
    ngli_free(vk->sem_img_avail);
    ngli_free(vk->fences);

    if (s->config.offscreen) {
        ngli_rendertarget_reset(&vk->rt);
        ngli_texture_reset(&vk->rt_color);
        ngli_texture_reset(&vk->rt_depth_stencil);
    }

    if (!s->config.offscreen)
    cleanup_swapchain(vk);

    vkDestroyCommandPool(vk->device, vk->transient_command_buffer_pool, NULL);
    vkDestroyFence(vk->device, vk->transient_command_buffer_fence, NULL);

    ngli_free(vk->swapchain_support.formats);
    ngli_free(vk->swapchain_support.present_modes);
    if (!s->config.offscreen)
    vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);
    ngli_free(vk->images);
    ngli_free(vk->image_views);
    ngli_free(vk->depth_images);
    ngli_free(vk->depth_image_views);
    ngli_free(vk->depth_device_memories);
    vkDestroyDevice(vk->device, NULL);
#if ENABLE_DEBUG
    void *proc_addr = vulkan_get_proc_addr(vk, "vkDestroyDebugUtilsMessengerEXT");
    if (proc_addr) {
        PFN_vkDestroyDebugUtilsMessengerEXT func = proc_addr;
        func(vk->instance, vk->report_callback, NULL);
    }
#endif
    vkDestroyInstance(vk->instance, NULL);

    shaderc_compiler_release(vk->spirv_compiler);
    shaderc_compile_options_release(vk->spirv_compiler_opts);

    ngli_darray_reset(&vk->wait_semaphores);
    ngli_darray_reset(&vk->wait_stages);
    ngli_darray_reset(&vk->signal_semaphores);
}

static int vk_configure(struct ngl_ctx *s, const struct ngl_config *config)
{
    struct vkcontext *vk = ngli_calloc(1, sizeof(*vk));
    if (!vk)
        return -1;
    s->vkcontext = vk;

    memcpy(&vk->config, config, sizeof(*config));
    memcpy(&s->config, config, sizeof(*config));

    int ret = vulkan_init(s, vk, config->display, config->window);
    if (ret < 0) {
        ngli_free(vk);
        return ret;
    }

    ret = ngli_pgcache_init(&s->pgcache, s);
    if (ret < 0)
        return ret;

    const int *viewport = config->viewport;
    if (viewport[2] > 0 && viewport[3] > 0) {
        ngli_gctx_set_viewport(s, viewport);
    } else {
        const int default_viewport[] = {0, 0, config->width, config->height};
        ngli_gctx_set_viewport(s, default_viewport);
    }

    const GLint scissor[] = {0, 0, config->width, config->height};
    ngli_gctx_set_scissor(s, scissor);

    ngli_gctx_set_clear_color(s, config->clear_color);

    struct graphicstate *graphicstate = &s->graphicstate;
    ngli_graphicstate_init(graphicstate);

    if (config->offscreen) {
        s->default_rendertarget_desc.color_formats[0] = NGLI_FORMAT_R8G8B8A8_UNORM;
        s->default_rendertarget_desc.nb_color_formats = 1;
        s->default_rendertarget_desc.depth_stencil_format = vk->prefered_depth_stencil_format;
        s->rendertarget_desc = &s->default_rendertarget_desc;
        ngli_gctx_set_rendertarget(s, &vk->rt, 0);
    } else {
        s->default_rendertarget_desc.color_formats[0] = NGLI_FORMAT_B8G8R8A8_UNORM;
        s->default_rendertarget_desc.nb_color_formats = 1;
        s->default_rendertarget_desc.depth_stencil_format = vk->prefered_depth_stencil_format;
        s->rendertarget_desc = &s->default_rendertarget_desc;
        ngli_gctx_set_rendertarget(s, NULL, 0);
    }

    return 0;
}

static int vk_resize(struct ngl_ctx *s, int width, int height, const int *viewport)
{
    struct ngl_config *config = &s->config;
    config->width = width;
    config->height = height;

    if (viewport && viewport[2] > 0 && viewport[3] > 0) {
        ngli_gctx_set_viewport(s, viewport);
    } else {
        const int default_viewport[] = {0, 0, config->width, config->height};
        ngli_gctx_set_viewport(s, default_viewport);
    }

    const int scissor[] = {0, 0, config->width, config->height};
    ngli_gctx_set_scissor(s, scissor);

    return 0;
}

static int vk_pre_draw(struct ngl_ctx *s, double t)
{
    int ret = 0;
    struct vkcontext *vk = s->vkcontext;

    vkWaitForFences(vk->device, 1, &vk->fences[vk->current_frame], VK_TRUE, UINT64_MAX);
    vkResetFences(vk->device, 1, &vk->fences[vk->current_frame]);

    struct ngl_config *config = &s->config;
    struct rendertarget *rt = config->offscreen ? &vk->rt : NULL;
    if (!config->offscreen) {
        /* TODO: swapchain_swap_buffers() | swapchain_acquire_image() */
        VkResult vkret = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX, vk->sem_img_avail[vk->current_frame], NULL, &vk->img_index);
        if (vkret == VK_ERROR_OUT_OF_DATE_KHR) {
            ret = reset_swapchain(s, vk);
            if (ret >= 0) {
                VkResult vkret = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX, vk->sem_img_avail[vk->current_frame], NULL, &vk->img_index);
                if (vkret != VK_SUCCESS) {
                    LOG(ERROR, "failed to acquire image after resetting the swap chain");
                    ret = -1;
                }
            }
        } else if (vkret == VK_SUBOPTIMAL_KHR) {
            ret = reset_swapchain(s, vk);
        } else if (vkret != VK_SUCCESS) {
            LOG(ERROR, "failed to acquire image");
            ret = -1;
        } else {
            ret = 0;
        }

        if (ret < 0)
            return ret;

        if (!ngli_darray_push(&vk->wait_semaphores, &vk->sem_img_avail[vk->current_frame]))
            return NGL_ERROR_MEMORY;

        VkPipelineStageFlagBits wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (!ngli_darray_push(&vk->wait_stages, &wait_stage))
            return NGL_ERROR_MEMORY;

        if (!ngli_darray_push(&vk->signal_semaphores, &vk->sem_render_finished[vk->current_frame]))
            return NGL_ERROR_MEMORY;
    }

    vk->cur_command_buffer = vk->command_buffers[vk->img_index];
    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    };
    VkResult vkret = vkBeginCommandBuffer(vk->cur_command_buffer, &command_buffer_begin_info);
    if (vkret != VK_SUCCESS)
        return -1;
    vk->cur_command_buffer_state = 1;

    ngli_gctx_set_rendertarget(s, rt, 0);
    ngli_gctx_clear_color(s);
    ngli_gctx_clear_depth_stencil(s);
    ngli_gctx_clear_depth_stencil(s);

    return ret;
}

static int vk_post_draw(struct ngl_ctx *s, double t)
{
    struct vkcontext *vk = s->vkcontext;


    struct ngl_config *config = &s->config;
    if (config->offscreen) {
        if (config->capture_buffer)
            ngli_rendertarget_read_pixels(&vk->rt, config->capture_buffer);
        ngli_gctx_flush(s);
        vk->current_frame = (vk->current_frame + 1) % vk->nb_in_flight_frames;
    } else {
        ngli_gctx_vk_end_render_pass(s);

        const VkImageSubresourceRange subres_range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };
        ngli_vk_transition_image_layout(vk, vk->cur_command_buffer, vk->images[vk->img_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &subres_range);

        ngli_gctx_flush(s);
        VkResult vkret = vulkan_swap_buffers(vk);
        if (vkret != VK_SUCCESS)
            return -1;
    }

    /* Reset cur_command_buffer so updating resources will use a transient
     * command buffer */
    vk->cur_command_buffer = VK_NULL_HANDLE;

    return 0;
}

static void vk_destroy(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;
    ngli_pgcache_reset(&s->pgcache);
    vulkan_uninit(s);
    ngli_free(vk);
    s->vkcontext = NULL;
}

int ngli_vk_find_memory_type(struct vkcontext *vk, uint32_t type_filter, VkMemoryPropertyFlags props)
{
    for (int i = 0; i < vk->phydev_mem_props.memoryTypeCount; i++)
        if ((type_filter & (1<<i)) && (vk->phydev_mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return -1;
}

static void vk_wait_idle(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;
    vkDeviceWaitIdle(vk->device);
}

const struct backend ngli_backend_vk = {
    .name         = "Vulkan",
    .configure    = vk_configure,
    .resize       = vk_resize,
    .pre_draw     = vk_pre_draw,
    .post_draw    = vk_post_draw,
    .wait_idle    = vk_wait_idle,
    .destroy      = vk_destroy,
};
