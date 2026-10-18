set(target_name gpu_lod)
set(shader_target_name "${target_name}_shaders")
set(shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(shader_output_dir "${PROJECT_SHADER_DIR}/gpu_driven_rendering/lod")

add_executable("${target_name}" main.cpp sample.h sample.cpp)
target_link_libraries("${target_name}" PRIVATE Framework::Framework)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Showcases")

project_compile_shader("${shader_dir}/lod.vert" "${shader_output_dir}/lod.vert.spv" TARGET_ENV vulkan1.3)
project_compile_shader("${shader_dir}/lod.frag" "${shader_output_dir}/lod.frag.spv" TARGET_ENV vulkan1.3)
# 固定工作组大小使用 Vulkan 1.2 的 SPIR-V，避免要求额外的 maintenance4 特性。
project_compile_shader("${shader_dir}/lod.comp" "${shader_output_dir}/lod.comp.spv" TARGET_ENV vulkan1.2)
add_custom_target("${shader_target_name}" DEPENDS
    "${shader_output_dir}/lod.vert.spv"
    "${shader_output_dir}/lod.frag.spv"
    "${shader_output_dir}/lod.comp.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Showcases/Shaders")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE "PROJECT_SHADER_DIR=\"${shader_output_dir}\"")
