#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>

#ifndef HEADLESS
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include <vulkan/vulkan.hpp>

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
  vk::UniqueSampler linear_sampler;
};

auto createContext() -> Context;

auto findMemoryType(const vk::PhysicalDevice& physical_device,
                    uint32_t type_filter, vk::MemoryPropertyFlags properties)
    -> uint32_t;

auto createImage(const ::Context& context, vk::Extent3D extent,
                 vk::Format format, vk::ImageUsageFlags usage)
    -> AllocatedImage;

void submit_one_time_command(const Context& context,
                             std::function<void(const vk::CommandBuffer&)> cb);

auto uploadToBuffer(const Context& ctx, const AllocatedBuffer& buf,
                    const void* data, vk::DeviceSize size) -> void;

auto createBuffer(const Context& context, vk::DeviceSize size,
                  vk::BufferUsageFlags usage, vk::MemoryPropertyFlags props)
    -> AllocatedBuffer;
