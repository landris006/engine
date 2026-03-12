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

#include "camera.h"
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
  glfwWindowHintString(GLFW_WAYLAND_APP_ID, "super_engine");
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
    auto scene = create_scene(context);
    auto rt_pipeline = create_rt_pipeline(context, swapchain.extent, scene);

    uint32_t sample_count = 0;

    Camera cam;
    cam.aspect = (float)swapchain.extent.width / (float)swapchain.extent.height;
    cam.position = glm::vec3(0, 2, 0);
    cam.yaw = 0.0f;
    FpsCameraController controller;

    double last_time = glfwGetTime();

    uint32_t current_frame = 0;
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      double now = glfwGetTime();
      float dt = (float)(now - last_time);
      last_time = now;

      if (controller.update(cam, window, dt)) {
        sample_count = 0;
      }
      update_camera(context, rt_pipeline, cam.create_ubo());

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

        // push constants
        sample_count++;
        PushConstants pc{.sample_count = sample_count,
                         .max_bounces = 4,
                         .time = (float)glfwGetTime(),
                         .light_count = scene.light_count};
        buf->pushConstants(rt_pipeline.layout.get(),
                           vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(pc),
                           &pc);

        // bind pipeline + descriptors
        buf->bindPipeline(vk::PipelineBindPoint::eRayTracingKHR,
                          rt_pipeline.pipeline.get());
        buf->bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR,
                                rt_pipeline.layout.get(), 0,
                                rt_pipeline.desc_set, {});

        // trace rays
        buf->traceRaysKHR(rt_pipeline.raygen_region, rt_pipeline.miss_region,
                          rt_pipeline.hit_region, rt_pipeline.callable_region,
                          swapchain.extent.width, swapchain.extent.height, 1);

        // storage image: GENERAL → TRANSFER_SRC
        auto storage_barrier =
            vk::ImageMemoryBarrier()
                .setOldLayout(vk::ImageLayout::eGeneral)
                .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setImage(rt_pipeline.storage_image.handle.get())
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                             vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                             storage_barrier);

        // swapchain image: UNDEFINED → TRANSFER_DST
        auto swapchain_barrier =
            vk::ImageMemoryBarrier()
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                .setImage(swapchain.images[next_image_index])
                .setSrcAccessMask({})
                .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                             vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                             swapchain_barrier);

        // blit
        auto blit =
            vk::ImageBlit()
                .setSrcSubresource(
                    vk::ImageSubresourceLayers()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setMipLevel(0)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1))
                .setSrcOffsets({vk::Offset3D{0, 0, 0},
                                vk::Offset3D{(int)swapchain.extent.width,
                                             (int)swapchain.extent.height, 1}})
                .setDstSubresource(
                    vk::ImageSubresourceLayers()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setMipLevel(0)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1))
                .setDstOffsets({vk::Offset3D{0, 0, 0},
                                vk::Offset3D{(int)swapchain.extent.width,
                                             (int)swapchain.extent.height, 1}});
        buf->blitImage(rt_pipeline.storage_image.handle.get(),
                       vk::ImageLayout::eTransferSrcOptimal,
                       swapchain.images[next_image_index],
                       vk::ImageLayout::eTransferDstOptimal, blit,
                       vk::Filter::eLinear);

        // swapchain: TRANSFER_DST → PRESENT
        auto present_barrier =
            vk::ImageMemoryBarrier{}
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                .setImage(swapchain.images[next_image_index])
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask({})
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eBottomOfPipe, {}, {},
                             {}, present_barrier);

        // storage image back to GENERAL for next frame
        storage_barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                             {}, {}, {}, storage_barrier);

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
