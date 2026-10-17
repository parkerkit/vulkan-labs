include_guard(GLOBAL)

function(project_setup_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "project_setup_target: target '${target}' does not exist.")
    endif()

    target_link_libraries("${target}" PRIVATE Project::Warnings Project::Options)
    project_enable_clang_tidy("${target}")
endfunction()

function(project_setup_ide target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "project_setup_ide: target '${target}' does not exist.")
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 IDE "" "FOLDER" "")
    if(IDE_UNPARSED_ARGUMENTS OR IDE_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "project_setup_ide: expected '${target} [FOLDER <folder>]'.")
    endif()

    if(DEFINED IDE_FOLDER)
        set_target_properties("${target}" PROPERTIES FOLDER "${IDE_FOLDER}")
    endif()

    get_target_property(target_type "${target}" TYPE)
    if(target_type STREQUAL "EXECUTABLE")
        set_target_properties("${target}" PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
        )
    endif()
endfunction()
