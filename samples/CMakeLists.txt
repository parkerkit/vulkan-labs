function(samples_add_group group)
    get_filename_component(group_name "${group}" NAME)
    if(NOT group_name MATCHES "^([0-9]+)_")
        return()
    endif()
    set(group_number "${CMAKE_MATCH_1}")

    file(GLOB sample_directories CONFIGURE_DEPENDS LIST_DIRECTORIES true "${group}/*")
    foreach(sample_directory IN LISTS sample_directories)
        if(NOT IS_DIRECTORY "${sample_directory}")
            continue()
        endif()

        get_filename_component(sample_name "${sample_directory}" NAME)
        file(GLOB_RECURSE sample_sources CONFIGURE_DEPENDS
            "${sample_directory}/*.cpp"
        )
        file(GLOB_RECURSE sample_headers CONFIGURE_DEPENDS
            "${sample_directory}/*.h"
            "${sample_directory}/*.hpp"
        )
        if(NOT sample_sources)
            continue()
        endif()

        set(sample_entries)
        set(sample_common_sources)
        foreach(source IN LISTS sample_sources)
            get_filename_component(source_name "${source}" NAME)
            if(source_name MATCHES "^main([0-9]*)\\.cpp$")
                list(APPEND sample_entries "${source}")
            else()
                list(APPEND sample_common_sources "${source}")
            endif()
        endforeach()
        if(NOT sample_entries)
            continue()
        endif()

        set(base_target_name "${group_number}_${sample_name}")

        set(shader_outputs)
        if(sample_name MATCHES "^([0-9]+)_")
            set(shader_prefix "${group_number}_${CMAKE_MATCH_1}_")
            file(GLOB sample_shaders CONFIGURE_DEPENDS
                "${sample_directory}/shaders/*.vert"
                "${sample_directory}/shaders/*.frag"
                "${sample_directory}/shaders/*.comp"
                "${sample_directory}/shaders/*.geom"
                "${sample_directory}/shaders/*.mesh"
                "${sample_directory}/shaders/*.task"
            )
            foreach(shader IN LISTS sample_shaders)
                get_filename_component(shader_name "${shader}" NAME)
                string(REPLACE "." "_" shader_name "${shader_name}")
                set(shader_output "${PROJECT_SHADER_DIR}/${shader_prefix}${shader_name}.spv")
                project_compile_shader("${shader}" "${shader_output}")
                list(APPEND shader_outputs "${shader_output}")
            endforeach()
        endif()

        # Some samples also load shaders generated from a different sample.
        foreach(source IN LISTS sample_sources)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
            file(READ "${source}" source_content)
            string(REGEX MATCHALL "PROJECT_SHADER_DIR[ \t\r\n]*\"[^\"]+\\.spv\""
                shader_references "${source_content}"
            )
            foreach(reference IN LISTS shader_references)
                string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" shader_name "${reference}")
                list(APPEND shader_outputs "${PROJECT_SHADER_DIR}/${shader_name}")
            endforeach()
        endforeach()

        if(shader_outputs)
            list(REMOVE_DUPLICATES shader_outputs)
            add_custom_target("${base_target_name}_shaders" DEPENDS ${shader_outputs})
            project_setup_ide("${base_target_name}_shaders" FOLDER "Samples/Shaders")
        endif()

        foreach(sample_entry IN LISTS sample_entries)
            get_filename_component(entry_name "${sample_entry}" NAME_WE)
            string(REGEX REPLACE "^main" "" entry_suffix "${entry_name}")
            set(target_name "${base_target_name}${entry_suffix}")
            add_executable("${target_name}" "${sample_entry}" ${sample_common_sources} ${sample_headers})
            project_setup_target("${target_name}")
            target_link_libraries("${target_name}" PRIVATE
                Vulkan::Vulkan
                ThirdParty::glfw
                ThirdParty::glm
                ThirdParty::imgui
                ThirdParty::meshoptimizer
                ThirdParty::stb_image
                ThirdParty::stb_image_write
                ThirdParty::tiny_obj
                ThirdParty::tiny_gltf
                ThirdParty::vma
            )
            target_compile_definitions("${target_name}" PRIVATE
                "PROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}/\""
                "PROJECT_SHADER_DIR=\"${PROJECT_SHADER_DIR}/\""
            )
            project_setup_ide("${target_name}" FOLDER "Samples/${group_name}")
            if(shader_outputs)
                add_dependencies("${target_name}" "${base_target_name}_shaders")
            endif()
        endforeach()
    endforeach()
endfunction()

samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/01_vulkan_tutorial")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/02_advance")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/03_compute_shader")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/04_headless")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/05_geometry_shader")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/06_extensions")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/07_vulkan_hpp")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/08_showcases")
samples_add_group("${CMAKE_CURRENT_SOURCE_DIR}/09_memory_allocation")
