cmake_minimum_required(VERSION 3.28)

# Use embedded debug information with the MSVC compiler.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

project(PlatformAdventure LANGUAGES CXX)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SFML
    URL https://github.com/SFML/SFML/archive/refs/tags/3.0.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SFML)

add_executable(
    PlatformAdventure
    src/main.cpp
)

target_compile_features(
    PlatformAdventure
    PRIVATE
    cxx_std_17
)

set_target_properties(
    PlatformAdventure
    PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(
    PlatformAdventure
    PRIVATE
    SFML::Graphics
)
