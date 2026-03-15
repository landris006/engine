#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vulkan/vulkan.hpp>

#include "context.h"

#pragma pack(push, 1)
struct DDSPixelFormat {
  uint32_t dwSize, dwFlags, dwFourCC, dwRGBBitCount;
  uint32_t dwRBitMask, dwGBitMask, dwBBitMask, dwABitMask;
};
struct DDSHeader {
  uint32_t dwSize, dwFlags, dwHeight, dwWidth;
  uint32_t dwPitchOrLinearSize, dwDepth, dwMipMapCount;
  uint32_t dwReserved1[11];
  DDSPixelFormat ddspf;
  uint32_t dwCaps, dwCaps2, dwCaps3, dwCaps4, dwReserved2;
};
struct DDSHeaderDX10 {
  uint32_t dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2;
};
#pragma pack(pop)

static constexpr uint32_t DDS_MAGIC = 0x20534444;
static constexpr uint32_t DDPF_FOURCC = 0x4;
static constexpr uint32_t FOURCC_DXT1 = 0x31545844;
static constexpr uint32_t FOURCC_DXT5 = 0x35545844;
static constexpr uint32_t FOURCC_ATI2 = 0x32495441;
static constexpr uint32_t FOURCC_BC5U = 0x55354342;
static constexpr uint32_t FOURCC_DX10 = 0x30315844;
static constexpr uint32_t DXGI_BC7_UNORM = 98;
static constexpr uint32_t DXGI_BC7_UNORM_SRGB = 99;

struct DdsInfo {
  uint32_t width, height;
  vk::Format vk_format;
  size_t data_offset;  // byte offset of mip 0 from file start
  size_t data_size;    // bytes for mip 0
};

static bool parse_dds_header(const void* file_data, size_t file_size, bool srgb,
                             DdsInfo* out) {
  const uint8_t* p = static_cast<const uint8_t*>(file_data);
  if (file_size < 4 + sizeof(DDSHeader)) return false;
  uint32_t magic;
  memcpy(&magic, p, 4);
  if (magic != DDS_MAGIC) return false;
  p += 4;

  DDSHeader hdr;
  memcpy(&hdr, p, sizeof(DDSHeader));
  p += sizeof(DDSHeader);
  if (hdr.dwSize != 124) return false;
  if (!(hdr.ddspf.dwFlags & DDPF_FOURCC)) return false;

  uint32_t fourcc = hdr.ddspf.dwFourCC;
  vk::Format fmt = vk::Format::eUndefined;
  uint32_t block_bytes = 16;

  if (fourcc == FOURCC_DXT1) {
    fmt = srgb ? vk::Format::eBc1RgbaSrgbBlock : vk::Format::eBc1RgbaUnormBlock;
    block_bytes = 8;
  } else if (fourcc == FOURCC_DXT5) {
    fmt = srgb ? vk::Format::eBc3SrgbBlock : vk::Format::eBc3UnormBlock;
  } else if (fourcc == FOURCC_ATI2 || fourcc == FOURCC_BC5U) {
    fmt = vk::Format::eBc5UnormBlock;  // BC5 has no sRGB variant
  } else if (fourcc == FOURCC_DX10) {
    if (file_size < 4 + sizeof(DDSHeader) + sizeof(DDSHeaderDX10)) return false;
    DDSHeaderDX10 dx10;
    memcpy(&dx10, p, sizeof(DDSHeaderDX10));
    p += sizeof(DDSHeaderDX10);
    if (dx10.dxgiFormat == DXGI_BC7_UNORM)
      fmt = srgb ? vk::Format::eBc7SrgbBlock : vk::Format::eBc7UnormBlock;
    else if (dx10.dxgiFormat == DXGI_BC7_UNORM_SRGB)
      fmt = vk::Format::eBc7SrgbBlock;
    else
      return false;
  } else {
    return false;
  }

  uint32_t w = hdr.dwWidth, h = hdr.dwHeight;
  size_t data_size = ((w + 3) / 4) * ((h + 3) / 4) * block_bytes;
  size_t data_offset =
      static_cast<size_t>(p - static_cast<const uint8_t*>(file_data));
  if (file_size < data_offset + data_size) return false;

  *out = {w, h, fmt, data_offset, data_size};
  return true;
}

static auto load_dds_texture(const Context& ctx, const void* file_data,
                             size_t file_size, bool srgb) -> AllocatedImage {
  DdsInfo info;
  if (!parse_dds_header(file_data, file_size, srgb, &info)) {
    fprintf(stderr, "load_dds_texture: failed to parse DDS header\n");
    std::exit(1);
  }
  const void* pixels =
      static_cast<const uint8_t*>(file_data) + info.data_offset;

  auto staging =
      createBuffer(ctx, info.data_size, vk::BufferUsageFlagBits::eTransferSrc,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  uploadToBuffer(ctx, staging, pixels, info.data_size);

  auto img = createImage(
      ctx, vk::Extent3D{info.width, info.height, 1}, info.vk_format,
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
                    .setImageExtent({info.width, info.height, 1});
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
