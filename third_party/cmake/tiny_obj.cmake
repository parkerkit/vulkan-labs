add_library(third_party_tiny_obj INTERFACE)
add_library(ThirdParty::tiny_obj ALIAS third_party_tiny_obj)
target_include_directories(third_party_tiny_obj SYSTEM INTERFACE "${THIRD_PARTY_ROOT}")
