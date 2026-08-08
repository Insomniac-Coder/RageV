cmake_minimum_required(VERSION 3.21)

# Dist is a Release build with logging and asserts compiled out. Declared before
# project() so the multi-config generators pick it up.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)

project(RageV
    VERSION 0.2.0
    DESCRIPTION "RageV Engine"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    # The old premake build forced /MT purely to match a prebuilt glfw3_mt.lib.
    # GLFW is built from source now, so use the DLL runtime everything else expects.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# The Dist configuration inherits Release codegen.
foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_DIST "${CMAKE_${lang}_FLAGS_RELEASE}" CACHE STRING "" FORCE)
endforeach()
foreach(kind EXE SHARED MODULE STATIC)
    set(CMAKE_${kind}_LINKER_FLAGS_DIST "${CMAKE_${kind}_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
endforeach()
set(CMAKE_MAP_IMPORTED_CONFIG_DIST Release "")

# Generator-expression form rather than the per-config CMAKE_*_OUTPUT_DIRECTORY_<CFG>
# variables: those set per-config target properties, which take precedence over
# the per-target RUNTIME_OUTPUT_DIRECTORY the applications use to claim their own
# directories.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

option(RAGEV_BUILD_SANDBOX "Build the Sandbox sample application" ON)
option(RAGEV_BUILD_TOOLS "Build the offline shader inspection tool" ON)
option(RAGEV_ENABLE_VALIDATION "Enable Vulkan validation layers in Debug builds" ON)

if(MSVC)
    add_compile_options(/MP)                 # parallel compilation
    add_compile_options(/utf-8)              # required by spdlog's bundled fmt
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(RageV/vendor)
add_subdirectory(RageV)
add_subdirectory(RageVEditor)
if(RAGEV_BUILD_SANDBOX)
    add_subdirectory(Sandbox)
endif()
if(RAGEV_BUILD_TOOLS)
    add_subdirectory(tools/shaderinfo)
    add_subdirectory(tools/rhismoke)
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RageVEditor)
endif()
