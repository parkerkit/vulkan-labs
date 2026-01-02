add_library(third_party_vma STATIC
    "${THIRD_PARTY_ROOT}/src/vma.cpp"
)
add_library(ThirdParty::vma ALIAS third_party_vma)

target_include_directories(third_party_vma SYSTEM PUBLIC "${THIRD_PARTY_ROOT}/vma")
target_link_libraries(third_party_vma PUBLIC Vulkan::Vulkan)
third_party_setup_target(third_party_vma)
