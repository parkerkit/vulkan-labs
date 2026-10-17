set(target_name framework)

add_library("${target_name}" STATIC
    include/framework/imgui_layer.h
    include/framework/input_state.h
    include/framework/window.h
    include/framework/orbit_camera.h
    include/framework/device.h
    include/framework/buffer.h
    include/framework/image.h
    include/framework/shader_module.h
    include/framework/pipeline.h
    include/framework/descriptors.h
    include/framework/synchronization.h
    include/framework/swapchain.h
    include/framework/frame_context.h
    include/framework/frame_scheduler.h
    include/framework/sample.h
    include/framework/application.h
    src/imgui_layer.cpp
    src/window.cpp
    src/orbit_camera.cpp
    src/vulkan_helpers.h
    src/vulkan_helpers.cpp
    src/device.cpp
    src/buffer.cpp
    src/image.cpp
    src/shader_module.cpp
    src/pipeline.cpp
    src/descriptors.cpp
    src/synchronization.cpp
    src/swapchain.cpp
    src/frame_scheduler.cpp
    src/application.cpp
)
add_library(Framework::Framework ALIAS "${target_name}")

target_compile_features("${target_name}" PUBLIC cxx_std_20)
target_compile_definitions("${target_name}" PRIVATE GLFW_INCLUDE_NONE)
target_include_directories("${target_name}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries("${target_name}"
    PUBLIC Vulkan::Vulkan ThirdParty::glm ThirdParty::imgui
    PRIVATE ThirdParty::glfw ThirdParty::vma
)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Framework")

if(BUILD_FRAMEWORK_EXAMPLES)
    add_subdirectory(example)
endif()
