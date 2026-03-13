#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <utils.h>
#include <vulkan/vulkan_core.h>

#include "scene.h"
#include "vulkan/vulkan.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <glob.h>
#include <sys/stat.h>
#include <tinyobjloader/tiny_obj_loader.h>

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "camera.h"
#include "config.h"
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
  GLFWwindow* window =
      glfwCreateWindow(WINDOW_WIDHT, WINDOW_HEIGHT, "Hello", nullptr, nullptr);

  {
    auto context = createContext();
    auto swapchain = createSwapchain(window, context, context.command_pool);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 20.0f;
    ImGui::GetIO().Fonts->AddFontDefault(&font_cfg);
    ImGui::GetStyle().ScaleAllSizes(1.5f);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    auto swapchain_format = swapchain.format;
    auto pipeline_rendering_info =
        vk::PipelineRenderingCreateInfo()
            .setColorAttachmentCount(1)
            .setPColorAttachmentFormats(&swapchain_format);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_4;
    init_info.Instance = context.instance.get();
    init_info.PhysicalDevice = context.physical_device;
    init_info.Device = context.device.get();
    init_info.QueueFamily = context.graphics_queue_idx;
    init_info.Queue = context.graphics_queue;
    init_info.DescriptorPoolSize = 1000;
    init_info.MinImageCount = 2;
    init_info.ImageCount = (uint32_t)swapchain.images.size();
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo =
        pipeline_rendering_info;
    ImGui_ImplVulkan_Init(&init_info);

    auto scene = create_scene(context);
    auto rt_pipeline = create_rt_pipeline(context, swapchain.extent, scene);

    // Shader hot reload — track mtimes of all .slang files
    auto latest_mtime = []() {
      time_t t = 0;
      glob_t g;
      if (glob("shaders/*.slang", 0, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
          struct stat st;
          if (stat(g.gl_pathv[i], &st) == 0) {
            t = std::max(t, st.st_mtime);
          }
        }
        globfree(&g);
      }
      return t;
    };
    time_t last_shader_mtime = latest_mtime();

    uint32_t sample_count = 0;

    Camera cam;
    cam.aspect = (float)swapchain.extent.width / (float)swapchain.extent.height;
    cam.position = glm::vec3(0, 2, 10);
    cam.yaw = glm::radians(-90.0f);
    FpsCameraController controller;

    double last_time = glfwGetTime();

    uint32_t current_frame = 0;
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
      ImGui::NewFrame();

      // --- ImGui windows go here ---
      ImGui::Begin("Debug");
      ImGui::Text("Samples: %u", sample_count);
      ImGui::End();

      ImGui::Render();

      time_t current_mtime = latest_mtime();
      if (current_mtime != last_shader_mtime) {
        last_shader_mtime = current_mtime;
        context.device->waitIdle();
        if (reload_pipeline(context, rt_pipeline)) {
          sample_count = 0;
        }
      }

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
                          rt_pipeline.rt_pipeline.get());
        buf->bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR,
                                rt_pipeline.layout.get(), 0,
                                rt_pipeline.desc_set, {});

        // trace rays
        buf->traceRaysKHR(rt_pipeline.raygen_region, rt_pipeline.miss_region,
                          rt_pipeline.hit_region, rt_pipeline.callable_region,
                          swapchain.extent.width, swapchain.extent.height, 1);

        // storage image: GENERAL → GENERAL
        auto storage_barrier =
            vk::ImageMemoryBarrier()
                .setOldLayout(vk::ImageLayout::eGeneral)
                .setNewLayout(vk::ImageLayout::eGeneral)
                .setImage(rt_pipeline.storage_image.handle.get())
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                             vk::PipelineStageFlagBits::eComputeShader, {}, {},
                             {}, storage_barrier);

        buf->bindPipeline(vk::PipelineBindPoint::eCompute,
                          rt_pipeline.tonemap_pipeline.get());
        buf->bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                rt_pipeline.layout.get(), 0,
                                rt_pipeline.desc_set, {});
        buf->dispatch(ceil(swapchain.extent.width / 8.0f),
                      ceil(swapchain.extent.height / 8.0f), 1);

        // display image: GENERAL → TRANSFER_SRC
        auto display_barrier =
            vk::ImageMemoryBarrier()
                .setOldLayout(vk::ImageLayout::eGeneral)
                .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setImage(rt_pipeline.display_image.handle.get())
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                             vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                             display_barrier);

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
        buf->blitImage(rt_pipeline.display_image.handle.get(),
                       vk::ImageLayout::eTransferSrcOptimal,
                       swapchain.images[next_image_index],
                       vk::ImageLayout::eTransferDstOptimal, blit,
                       vk::Filter::eLinear);

        // swapchain: TRANSFER_DST → COLOR_ATTACHMENT (for ImGui)
        auto imgui_barrier =
            vk::ImageMemoryBarrier{}
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setImage(swapchain.images[next_image_index])
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eColorAttachmentOutput,
                             {}, {}, {}, imgui_barrier);

        // ImGui dynamic rendering
        auto color_attachment =
            vk::RenderingAttachmentInfo()
                .setImageView(swapchain.image_views[next_image_index].get())
                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setLoadOp(vk::AttachmentLoadOp::eLoad)
                .setStoreOp(vk::AttachmentStoreOp::eStore);
        auto rendering_info =
            vk::RenderingInfo()
                .setRenderArea(vk::Rect2D({0, 0}, swapchain.extent))
                .setLayerCount(1)
                .setColorAttachments(color_attachment);
        buf->beginRendering(rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), buf.get());
        buf->endRendering();

        // swapchain: COLOR_ATTACHMENT → PRESENT
        auto present_barrier =
            vk::ImageMemoryBarrier{}
                .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                .setImage(swapchain.images[next_image_index])
                .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                .setDstAccessMask({})
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eColor)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1));
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                             vk::PipelineStageFlagBits::eBottomOfPipe, {}, {},
                             {}, present_barrier);

        // storage image back to GENERAL for next frame
        storage_barrier.setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                             vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                             {}, {}, {}, storage_barrier);

        display_barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
        buf->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eComputeShader, {}, {},
                             {}, display_barrier);

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
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
