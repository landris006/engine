#pragma once

#include <cstdint>
#include <vector>

#include "context.h"

static constexpr char ASSETS_DIR[] = "assets";

struct Mesh {
  AllocatedBuffer vertex_buffer;
  AllocatedBuffer index_buffer;
  vk::UniqueAccelerationStructureKHR blas_handle;
  AllocatedBuffer blas_buffer;
};

struct Scene {
  std::vector<Mesh> meshes;
  AllocatedBuffer mesh_info_buffer;
  AllocatedBuffer material_buffer;
  AllocatedBuffer light_triangle_buffer;
  uint32_t light_count = 0;

  AllocatedBuffer tlas_buffer;
  AllocatedBuffer tlas_instance_buffer;
  vk::UniqueAccelerationStructureKHR tlas_handle;

  std::vector<AllocatedImage> textures;
};

Scene create_scene(const Context& context, const char* model_path);
