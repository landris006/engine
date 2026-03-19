#include <glob.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <sys/stat.h>
#include <utils.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstdint>
#include <glm/detail/qualifier.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan.hpp>

#include "camera.h"
#include "config.h"
#include "context.h"
#include "ini.h"
#include "rt_pipeline.h"
#include "scene.h"
#include "swapchain.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

int main(int argc, char** argv) {
  const char* model_path = "assets/cornell.obj";
  const char* skybox_path = "assets/skybox.hdr";
  for (int i = 1; i < argc - 1; i++) {
    if (strcmp(argv[i], "--model") == 0)
      model_path = argv[i + 1];
    else if (strcmp(argv[i], "--skybox") == 0)
      skybox_path = argv[i + 1];
  }

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

    auto scene = create_scene(context, model_path);

    // auto scene = load_obj_scene(context, model_path);
    auto rt_pipeline =
        create_rt_pipeline(context, swapchain.extent, scene, skybox_path);

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

    auto ini = load_scene_ini();
    Camera cam{
        .position = ini.camera_pos,
        .yaw = glm::radians(ini.camera_yaw_deg),
        .pitch = glm::radians(ini.camera_pitch_deg),
        .fov = ini.camera_vfov_deg,
        .aspect =
            (float)swapchain.extent.width / (float)swapchain.extent.height,
    };
    FpsCameraController controller;

    double last_time = glfwGetTime();

    static constexpr int FRAME_TIME_HISTORY = 128;
    float frame_times[FRAME_TIME_HISTORY] = {};
    int frame_time_idx = 0;
    float avg_all = 0.0f;
    uint32_t avg_all_count = 0;

    Controls controls;

    PushConstants pc = {
        .sample_count = 0,
        .max_bounces = 4,
        .light_count = scene.light_count,
        .dir_light = glm::vec3(0.554743, 0.741466, 0.377476),
        .time = (float)glfwGetTime(),
    };

    uint32_t current_frame = 0;
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      double now = glfwGetTime();
      float dt = (float)(now - last_time);
      last_time = now;
      float dt_ms = dt * 1000.0f;
      frame_times[frame_time_idx] = dt_ms;
      frame_time_idx = (frame_time_idx + 1) % FRAME_TIME_HISTORY;
      avg_all_count = std::min(avg_all_count + 1, 500u);
      avg_all += (dt_ms - avg_all) / (float)avg_all_count;

      pc.time = (float)now;

      float avg10 = 0.0f;
      for (int i = 1; i <= 10; i++)
        avg10 += frame_times[(frame_time_idx - i + FRAME_TIME_HISTORY) %
                             FRAME_TIME_HISTORY];
      avg10 /= 10.0f;

      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
      ImGui::NewFrame();

      // --- ImGui windows go here ---
      ImGui::Begin("Debug");
      ImGui::Text("Samples: %u", pc.sample_count);
      ImGui::Text("Frame:   %6.2f ms  %5.0f fps", avg10, 1000.0f / avg10);
      ImGui::Text("Avg all: %6.2f ms  %5.0f fps", avg_all, 1000.0f / avg_all);
      ImGui::PlotLines("##frametimes", frame_times, FRAME_TIME_HISTORY,
                       frame_time_idx, nullptr, 0.0f, 50.0f, ImVec2(0, 60));

      ImGui::Text("Camera");
      auto camera_controls = {
          ImGui::InputFloat3("Position", glm::value_ptr(cam.position)),
          ImGui::SliderFloat("Yaw", &cam.yaw, 0.0f, 2 * glm::pi<float>()),
          ImGui::SliderFloat("Pitch", &cam.pitch, -glm::half_pi<float>() + 0.1f,
                             glm::half_pi<float>() - 0.1f),
          ImGui::SliderFloat("FoV", &cam.fov, 0.0f, 180.0f),
      };
      if (std::ranges::any_of(camera_controls, std::identity())) {
        pc.sample_count = 0;
      }

      ImGui::Text("Light Ray");
      auto light_controls = {
          ImGui::SliderFloat("Azimuth", &controls.light_ray_azimuth, 0.0f,
                             360.0),
          ImGui::SliderFloat("Elevation", &controls.light_ray_elevation, 0.0,
                             90.0),
          ImGui::ColorEdit3("Color", glm::value_ptr(controls.light_ray_color)),
          ImGui::SliderFloat("Intensity", &controls.light_ray_intensity, 0.0f,
                             10.0f),
      };
      if (std::ranges::any_of(light_controls, std::identity())) {
        pc.sample_count = 0;
      }

      pc.dir_light = get_light_ray(glm::radians(controls.light_ray_azimuth),
                                   glm::radians(controls.light_ray_elevation));
      pc.dir_light_radiance =
          controls.light_ray_intensity * controls.light_ray_color;

      ImGui::End();

      ImGui::Render();

      time_t current_mtime = latest_mtime();
      if (current_mtime != last_shader_mtime) {
        last_shader_mtime = current_mtime;
        context.device->waitIdle();
        if (reload_pipeline(context, rt_pipeline)) {
          pc.sample_count = 0;
        }
      }

      if (controller.update(cam, window, dt)) {
        pc.sample_count = 0;
      }

      static bool ctrl_s_prev = false;
      bool ctrl_s_now =
          (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
           glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) &&
          glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
      if (ctrl_s_now && !ctrl_s_prev) {
        SceneConfig cfg;
        cfg.camera_pos = cam.position;
        cfg.camera_yaw_deg = glm::degrees(cam.yaw);
        cfg.camera_pitch_deg = glm::degrees(cam.pitch);
        cfg.camera_vfov_deg = cam.fov;
        if (save_scene_ini(cfg)) {
          printf("Scene config saved\n");
        };
      }
      ctrl_s_prev = ctrl_s_now;

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
        pc.sample_count++;
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
