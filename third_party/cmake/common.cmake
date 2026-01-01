function(third_party_setup_target target)
    set_target_properties("${target}" PROPERTIES
        FOLDER "ThirdParty"
        COMPILE_WARNING_AS_ERROR OFF
        C_CLANG_TIDY ""
        CXX_CLANG_TIDY ""
    )

    if(MSVC)
        get_target_property(compile_options "${target}" COMPILE_OPTIONS)
        if(compile_options)
            list(FILTER compile_options EXCLUDE REGEX "^/W[0-4]$|^/Wall$")
            set_property(TARGET "${target}" PROPERTY COMPILE_OPTIONS ${compile_options})
        endif()
        target_compile_options("${target}" PRIVATE /w)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang"
        OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options("${target}" PRIVATE -w)
    endif()
endfunction()
