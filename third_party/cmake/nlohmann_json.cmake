set(NLOHMANN_JSON_INCLUDE_DIR "${THIRD_PARTY_ROOT}/nlohmann/include")

add_library(third_party_nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS third_party_nlohmann_json)
add_library(ThirdParty::nlohmann_json ALIAS third_party_nlohmann_json)

target_include_directories(third_party_nlohmann_json SYSTEM INTERFACE "${NLOHMANN_JSON_INCLUDE_DIR}")
target_compile_features(third_party_nlohmann_json INTERFACE cxx_std_11)
