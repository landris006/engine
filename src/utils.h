#pragma once
#include <vulkan/vulkan_core.h>

#include <glm/ext/quaternion_geometric.hpp>
#include <glm/fwd.hpp>

#include "context.h"

void glfw_error_callback(int error, const char* description);
auto vk_check(VkResult err) -> void;
auto load_texture(const Context& ctx, const void* pixels, int w, int h,
                  bool srgb = false) -> AllocatedImage;

struct Controls {
  // azimuth in degrees
  float light_ray_azimuth = 0.0f;
  // elevation in degrees
  float light_ray_elevation = 0.0f;
  glm::vec3 light_ray_color = {1.0f, 1.0f, 1.0f};
  float light_ray_intensity = 1.0f;
};

auto get_light_ray(float azimuth, float elevation) -> glm::vec3;
