add_subdirectory("${THIRD_PARTY_ROOT}/glfw" "${CMAKE_CURRENT_BINARY_DIR}/glfw" SYSTEM)
add_library(ThirdParty::glfw ALIAS glfw)
target_include_directories(glfw SYSTEM INTERFACE "${THIRD_PARTY_ROOT}/glfw/include")
third_party_setup_target(glfw)
