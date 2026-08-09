cmake_minimum_required(VERSION 3.21)

# The same three configurations as the engine. They must match: an import
# library is per-config, and a config the engine does not have is a link error.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)

project(SampleModule LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dist inherits Release codegen, exactly as the engine's top level does.
foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_DIST "${CMAKE_${lang}_FLAGS_RELEASE}" CACHE STRING "" FORCE)
endforeach()
foreach(kind EXE SHARED MODULE STATIC)
    set(CMAKE_${kind}_LINKER_FLAGS_DIST "${CMAKE_${kind}_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
endforeach()

# The editor passes -DRAGEV_ENGINE=<engine build directory> when it configures
# this. The engine's build writes RageVTargets.cmake there, and importing it is
# what makes RageV::RageV exist here with its include paths, definitions and
# import libraries carried along -- nothing about the engine is spelled out in
# this file, so this file does not go stale when the engine changes.
if(NOT DEFINED RAGEV_ENGINE OR NOT EXISTS "${RAGEV_ENGINE}/RageVTargets.cmake")
    message(FATAL_ERROR
        "RAGEV_ENGINE must point at a RageV build directory "
        "(one containing RageVTargets.cmake). The editor passes it "
        "automatically; by hand: cmake -S Source -B bin/module "
        "-DRAGEV_ENGINE=<engine>/build")
endif()

# GLFW's exported interface names Threads::Threads, which comes from
# find_package rather than from the export file -- so it has to exist before
# the include, or the import fails naming a target this file never mentions.
find_package(Threads REQUIRED)

include("${RAGEV_ENGINE}/RageVTargets.cmake")

# Scripts sit flat in Source/, the way C# scripts sit flat in Scripts/.
# A plain GLOB, not GLOB_RECURSE: recursing would sweep up any build
# directory somebody configures inside Source/, including CMake's own
# compiler-probe .cpp files.
file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

add_library(Sample SHARED ${MODULE_SOURCES})
target_link_libraries(Sample PRIVATE RageV::RageV)

# The DLL lands beside the project's other build output, where the engine
# looks for it: <project>/bin/<Config>/. Because this property contains a
# generator expression, CMake does not append a per-config subdirectory of
# its own.
set_target_properties(Sample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/$<CONFIG>")

if(MSVC)
    # /utf-8 is load-bearing: spdlog's bundled fmt refuses to compile without
    # it, and Log.h reaches it from almost every engine header.
    target_compile_options(Sample PRIVATE /MP /W3 /utf-8)
endif()
