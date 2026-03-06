#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <utils.h>

#include <vulkan/vulkan.hpp>

int main() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 800, "Vulkan.hpp + Slang + ImGui",
                                        nullptr, nullptr);

  vk::ApplicationInfo appInfo("Vulkan.hpp Engine", 1, "No Engine", 1,
                              VK_API_VERSION_1_3);

  vk::InstanceCreateInfo createInfo({}, &appInfo);

  vk::UniqueInstance instance = vk::createInstanceUnique(createInfo);

  vk::UniqueDevice device =
      instance->enumeratePhysicalDevices().front().createDeviceUnique(
          vk::DeviceCreateInfo());

  // Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
  // slang::createGlobalSession(slangGlobalSession.writePosition());

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplGlfw_InitForVulkan(window, true);

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = instance.get();
  // init_info.PhysicalDevice = physicalDevice;
  // init_info.Device = device.get();
  // ...
  // ImGui_ImplVulkan_Init(&init_info, render_pass.get());

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
  }

  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
