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
  size_t data_offset;
  size_t data_size;
};

auto load_dds_texture(const Context& ctx, const void* file_data,
                      size_t file_size, bool srgb) -> AllocatedImage;
