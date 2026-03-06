#pragma once
#include <vulkan/vulkan_core.h>

#include <cstdio>
#include <cstdlib>

static void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void vk_check(VkResult err) {
  if (err == VK_SUCCESS) {
    return;
  }
  fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
  if (err < 0) {
    std::abort();
  }
}
