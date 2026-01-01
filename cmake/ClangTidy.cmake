include_guard(GLOBAL)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

function(project_enable_clang_tidy target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "project_enable_clang_tidy: target '${target}' does not exist.")
    endif()

    if(ENABLE_CLANG_TIDY)
        set_target_properties("${target}" PROPERTIES
            C_CLANG_TIDY "${CLANG_TIDY_EXE}"
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
        )
    endif()
endfunction()
