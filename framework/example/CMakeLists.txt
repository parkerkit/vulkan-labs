set(target_name framework_example)
set(shader_target_name "${target_name}_shaders")

add_executable("${target_name}"
    main.cpp
    cube_sample.h
    cube_sample.cpp
)
target_link_libraries("${target_name}" PRIVATE Framework::Framework)
project_setup_target("${target_name}")
project_setup_ide("${target_name}" FOLDER "Framework")

project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/framework_example.vert"
    "${PROJECT_SHADER_DIR}/framework_example_vert.spv"
    TARGET_ENV vulkan1.3
)
project_compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/framework_example.frag"
    "${PROJECT_SHADER_DIR}/framework_example_frag.spv"
    TARGET_ENV vulkan1.3
)
add_custom_target("${shader_target_name}" DEPENDS
    "${PROJECT_SHADER_DIR}/framework_example_vert.spv"
    "${PROJECT_SHADER_DIR}/framework_example_frag.spv"
)
project_setup_ide("${shader_target_name}" FOLDER "Framework")
add_dependencies("${target_name}" "${shader_target_name}")
target_compile_definitions("${target_name}" PRIVATE
    "PROJECT_SHADER_DIR=\"${PROJECT_SHADER_DIR}/\""
)
