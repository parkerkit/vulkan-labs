include_guard(GLOBAL)

function(project_compile_shader source output)
    if(NOT GLSLC_EXE)
        find_program(GLSLC_EXE NAMES glslc glslc.exe
            HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
            REQUIRED
        )
    endif()

    get_filename_component(output_directory "${output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND "${GLSLC_EXE}" -MD -MF "${output}.d" "${source}" -o "${output}"
        DEPENDS "${source}"
        DEPFILE "${output}.d"
        COMMENT "Compiling shader ${source}"
        VERBATIM
    )
endfunction()
