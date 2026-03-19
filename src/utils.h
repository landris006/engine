#pragma once
#include <vulkan/vulkan_core.h>

#include "context.h"

void glfw_error_callback(int error, const char* description);
auto vk_check(VkResult err) -> void;
auto load_texture(const Context& ctx, const void* pixels, int w, int h,
                  bool srgb = false) -> AllocatedImage;
