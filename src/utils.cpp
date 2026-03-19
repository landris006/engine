#include "utils.h"

void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

auto vk_check(VkResult err) -> void {
  if (err == VK_SUCCESS) {
    return;
  }
  fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
  if (err < 0) {
    std::abort();
  }
}

auto load_texture(const Context& ctx, const void* pixels, int w, int h,
                  bool srgb) -> AllocatedImage {
  vk::DeviceSize size = (vk::DeviceSize)(w * h * 4 * sizeof(uint8_t));
  auto staging = createBuffer(ctx, size, vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent);

  uploadToBuffer(ctx, staging, pixels, size);

  auto fmt = srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
  auto img = createImage(
      ctx, vk::Extent3D{(uint32_t)w, (uint32_t)h, 1}, fmt,
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);

  submit_one_time_command(ctx, [&](const vk::CommandBuffer& cmd) {
    auto subresource_range = vk::ImageSubresourceRange()
                                 .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                 .setBaseMipLevel(0)
                                 .setLevelCount(1)
                                 .setBaseArrayLayer(0)
                                 .setLayerCount(1);

    auto barrier = vk::ImageMemoryBarrier()
                       .setOldLayout(vk::ImageLayout::eUndefined)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                       .setImage(img.handle.get())
                       .setSubresourceRange(subresource_range)
                       .setSrcAccessMask({})
                       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
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
                    .setImageExtent({(uint32_t)w, (uint32_t)h, 1});
    cmd.copyBufferToImage(staging.handle.get(), img.handle.get(),
                          vk::ImageLayout::eTransferDstOptimal, copy);

    barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eRayTracingShaderKHR, {}, {},
                        {}, barrier);
  });

  return img;
}
