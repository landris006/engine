#pragma once

#include "context.h"

struct RtPipeline {
  AllocatedImage image;
};

static auto createRtPipeline(const Context& context, const vk::Extent2D extent)
    -> RtPipeline {
  auto image = createImage(
      context, vk::Extent3D(extent.width, extent.height, 1),
      vk::Format::eR32G32B32A32Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc);

  submitOneTimeCommand(context, [&](const vk::CommandBuffer& buf) {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image = image.handle.get();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = {};

    buf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {},
                        barrier);
  });

  return RtPipeline{
      .image = std::move(image),
  };
}
