set(THIRD_PARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Do not inherit directory-wide static analysis into vendored sources.
set(CMAKE_C_CLANG_TIDY "")
set(CMAKE_CXX_CLANG_TIDY "")

include("${THIRD_PARTY_ROOT}/cmake/common.cmake")
include("${THIRD_PARTY_ROOT}/cmake/glfw.cmake")
include("${THIRD_PARTY_ROOT}/cmake/glm.cmake")
include("${THIRD_PARTY_ROOT}/cmake/imgui.cmake")
include("${THIRD_PARTY_ROOT}/cmake/meshoptimizer.cmake")
include("${THIRD_PARTY_ROOT}/cmake/stb.cmake")
include("${THIRD_PARTY_ROOT}/cmake/tiny_obj.cmake")
include("${THIRD_PARTY_ROOT}/cmake/nlohmann_json.cmake")
include("${THIRD_PARTY_ROOT}/cmake/tiny_gltf.cmake")
