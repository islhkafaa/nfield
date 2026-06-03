cmake_minimum_required(VERSION 3.25)
project(nfield VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

include(FetchContent)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

FetchContent_Declare(
    imgui
    URL            https://github.com/ocornut/imgui/archive/refs/heads/docking.zip
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

FetchContent_Declare(
    stb
    URL            https://github.com/nothings/stb/archive/refs/heads/master.zip
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE third_party/stb)

find_program(VULKAN_GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/bin")
if(NOT VULKAN_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc shader compiler not found! Make sure Vulkan SDK is installed.")
endif()

function(compile_shader input_file output_spv_var)
    get_filename_component(name ${input_file} NAME)
    set(spv_file "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv")
    add_custom_command(
        OUTPUT ${spv_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND ${VULKAN_GLSLC_EXECUTABLE} ${input_file} -o ${spv_file}
        DEPENDS ${input_file}
        COMMENT "Compiling shader ${name} to SPIR-V"
    )
    set(${output_spv_var} ${${output_spv_var}} ${spv_file} PARENT_SCOPE)
endfunction()

set(SPV_SHADERS "")
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/particle.vert" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/particle.frag" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/integrate_direct.comp" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/integrate_bh.comp" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/fullscreen.vert" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/trail_decay.frag" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_threshold.comp" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_blur.comp" SPV_SHADERS)
compile_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_composite.frag" SPV_SHADERS)

add_executable(nfield
    src/main.cpp
    src/core/App.cpp
    src/core/Preset.cpp
    src/vulkan/VulkanContext.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Pipeline.cpp
    include/core/App.hpp
    include/core/Preset.hpp
    include/core/SimState.hpp
    include/vulkan/VulkanContext.hpp
    include/vulkan/Buffer.hpp
    include/vulkan/Pipeline.hpp
    include/core/Camera.hpp
    ${SPV_SHADERS}
)

target_include_directories(nfield PRIVATE include)
target_compile_features(nfield PRIVATE cxx_std_20)

include(cmake/CompilerOptions.cmake)
target_apply_warnings(nfield)

target_link_libraries(nfield PRIVATE
    glfw
    glm::glm
    imgui
    stb
    Vulkan::Vulkan
)

if(WIN32)
    target_link_libraries(nfield PRIVATE dwmapi)

    get_filename_component(COMPILER_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    set(RUNTIME_DLLS "libstdc++-6.dll" "libgcc_s_seh-1.dll" "libgcc_s_ucrt-1.dll" "libwinpthread-1.dll")
    foreach(dll ${RUNTIME_DLLS})
        if(EXISTS "${COMPILER_DIR}/${dll}")
            add_custom_command(TARGET nfield POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${COMPILER_DIR}/${dll}"
                    "${CMAKE_CURRENT_BINARY_DIR}"
            )
        endif()
    endforeach()
endif()
