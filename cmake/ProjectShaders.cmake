include_guard(GLOBAL)

function(project_compile_shader source output)
    cmake_parse_arguments(PARSE_ARGV 2 SHADER "" "TARGET_ENV" "OPTIONS")
    if(NOT GLSLC_EXE)
        find_program(GLSLC_EXE NAMES glslc glslc.exe
            HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
            REQUIRED
        )
    endif()

    set(shader_compile_options)
    get_filename_component(shader_extension "${source}" LAST_EXT)
    if(SHADER_TARGET_ENV)
        list(APPEND shader_compile_options "--target-env=${SHADER_TARGET_ENV}")
    elseif(shader_extension MATCHES "^\\.(mesh|task)$")
        # Mesh and task shaders require SPIR-V 1.4 or later.
        list(APPEND shader_compile_options --target-env=vulkan1.2)
    endif()
    list(APPEND shader_compile_options ${SHADER_OPTIONS})

    get_filename_component(output_directory "${output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND "${GLSLC_EXE}" ${shader_compile_options} -MD -MF "${output}.d" "${source}" -o "${output}"
        DEPENDS "${source}"
        DEPFILE "${output}.d"
        COMMENT "Compiling shader ${source}"
        VERBATIM
    )
endfunction()
