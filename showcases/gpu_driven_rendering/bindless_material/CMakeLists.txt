set(target_name gpu_bindless_material)
set(shader_target_name "${target_name}_shaders")
set(shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(shader_output_dir "${PROJECT_SHADER_DIR}/gpu_driven_rendering/bindless_material")
set(texture_count 4)

add_executable("${target_name}" main.cpp sample.h sample.cpp)
target_link_libraries("${target_name}" PRIVATE Framework::Framework)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Showcases")

project_compile_shader("${shader_dir}/material.vert" "${shader_output_dir}/material.vert.spv" TARGET_ENV vulkan1.3)
project_compile_shader(
    "${shader_dir}/material.frag" "${shader_output_dir}/material.frag.spv"
    TARGET_ENV vulkan1.3 OPTIONS "-DBINDLESS_TEXTURE_COUNT=${texture_count}"
)
add_custom_target("${shader_target_name}" DEPENDS
    "${shader_output_dir}/material.vert.spv"
    "${shader_output_dir}/material.frag.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Showcases/Shaders")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE
    "BINDLESS_TEXTURE_COUNT=${texture_count}"
    "PROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}/\""
    "PROJECT_SHADER_DIR=\"${PROJECT_SHADER_DIR}/\""
)
