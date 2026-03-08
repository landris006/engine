#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <utils.h>

#include "context.h"
#include "swapchain.h"

int main() {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    std::abort();
  }

  if (!glfwVulkanSupported()) {
    printf("GLFW: Vulkan Not Supported\n");
    std::abort();
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 800, "Hello", nullptr, nullptr);

  {
    auto context = createContext();
    auto swapchain = createSwapchain(context, window);
  }

  // Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
  // slang::createGlobalSession(slangGlobalSession.writePosition());

  // IMGUI_CHECKVERSION();
  // ImGui::CreateContext();
  // ImGui_ImplGlfw_InitForVulkan(window, true);
  //
  // ImGui_ImplVulkan_InitInfo init_info = {};
  // init_info.Instance = context.instance.get();
  // init_info.PhysicalDevice = context.physical_device;
  // init_info.Device = context.device.get();
  // ...
  // ImGui_ImplVulkan_Init(&init_info, render_pass.get());

  // while (!glfwWindowShouldClose(window)) {
  //   glfwPollEvents();
  // }

  // ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
