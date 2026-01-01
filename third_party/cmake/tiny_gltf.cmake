add_library(third_party_tiny_gltf INTERFACE)
add_library(ThirdParty::tiny_gltf ALIAS third_party_tiny_gltf)
target_include_directories(third_party_tiny_gltf SYSTEM INTERFACE
    "${THIRD_PARTY_ROOT}"
    # TinyGLTF includes "json.hpp" directly; keep that upstream include unchanged.
    "${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann"
)
target_link_libraries(third_party_tiny_gltf INTERFACE
    nlohmann_json::nlohmann_json
    ThirdParty::stb_image
    ThirdParty::stb_image_write
)
