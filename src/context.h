#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <ranges>
#include <vulkan/vulkan.hpp>

static const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
static const char* extensions[] = {vk::KHRSwapchainExtensionName};

struct Context {
  vk::UniqueInstance instance;
  vk::PhysicalDevice physical_device;
  vk::UniqueDevice device;
  uint32_t graphics_queue_family_idx;
};

static auto createContext() -> Context {
  vk::ApplicationInfo appInfo("Vulkan.hpp Engine", 1, "No Engine", 1,
                              VK_API_VERSION_1_4);

  // Instance
  uint32_t count = 0;
  const char** exts = glfwGetRequiredInstanceExtensions(&count);

  printf("GLFW Required extensions:\n");
  for (uint32_t i = 0; i < count; i++) {
    printf(" - %s\n", exts[i]);
  }

  vk::InstanceCreateInfo instance_create_info({}, &appInfo);
  instance_create_info.enabledExtensionCount = count;
  instance_create_info.ppEnabledExtensionNames = exts;

#if defined(DEBUG)
  printf("debug enabled\n");
  instance_create_info.setPEnabledLayerNames(layers);
#endif

  auto instance = vk::createInstanceUnique(instance_create_info);

  // Physical Device
  auto devices = instance->enumeratePhysicalDevices();

  vk::PhysicalDevice selected_device;
  for (const auto& device : devices) {
    if (device.getProperties().deviceType ==
        vk::PhysicalDeviceType::eDiscreteGpu) {
      selected_device = device;
      break;
    }
  }
  if (!selected_device) {
    selected_device = devices.front();
  }

  printf("Selected device: %s\n",
         selected_device.getProperties().deviceName.data());

  int chosen_index = -1;
  auto queue_families = selected_device.getQueueFamilyProperties();
  for (const auto& [i, queue_family] : queue_families | std::views::enumerate) {
    if (queue_family.queueFlags & vk::QueueFlagBits::eGraphics) {
      chosen_index = i;
      break;
    }
  }
  if (chosen_index < 0) {
    fprintf(stderr, "[vulkan] Error: No graphics queue family found.\n");
    std::abort();
  }

  float priority = 1.0f;
  auto queue_create_info = vk::DeviceQueueCreateInfo()
                               .setQueueFamilyIndex(chosen_index)
                               .setQueueCount(1)
                               .setQueuePriorities(priority);

  auto device = selected_device.createDeviceUnique(
      vk::DeviceCreateInfo{}
          .setQueueCreateInfos(queue_create_info)
          .setPEnabledExtensionNames(extensions));

  return ::Context{
      .instance = std::move(instance),
      .physical_device = selected_device,
      .device = std::move(device),
      .graphics_queue_family_idx = static_cast<uint32_t>(chosen_index),
  };
}
