cmake_minimum_required(VERSION 3.24)

# Версия берётся из единственного источника — src/core/version.hpp.
file(STRINGS src/core/version.hpp _ver REGEX "kVersion = \"[^\"]+\"")
string(REGEX REPLACE ".*kVersion = \"([0-9]+\\.[0-9]+\\.[0-9]+)[^\"]*\".*" "\\1" OC2_VERSION "${_ver}")

project(opencossacks2 VERSION ${OC2_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

include(cmake/Dependencies.cmake)

add_executable(opencossacks2
    src/main.cpp
    src/platform/window.cpp
    src/render/vulkan_context.cpp
)
target_link_libraries(opencossacks2 PRIVATE SDL3::SDL3-static volk)
if(WIN32)
    set_target_properties(opencossacks2 PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_include_directories(opencossacks2 PRIVATE src)

if(MSVC)
    target_compile_options(opencossacks2 PRIVATE /W4 /utf-8 /permissive-)
else()
    # Vulkan-структуры инициализируются как {VK_STRUCTURE_TYPE_...}, остальное — нулями намеренно.
    target_compile_options(opencossacks2 PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
