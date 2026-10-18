set(target_name gpu_prefix_sum_compaction)
set(shader_target_name "${target_name}_shaders")
set(shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(shader_output_dir "${PROJECT_SHADER_DIR}/gpu_driven_rendering/prefix_sum_compaction")

add_executable("${target_name}" main.cpp sample.h sample.cpp)
target_link_libraries("${target_name}" PRIVATE Framework::Framework)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Showcases")

set(shader_outputs)
foreach(shader IN ITEMS scene.vert scene.frag frustum_debug.vert frustum_debug.frag)
    set(output "${shader_output_dir}/${shader}.spv")
    project_compile_shader("${shader_dir}/${shader}" "${output}" TARGET_ENV vulkan1.3)
    list(APPEND shader_outputs "${output}")
endforeach()

# 固定工作组大小使用 Vulkan 1.2 的 SPIR-V，避免引入框架尚未启用的 maintenance4 特性。
foreach(shader IN ITEMS scan_block_totals.comp scatter_commands.comp)
    set(output "${shader_output_dir}/${shader}.spv")
    project_compile_shader("${shader_dir}/${shader}" "${output}" TARGET_ENV vulkan1.2)
    list(APPEND shader_outputs "${output}")
endforeach()
project_compile_shader(
    "${shader_dir}/classify.comp"
    "${shader_output_dir}/atomic_compact.spv"
    TARGET_ENV vulkan1.2
)
project_compile_shader(
    "${shader_dir}/classify.comp"
    "${shader_output_dir}/scan_blocks.spv"
    TARGET_ENV vulkan1.2
    OPTIONS -DPREFIX_SUM_COMPACTION
)
add_custom_target("${shader_target_name}" DEPENDS
    ${shader_outputs}
    "${shader_output_dir}/atomic_compact.spv"
    "${shader_output_dir}/scan_blocks.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Showcases/Shaders")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE
    "PROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}/\""
    "PROJECT_SHADER_DIR=\"${shader_output_dir}\""
)
