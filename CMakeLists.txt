cmake_minimum_required(VERSION 3.20)
project(engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

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

# Main Executable
add_executable(engine src/main.cpp)

target_include_directories(engine PRIVATE src ${SLANG_INCLUDE_DIR})

target_link_libraries(engine PRIVATE
    Vulkan::Vulkan
    glfw
    imgui
    ${SLANG_LIB}
)
