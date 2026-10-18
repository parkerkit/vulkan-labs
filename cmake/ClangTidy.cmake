include_guard(GLOBAL)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

function(project_enable_clang_tidy target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "project_enable_clang_tidy: target '${target}' does not exist.")
    endif()

    if(ENABLE_CLANG_TIDY)
        set(cxx_clang_tidy "${CLANG_TIDY_EXE}")
        if(MSVC)
            # Use the dash form so clang-tidy recognizes the exception option.
            list(APPEND cxx_clang_tidy "--extra-arg=-EHsc")
        endif()

        set_target_properties("${target}" PROPERTIES
            C_CLANG_TIDY "${CLANG_TIDY_EXE}"
            CXX_CLANG_TIDY "${cxx_clang_tidy}"
        )
    endif()
endfunction()
