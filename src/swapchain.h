#pragma once

#include <vulkan/vulkan.hpp>

#include "context.h"

static constexpr int FRAMES_IN_FLIGHT = 2;

struct Swapchain {
  vk::UniqueSurfaceKHR surface;
  vk::UniqueSwapchainKHR handle;
  std::vector<vk::Image> images;
  std::vector<vk::UniqueImageView> image_views;
  vk::Format format;
  vk::Extent2D extent;
  std::vector<vk::UniqueSemaphore> image_available;
  std::vector<vk::UniqueSemaphore> render_finished;
  std::vector<vk::UniqueFence> in_flight;
  std::vector<vk::UniqueCommandBuffer> command_buffers;
};

Swapchain createSwapchain(GLFWwindow* window, const Context& ctx,
                          const vk::UniqueCommandPool& cmd_pool);
