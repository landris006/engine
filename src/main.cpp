#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <utils.h>
#include <vulkan/vulkan_core.h>

#include "scene.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "context.h"
#include "rt_pipeline.h"
#include "swapchain.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

int main() {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    std::abort();
  }

  if (!glfwVulkanSupported()) {
    printf("GLFW: Vulkan Not Supported\n");
    std::abort();
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 800, "Hello", nullptr, nullptr);

  {
    auto context = createContext();
    auto swapchain = createSwapchain(window, context, context.command_pool);

    // Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
    // slang::createGlobalSession(slangGlobalSession.writePosition());

    // IMGUI_CHECKVERSION();
    // ImGui::CreateContext();
    // ImGui_ImplGlfw_InitForVulkan(window, true);
    //
    // ImGui_ImplVulkan_InitInfo init_info = {};
    // init_info.Instance = context.instance.get();
    // init_info.PhysicalDevice = context.physical_device;
    // init_info.Device = context.device.get();
    // ...
    // ImGui_ImplVulkan_Init(&init_info, render_pass.get());
    auto rt_pipeline = createRtPipeline(context, swapchain.extent);
    auto scene = create_scene(context);

    uint32_t current_frame = 0;
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      vk_check((VkResult)context.device->waitForFences(
          1, &swapchain.in_flight[current_frame].get(), true,
          std::numeric_limits<uint64_t>::max()));
      vk_check((VkResult)context.device->resetFences(
          1, &swapchain.in_flight[current_frame].get()));

      auto next_image_index =
          context.device
              ->acquireNextImageKHR(
                  swapchain.handle.get(), std::numeric_limits<uint64_t>::max(),
                  swapchain.image_available[current_frame].get())
              .value;

      vk::UniqueCommandBuffer& buf = swapchain.command_buffers[current_frame];

      {
        buf->begin(vk::CommandBufferBeginInfo().setFlags(
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
        barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        barrier.image = swapchain.images[next_image_index];
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = {};

        buf->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                             vk::PipelineStageFlagBits::eBottomOfPipe, {}, {},
                             {}, barrier);

        buf->end();
      }

      vk::PipelineStageFlags wait_stage_mask =
          vk::PipelineStageFlagBits::eTopOfPipe;
      context.graphics_queue.submit(
          vk::SubmitInfo()
              .setWaitSemaphores(swapchain.image_available[current_frame].get())
              .setWaitDstStageMask(wait_stage_mask)
              .setCommandBuffers(buf.get())
              .setSignalSemaphores(
                  swapchain.render_finished[next_image_index].get()),
          swapchain.in_flight[current_frame].get());

      vk_check((VkResult)context.graphics_queue.presentKHR(
          vk::PresentInfoKHR()
              .setWaitSemaphores(
                  swapchain.render_finished[next_image_index].get())
              .setSwapchains(swapchain.handle.get())
              .setImageIndices(next_image_index)));

      current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
    }

    context.device->waitIdle();
  }

  // ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
