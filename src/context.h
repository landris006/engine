#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <ranges>
#include <vulkan/vulkan.hpp>

static const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
static const char* extensions[] = {vk::KHRSwapchainExtensionName,
                                   vk::KHRRayTracingPipelineExtensionName,
                                   vk::KHRAccelerationStructureExtensionName,
                                   vk::KHRDeferredHostOperationsExtensionName};

struct AllocatedImage {
  vk::UniqueImage handle;
  vk::UniqueDeviceMemory memory;
  vk::UniqueImageView view;
};

struct AllocatedBuffer {
  vk::UniqueBuffer handle;
  vk::UniqueDeviceMemory memory;
  vk::DeviceAddress address;
};

struct Context {
  vk::UniqueInstance instance;
  vk::PhysicalDevice physical_device;
  vk::UniqueDevice device;
  vk::UniqueCommandPool command_pool;
  vk::Queue graphics_queue;
  uint32_t graphics_queue_idx;
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

  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  auto instance = vk::createInstanceUnique(instance_create_info);

  VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());

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

  uint32_t chosen_index = -1;
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

  auto bda_features =
      vk::PhysicalDeviceBufferDeviceAddressFeatures().setBufferDeviceAddress(
          true);
  auto scalar_features = vk::PhysicalDeviceScalarBlockLayoutFeatures()
                             .setScalarBlockLayout(true)
                             .setPNext(&bda_features);
  auto as_features = vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
                         .setAccelerationStructure(true)
                         .setPNext(&scalar_features);
  auto rt_features = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
                         .setRayTracingPipeline(true)
                         .setPNext(&as_features);

  auto features = vk::PhysicalDeviceFeatures().setShaderInt64(true);

  auto device = selected_device.createDeviceUnique(
      vk::DeviceCreateInfo()
          .setPEnabledFeatures(&features)
          .setQueueCreateInfos(queue_create_info)
          .setPEnabledExtensionNames(extensions)
          .setPNext(&rt_features));

  VULKAN_HPP_DEFAULT_DISPATCHER.init(device.get());

  printf("Device Extensions:\n");
  for (const auto& extension : extensions) {
    printf(" - %s\n", extension);
  }

  auto graphics_queue = device->getQueue(chosen_index, 0);

  auto command_pool = device->createCommandPoolUnique(
      vk::CommandPoolCreateInfo()
          .setQueueFamilyIndex(chosen_index)
          .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer));

  return ::Context{
      .instance = std::move(instance),
      .physical_device = selected_device,
      .device = std::move(device),
      .command_pool = std::move(command_pool),
      .graphics_queue = graphics_queue,
      .graphics_queue_idx = chosen_index,
  };
}

static auto findMemoryType(const vk::PhysicalDevice& physical_device,
                           uint32_t type_filter,
                           vk::MemoryPropertyFlags properties) -> uint32_t {
  auto mem_props = physical_device.getMemoryProperties();
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
    if ((type_filter & (1 << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  fprintf(stderr, "[vulkan] Error: No suitable memory type found.\n");
  std::abort();
}

static auto createImage(const ::Context& context, vk::Extent3D extent,
                        vk::Format format, vk::ImageUsageFlags usage)
    -> AllocatedImage {
  auto device = context.device.get();

  auto image = device.createImageUnique(
      vk::ImageCreateInfo()
          .setImageType(vk::ImageType::e2D)
          .setFormat(format)
          .setExtent(extent)
          .setMipLevels(1)
          .setArrayLayers(1)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setTiling(vk::ImageTiling::eOptimal)
          .setUsage(usage)
          .setSharingMode(vk::SharingMode::eExclusive)
          .setQueueFamilyIndexCount(0)
          .setPQueueFamilyIndices(nullptr)
          .setInitialLayout(vk::ImageLayout::eUndefined));

  auto requirements = device.getImageMemoryRequirements(image.get());

  auto memory = device.allocateMemoryUnique(
      vk::MemoryAllocateInfo()
          .setAllocationSize(requirements.size)
          .setMemoryTypeIndex(findMemoryType(
              context.physical_device, requirements.memoryTypeBits,
              vk::MemoryPropertyFlagBits::eDeviceLocal)));

  device.bindImageMemory(image.get(), memory.get(), 0);

  auto view = device.createImageViewUnique(
      vk::ImageViewCreateInfo()
          .setImage(image.get())
          .setViewType(vk::ImageViewType::e2D)
          .setFormat(format)
          .setComponents(vk::ComponentMapping()
                             .setR(vk::ComponentSwizzle::eIdentity)
                             .setG(vk::ComponentSwizzle::eIdentity)
                             .setB(vk::ComponentSwizzle::eIdentity)
                             .setA(vk::ComponentSwizzle::eIdentity))
          .setSubresourceRange(
              vk::ImageSubresourceRange()
                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                  .setBaseMipLevel(0)
                  .setLevelCount(1)
                  .setBaseArrayLayer(0)
                  .setLayerCount(1)));

  return AllocatedImage{
      .handle = std::move(image),
      .memory = std::move(memory),
      .view = std::move(view),
  };
}

static auto submit_one_time_command(
    const Context& context, std::function<void(const vk::CommandBuffer&)> cb) {
  auto cmd_buf = context.device->allocateCommandBuffersUnique(
      vk::CommandBufferAllocateInfo()
          .setCommandPool(context.command_pool.get())
          .setLevel(vk::CommandBufferLevel::ePrimary)
          .setCommandBufferCount(1));

  {
    cmd_buf[0]->begin(vk::CommandBufferBeginInfo().setFlags(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    cb(cmd_buf[0].get());

    cmd_buf[0]->end();
  }

  context.graphics_queue.submit(
      vk::SubmitInfo().setCommandBuffers(cmd_buf[0].get()));

  context.graphics_queue.waitIdle();
}

static auto uploadToBuffer(const Context& ctx, const AllocatedBuffer& buf,
                           const void* data, vk::DeviceSize size) -> void {
  void* mapped = ctx.device->mapMemory(buf.memory.get(), 0, size);
  memcpy(mapped, data, size);
  ctx.device->unmapMemory(buf.memory.get());
}

static auto createBuffer(const Context& context, vk::DeviceSize size,
                         vk::BufferUsageFlags usage,
                         vk::MemoryPropertyFlags props) -> AllocatedBuffer {
  auto buffer = context.device->createBufferUnique(
      vk::BufferCreateInfo()
          .setSize(size)
          .setUsage(usage)
          .setSharingMode(vk::SharingMode::eExclusive)
          .setQueueFamilyIndexCount(0));

  auto requirements = context.device->getBufferMemoryRequirements(buffer.get());

  auto allocate_flags = vk::MemoryAllocateFlagsInfo{}.setFlags(
      vk::MemoryAllocateFlagBits::eDeviceAddress);

  auto memory = context.device->allocateMemoryUnique(
      vk::MemoryAllocateInfo()
          .setAllocationSize(requirements.size)
          .setMemoryTypeIndex(findMemoryType(
              context.physical_device, requirements.memoryTypeBits, props))
          .setPNext(&allocate_flags));

  context.device->bindBufferMemory(buffer.get(), memory.get(), 0);

  vk::DeviceAddress address = 0;
  if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
    address = context.device->getBufferAddress(buffer.get());
  }

  return AllocatedBuffer{
      .handle = std::move(buffer),
      .memory = std::move(memory),
      .address = address,
  };
};
