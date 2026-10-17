cmake_minimum_required(VERSION 3.25)

project(
    vulkan-labs
    VERSION 0.1.0
    DESCRIPTION "Vulkan samples and rendering experiments"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(BUILD_SAMPLES "Build the Vulkan samples" ON)
option(BUILD_FRAMEWORK "Build the reusable Vulkan framework" ON)
option(BUILD_FRAMEWORK_EXAMPLES "Build the framework interactive examples" ON)
option(BUILD_SHOWCASES "Build the showcases with the framework" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy for project targets" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(ProjectWarnings)
include(ProjectOptions)
include(ClangTidy)
include(ProjectTarget)
include(ProjectShaders)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/assets" PROJECT_ASSETS_DIR)
file(TO_CMAKE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders" PROJECT_SHADER_DIR)

find_package(Vulkan REQUIRED)

add_subdirectory(third_party)

if(BUILD_FRAMEWORK)
    add_subdirectory(framework)
endif()

if(BUILD_FRAMEWORK AND BUILD_SHOWCASES)
    add_subdirectory(showcases)
endif()

if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
