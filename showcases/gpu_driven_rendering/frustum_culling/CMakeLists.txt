set(target_name gpu_frustum_culling)
set(shader_target_name "${target_name}_shaders")

add_executable("${target_name}"
    main.cpp
    sample.h
    sample.cpp
)
target_link_libraries("${target_name}" PRIVATE Framework::Framework)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Showcases")

project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/frustum_culling.vert"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_culling_vert.spv"
    TARGET_ENV vulkan1.3
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/frustum_culling.frag"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_culling_frag.spv"
    TARGET_ENV vulkan1.3
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/frustum_culling.comp"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_culling_comp.spv"
    # 固定工作组大小使用 Vulkan 1.2 的 SPIR-V，避免引入框架尚未启用的 maintenance4 特性。
    TARGET_ENV vulkan1.2
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/frustum_debug.vert"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_debug_vert.spv"
    TARGET_ENV vulkan1.3
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/frustum_debug.frag"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_debug_frag.spv"
    TARGET_ENV vulkan1.3
)
add_custom_target("${shader_target_name}" DEPENDS
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_culling_vert.spv"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_culling_frag.spv"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_culling_comp.spv"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_debug_vert.spv"
    "${PROJECT_SHADER_DIR}/gpu_driven_rendering/frustum_debug_frag.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Showcases/Shaders")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE
    "PROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}/\""
    "PROJECT_SHADER_DIR=\"${PROJECT_SHADER_DIR}/\""
)
