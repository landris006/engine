#include "config.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "camera.h"
#include "context.h"
#include "ini.h"
#include "rt_pipeline.h"
#include "scene.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stbimage/stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
  uint32_t num_samples = 256;
  const char* model_path = "assets/cornell.obj";
  const char* skybox_path = "assets/skybox.hdr";
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
      model_path = argv[++i];
    else if (strcmp(argv[i], "--skybox") == 0 && i + 1 < argc)
      skybox_path = argv[++i];
    else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
      num_samples = (uint32_t)atoi(argv[++i]);
    else
      num_samples = (uint32_t)atoi(argv[i]);  // positional fallback
  }

  auto context = createContext();

  auto scene = create_scene(context, model_path);

  vk::Extent2D extent{WINDOW_WIDHT, WINDOW_HEIGHT};
  auto rt = create_rt_pipeline(context, extent, scene, skybox_path);

  auto ini = load_scene_ini();
  Camera cam;
  cam.aspect = (float)extent.width / (float)extent.height;
  cam.position = ini.camera_pos;
  cam.yaw = glm::radians(ini.camera_yaw_deg);
  cam.pitch = glm::radians(ini.camera_pitch_deg);
  cam.fov = ini.camera_vfov_deg;
  update_camera(context, rt, cam.create_ubo());

  printf("Rendering %u samples at %ux%u\n", num_samples, extent.width,
         extent.height);

  for (uint32_t s = 1; s <= num_samples; s++) {
    submit_one_time_command(context, [&](const vk::CommandBuffer& buf) {
      PushConstants pc{
          .sample_count = s,
          .max_bounces = 4,
          .light_count = scene.light_count,
          .dir_light = glm::vec3(0.554743, 0.741466, 0.377476),
          .time = 0.0f,
      };
      buf.pushConstants(rt.layout.get(), vk::ShaderStageFlagBits::eRaygenKHR, 0,
                        sizeof(pc), &pc);

      buf.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR,
                       rt.rt_pipeline.get());
      buf.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR,
                             rt.layout.get(), 0, rt.desc_set, {});

      buf.traceRaysKHR(rt.raygen_region, rt.miss_region, rt.hit_region,
                       rt.callable_region, extent.width, extent.height, 1);

      // storage image: GENERAL → GENERAL (ray write → compute read)
      auto storage_barrier =
          vk::ImageMemoryBarrier()
              .setOldLayout(vk::ImageLayout::eGeneral)
              .setNewLayout(vk::ImageLayout::eGeneral)
              .setImage(rt.storage_image.handle.get())
              .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
              .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
              .setSubresourceRange(
                  vk::ImageSubresourceRange()
                      .setAspectMask(vk::ImageAspectFlagBits::eColor)
                      .setBaseMipLevel(0)
                      .setLevelCount(1)
                      .setBaseArrayLayer(0)
                      .setLayerCount(1));
      buf.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                          vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
                          storage_barrier);

      buf.bindPipeline(vk::PipelineBindPoint::eCompute,
                       rt.tonemap_pipeline.get());
      buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, rt.layout.get(),
                             0, rt.desc_set, {});
      buf.dispatch((uint32_t)ceil(extent.width / 8.0f),
                   (uint32_t)ceil(extent.height / 8.0f), 1);

      // Reset storage image layout for next sample
      storage_barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
          .setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
      buf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eRayTracingShaderKHR, {},
                          {}, {}, storage_barrier);

      // display image: GENERAL → GENERAL (compute write → compute write)
      auto display_barrier =
          vk::ImageMemoryBarrier()
              .setOldLayout(vk::ImageLayout::eGeneral)
              .setNewLayout(vk::ImageLayout::eGeneral)
              .setImage(rt.display_image.handle.get())
              .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
              .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
              .setSubresourceRange(
                  vk::ImageSubresourceRange()
                      .setAspectMask(vk::ImageAspectFlagBits::eColor)
                      .setBaseMipLevel(0)
                      .setLevelCount(1)
                      .setBaseArrayLayer(0)
                      .setLayerCount(1));
      buf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
                          display_barrier);
    });

    int digits = snprintf(nullptr, 0, "%u", num_samples);
    printf("\r  Progress: %*u / %u", digits, s, num_samples);
    fflush(stdout);
  }

  printf("\nDone. Writing image...\n");

  vk::DeviceSize buf_size = extent.width * extent.height * 4 * sizeof(float);
  auto staging =
      createBuffer(context, buf_size, vk::BufferUsageFlagBits::eTransferDst,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);

  submit_one_time_command(context, [&](const vk::CommandBuffer& buf) {
    // display_image: GENERAL → TRANSFER_SRC
    auto barrier = vk::ImageMemoryBarrier()
                       .setOldLayout(vk::ImageLayout::eGeneral)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setImage(rt.display_image.handle.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                       .setSubresourceRange(
                           vk::ImageSubresourceRange()
                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                               .setBaseMipLevel(0)
                               .setLevelCount(1)
                               .setBaseArrayLayer(0)
                               .setLayerCount(1));
    buf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                        barrier);

    auto copy = vk::BufferImageCopy()
                    .setBufferOffset(0)
                    .setBufferRowLength(0)
                    .setBufferImageHeight(0)
                    .setImageSubresource(
                        vk::ImageSubresourceLayers()
                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                            .setMipLevel(0)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1))
                    .setImageOffset({0, 0, 0})
                    .setImageExtent({extent.width, extent.height, 1});
    buf.copyImageToBuffer(rt.display_image.handle.get(),
                          vk::ImageLayout::eTransferSrcOptimal,
                          staging.handle.get(), copy);
  });

  // Convert float RGBA → uint8 RGBA and write PNG
  const float* src =
      (float*)context.device->mapMemory(staging.memory.get(), 0, buf_size);
  uint32_t pixel_count = extent.width * extent.height;
  std::vector<uint8_t> pixels(pixel_count * 4);
  for (uint32_t i = 0; i < pixel_count; i++) {
    pixels[i * 4 + 0] =
        (uint8_t)(std::clamp(src[i * 4 + 0], 0.0f, 1.0f) * 255.0f + 0.5f);
    pixels[i * 4 + 1] =
        (uint8_t)(std::clamp(src[i * 4 + 1], 0.0f, 1.0f) * 255.0f + 0.5f);
    pixels[i * 4 + 2] =
        (uint8_t)(std::clamp(src[i * 4 + 2], 0.0f, 1.0f) * 255.0f + 0.5f);
    pixels[i * 4 + 3] = 255;
  }
  context.device->unmapMemory(staging.memory.get());

  const char* out_path = "output.png";
  stbi_write_png(out_path, (int)extent.width, (int)extent.height, 4,
                 pixels.data(), (int)(extent.width * 4));
  printf("Wrote %s\n", out_path);

  context.device->waitIdle();
  return 0;
}
