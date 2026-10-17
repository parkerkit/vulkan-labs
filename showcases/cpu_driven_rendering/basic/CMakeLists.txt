set(target_name cpu_basic)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/base.vert"
    "${PROJECT_SHADER_DIR}/cpu_driven_rendering/base_vert.spv"
    TARGET_ENV vulkan1.3
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/base.frag"
    "${PROJECT_SHADER_DIR}/cpu_driven_rendering/base_frag.spv"
    TARGET_ENV vulkan1.3
)
add_custom_target("${shader_target_name}" DEPENDS
    "${PROJECT_SHADER_DIR}/cpu_driven_rendering/base_vert.spv"
    "${PROJECT_SHADER_DIR}/cpu_driven_rendering/base_frag.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Showcases/Shaders")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE
    "PROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}/\""
    "PROJECT_SHADER_DIR=\"${PROJECT_SHADER_DIR}/\""
)
