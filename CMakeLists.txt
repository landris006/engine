cmake_minimum_required(VERSION 3.20)
project(engine)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

find_package(glm REQUIRED)

find_library(SLANG_LIB slang)
find_path(SLANG_INCLUDE_DIR slang.h PATH_SUFFIXES slang)

# ImGui
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PRIVATE Vulkan::Vulkan glfw)

add_subdirectory(external/fastgltf)

set(SHARED_SOURCES
    src/stb_impl.cpp
    src/context.cpp
    src/utils.cpp
    src/dds.cpp
    src/ini.cpp
    src/scene.cpp
    src/rt_pipeline.cpp
)

# Main Executable
add_executable(engine
    src/main.cpp
    src/swapchain.cpp
    ${SHARED_SOURCES}
)

target_include_directories(engine PRIVATE src external ${SLANG_INCLUDE_DIR})

target_link_libraries(engine PRIVATE
    Vulkan::Vulkan
    fastgltf::fastgltf
    glfw
    imgui
    glm::glm
    ${SLANG_LIB}
)

target_compile_options(engine PRIVATE -Wno-missing-field-initializers -Wunused-parameter)

target_compile_definitions(engine PRIVATE
  $<$<CONFIG:Debug>:DEBUG>
  VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

target_precompile_headers(engine PRIVATE
    <vulkan/vulkan.hpp>
    <glm/glm.hpp>
    <fastgltf/core.hpp>
)

# Headless renderer
add_executable(engine_headless
    src/headless.cpp
    ${SHARED_SOURCES}
)

target_include_directories(engine_headless PRIVATE src external ${SLANG_INCLUDE_DIR})

target_link_libraries(engine_headless PRIVATE
    Vulkan::Vulkan
    fastgltf::fastgltf
    glm::glm
    ${SLANG_LIB}
)

target_compile_options(engine_headless PRIVATE -Wno-missing-field-initializers -Wunused-parameter)

target_compile_definitions(engine_headless PRIVATE
    HEADLESS
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    $<$<CONFIG:Debug>:DEBUG>
)

target_precompile_headers(engine_headless PRIVATE
    <vulkan/vulkan.hpp>
    <glm/glm.hpp>
    <fastgltf/core.hpp>
)
