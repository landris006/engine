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

static auto createSwapchain(GLFWwindow* window, const Context& ctx,
                            const vk::UniqueCommandPool& cmd_pool)
    -> Swapchain {
  VkSurfaceKHR raw_surface;
  glfwCreateWindowSurface(ctx.instance.get(), window, nullptr, &raw_surface);
  auto surface = vk::UniqueSurfaceKHR(raw_surface, ctx.instance.get());

  auto capabilities =
      ctx.physical_device.getSurfaceCapabilitiesKHR(surface.get());

  auto formats = ctx.physical_device.getSurfaceFormatsKHR(surface.get());
  if (formats.empty()) {
    fprintf(stderr, "[vulkan] Error: No surface formats found.\n");
    std::abort();
  }

  auto selected_format = formats[0];
  for (const auto& format : formats) {
    if (format.format == vk::Format::eB8G8R8A8Unorm &&
        format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      selected_format = format;
      break;
    }
  }

  auto present_modes =
      ctx.physical_device.getSurfacePresentModesKHR(surface.get());
  vk::PresentModeKHR selected_present_mode = vk::PresentModeKHR::eFifo;
  for (const auto& present_mode : present_modes) {
    if (present_mode == vk::PresentModeKHR::eMailbox) {
      selected_present_mode = vk::PresentModeKHR::eMailbox;
      break;
    }
  }

  auto extent = capabilities.currentExtent;
  if (extent.width == std::numeric_limits<uint32_t>::max()) {
    extent.width = 1280;
    extent.height = 800;
  }

  auto swapchain = ctx.device->createSwapchainKHRUnique(
      vk::SwapchainCreateInfoKHR()
          .setSurface(surface.get())
          .setMinImageCount(capabilities.minImageCount + 1)
          .setImageFormat(selected_format.format)
          .setImageColorSpace(selected_format.colorSpace)
          .setImageExtent(extent)
          .setImageArrayLayers(1)
          .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment |
                         vk::ImageUsageFlagBits::eTransferDst)
          .setImageSharingMode(vk::SharingMode::eExclusive)
          .setPreTransform(capabilities.currentTransform)
          .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
          .setPresentMode(selected_present_mode)
          .setClipped(true));

  auto images = ctx.device->getSwapchainImagesKHR(swapchain.get());
  auto image_views = std::vector<vk::UniqueImageView>();

  auto render_finished_semaphores =
      std::vector<vk::UniqueSemaphore>(images.size());
  for (const auto& [i, image] : images | std::views::enumerate) {
    auto image_view = ctx.device->createImageViewUnique(
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(selected_format.format)
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
    image_views.push_back(std::move(image_view));

    render_finished_semaphores[i] =
        ctx.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
  }

  auto image_available_semaphores =
      std::vector<vk::UniqueSemaphore>(FRAMES_IN_FLIGHT);
  auto in_flight_fences = std::vector<vk::UniqueFence>(FRAMES_IN_FLIGHT);
  auto command_buffers = std::vector<vk::UniqueCommandBuffer>(FRAMES_IN_FLIGHT);

  for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
    image_available_semaphores[i] =
        ctx.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
    in_flight_fences[i] = ctx.device->createFenceUnique(
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
  }

  command_buffers = ctx.device->allocateCommandBuffersUnique(
      vk::CommandBufferAllocateInfo()
          .setCommandPool(cmd_pool.get())
          .setLevel(vk::CommandBufferLevel::ePrimary)
          .setCommandBufferCount(FRAMES_IN_FLIGHT));

  return Swapchain{
      .surface = std::move(surface),
      .handle = std::move(swapchain),
      .images = std::move(images),
      .image_views = std::move(image_views),
      .format = selected_format.format,
      .extent = extent,
      .image_available = std::move(image_available_semaphores),
      .render_finished = std::move(render_finished_semaphores),
      .in_flight = std::move(in_flight_fences),
      .command_buffers = std::move(command_buffers),
  };
}
