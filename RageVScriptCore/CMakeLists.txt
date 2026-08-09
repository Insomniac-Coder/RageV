# The managed script assembly.
#
# Optional on purpose. This repository builds from a clone with nothing but a
# C++ compiler installed, and one scripting language is not worth giving that
# up -- so a missing .NET SDK downgrades the build rather than failing it, and
# the engine reports at runtime that C# is unavailable.

# Defined before the early return below, so every caller can call it
# unconditionally and this file stays the only place that knows whether there is
# anything to stage.
function(ragev_stage_managed target)
    if(NOT TARGET ScriptCore)
        return()
    endif()

    add_dependencies(${target} ScriptCore)

    # Staged next to each executable that can host it, the same way assets are.
    # Not shared from one location: the tools are run from their own directories
    # by design, and an assembly resolved relative to the working directory
    # would work only for whichever one happened to be launched from the right
    # place.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/managed"
            "$<TARGET_FILE_DIR:${target}>/managed"
        VERBATIM)
endfunction()

find_program(RAGEV_DOTNET dotnet)

if(NOT RAGEV_DOTNET)
    message(STATUS "RageV: no 'dotnet' on PATH -- C# scripting will be unavailable at runtime.")
    message(STATUS "RageV: install the .NET 8 SDK and re-run CMake to enable it.")
    return()
endif()

message(STATUS "RageV: building the managed script assembly with ${RAGEV_DOTNET}")

set(RAGEV_MANAGED_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/managed")

# Dist maps to Release: the .NET SDK has no such configuration, and shipping a
# Debug-compiled script assembly would carry debug JIT behaviour into a packaged
# game.
set(RAGEV_MANAGED_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

file(GLOB_RECURSE RAGEV_SCRIPTCORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cs")

add_custom_command(
    OUTPUT "${RAGEV_MANAGED_DIR}/RageV.ScriptCore.dll"
    COMMAND "${RAGEV_DOTNET}" build
            "${CMAKE_CURRENT_SOURCE_DIR}/RageV.ScriptCore.csproj"
            --configuration ${RAGEV_MANAGED_CONFIG}
            --output "${RAGEV_MANAGED_DIR}"
            --nologo
            -v quiet
    DEPENDS ${RAGEV_SCRIPTCORE_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/RageV.ScriptCore.csproj"
    COMMENT "Building RageV.ScriptCore"
    VERBATIM)

add_custom_target(ScriptCore ALL
    DEPENDS "${RAGEV_MANAGED_DIR}/RageV.ScriptCore.dll")
set_target_properties(ScriptCore PROPERTIES FOLDER "Managed")
