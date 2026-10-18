set(target_name gpu_multi_draw_indirect)
set(shader_target_name "${target_name}_shaders")
set(shader_output_dir "${PROJECT_SHADER_DIR}/gpu_driven_rendering/multi_draw_indirect")

add_executable("${target_name}"
    main.cpp
    sample.h
    sample.cpp
)
target_link_libraries("${target_name}" PRIVATE Framework::Framework)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Showcases")

project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/multi_draw_indirect.vert"
    "${shader_output_dir}/multi_draw_indirect_vert.spv"
    TARGET_ENV vulkan1.3
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/multi_draw_indirect.frag"
    "${shader_output_dir}/multi_draw_indirect_frag.spv"
    TARGET_ENV vulkan1.3
)
add_custom_target("${shader_target_name}" DEPENDS
    "${shader_output_dir}/multi_draw_indirect_vert.spv"
    "${shader_output_dir}/multi_draw_indirect_frag.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Showcases/Shaders")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE
    "PROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}/\""
    "PROJECT_SHADER_DIR=\"${shader_output_dir}\""
)
