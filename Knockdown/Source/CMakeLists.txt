cmake_minimum_required(VERSION 3.21)

# The same three configurations as the engine. They must match: an import
# library is per-config, and a config the engine does not have is a link error.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)

project(KnockdownModule LANGUAGES CXX)

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

# Declaration-site script markers (RVShowInEditor / RVCallable / RVScript).
#
# rvgen scans Source/ and, for every file that marks something, writes a
# wrapper TU that #includes the file and appends the registrations the
# markers describe -- so a marked file compiles through its wrapper instead
# of directly, and a script says `RVShowInEditor` above a field instead of
# ending the file with an RV_REGISTER_SCRIPT block.
#
# It runs at *configure* time because the source list depends on its answer,
# and only a configure can change the source list. The CONFIGURE_DEPENDS
# loop below is the other half: editing any script re-runs the scan, so the
# first marker added to a file cannot sit registering nothing until somebody
# happens to reconfigure.
foreach(script ${MODULE_SOURCES})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script}")
endforeach()

# Any config's rvgen: its output does not depend on how it was compiled.
set(RVGEN_EXE "")
foreach(config Release Debug Dist)
    if(EXISTS "${RAGEV_ENGINE}/bin/${config}/rvgen/rvgen.exe")
        set(RVGEN_EXE "${RAGEV_ENGINE}/bin/${config}/rvgen/rvgen.exe")
        break()
    endif()
endforeach()
if(NOT RVGEN_EXE)
    message(FATAL_ERROR
        "rvgen.exe was not found under ${RAGEV_ENGINE}/bin/<config>/rvgen. "
        "It generates script registrations from RVShowInEditor markers; "
        "build the engine once, then configure this module again.")
endif()

execute_process(
    COMMAND "${RVGEN_EXE}" --source "${CMAKE_CURRENT_SOURCE_DIR}"
                           --out "${CMAKE_CURRENT_BINARY_DIR}/rvgen"
    RESULT_VARIABLE RVGEN_RESULT
    ERROR_VARIABLE RVGEN_ERRORS)
if(NOT RVGEN_RESULT EQUAL 0)
    # The error lines are MSVC-shaped, so the editor's build panel parses
    # them out of this message like any other compile error.
    message(FATAL_ERROR "rvgen refused a marked declaration:\n${RVGEN_ERRORS}")
endif()

include("${CMAKE_CURRENT_BINARY_DIR}/rvgen/rvgen.manifest.cmake")
if(RVGEN_WRAPPED_SOURCES)
    list(REMOVE_ITEM MODULE_SOURCES ${RVGEN_WRAPPED_SOURCES})
endif()

add_library(Knockdown SHARED ${MODULE_SOURCES} ${RVGEN_GENERATED})
target_link_libraries(Knockdown PRIVATE RageV::RageV)

# The DLL lands beside the project's other build output, where the engine
# looks for it: <project>/bin/<Config>/. Because this property contains a
# generator expression, CMake does not append a per-config subdirectory of
# its own.
set_target_properties(Knockdown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/$<CONFIG>")

if(MSVC)
    # /utf-8 is load-bearing: spdlog's bundled fmt refuses to compile without
    # it, and Log.h reaches it from almost every engine header.
    target_compile_options(Knockdown PRIVATE /MP /W3 /utf-8)
endif()
