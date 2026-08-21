file(GLOB_RECURSE RAGEV_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

# A DLL, not a static library.
#
# Not a packaging preference -- it is what lets a game have C++ scripts without
# rebuilding the engine. ScriptRegistry keeps its factories in a function-local
# static; a project module that linked the engine statically would get its own
# copy of that map, register into it, and the editor would never see a thing.
# The registrations would look exactly like a silent failure. One engine module
# means one registry, one Application, one Renderer, for every module that
# loads it.
#
# The cost: everything that links this must be built by the same toolchain with
# the same runtime library. That is Unreal's rule too, and the shared /MD
# runtime set at the top level is what makes memory allocated on one side
# free-able on the other.
add_library(RageV SHARED ${RAGEV_SOURCES})
add_library(RageV::RageV ALIAS RageV)

# Deleting a source file must not strand its symbols in the export list.
#
# The .obj of a deleted source survives in the objects directory, and the
# exports.def is only regenerated when *it* is out of date -- a removed input
# does not age it -- so the link fails on symbols from a file that no longer
# exists. A pre-build pass prunes objects whose source is gone and takes the
# stale .def with them; the def rule then rebuilds it because its output is
# missing. See cmake/PruneStaleObjects.cmake for the full story.
set(RAGEV_EXPECTED_OBJECTS "")
foreach(RAGEV_SOURCE ${RAGEV_SOURCES})
    if(RAGEV_SOURCE MATCHES "\\.cpp$")
        get_filename_component(RAGEV_OBJECT_NAME "${RAGEV_SOURCE}" NAME_WE)
        list(APPEND RAGEV_EXPECTED_OBJECTS "${RAGEV_OBJECT_NAME}.obj")
    endif()
endforeach()
string(REPLACE ";" "\n" RAGEV_EXPECTED_OBJECTS "${RAGEV_EXPECTED_OBJECTS}")
file(WRITE "${CMAKE_BINARY_DIR}/RageV_expected_objects.txt" "${RAGEV_EXPECTED_OBJECTS}\n")

add_custom_command(TARGET RageV PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DOBJECT_DIR=${CMAKE_BINARY_DIR}/RageV/RageV.dir/$<CONFIG>
        -DEXPECTED=${CMAKE_BINARY_DIR}/RageV_expected_objects.txt
        -P ${CMAKE_SOURCE_DIR}/cmake/PruneStaleObjects.cmake
    COMMENT "Pruning objects whose sources are gone")

# Export every symbol rather than annotating the public API by hand.
#
# The tempting alternative -- __declspec(dllexport) on the classes a game module
# needs -- does not survive contact with the editor, which reaches into the
# renderer, the RHI and the asset system. Its export surface is "most of the
# engine", so a hand-maintained list would be a list nobody can keep correct,
# and the failure mode is an unresolved symbol at link time in whichever target
# was forgotten.
#
# Two things this does not cover, both handled elsewhere:
#   - static data members read by an inline accessor. Those are *data* symbols,
#     and a consumer links its own copy rather than the engine's. Application,
#     Log and Platform had exactly that shape and now call out of line instead.
#   - ImGui, which keeps its context in a global. See ImGuiBinding.h.
set_target_properties(RageV PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${RAGEV_SOURCES})

target_include_directories(RageV PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# glm is deliberately absent. RageV/Math implements its own vectors, matrices
# and quaternions; the vendored glm is now linked only into scenetest, where it
# serves as the oracle those implementations are checked against.
target_link_libraries(RageV
    PUBLIC
        spdlog
        imgui
        imguizmo
        yaml-cpp
        volk_headers
    PRIVATE
        glad
        stb
        cgltf
        ufbx
        Jolt
        miniaudio
        glfw
        volk
        vma
        glslang
        SPIRV
        glslang-default-resource-limits
        spirv-cross-core
        spirv-cross-glsl
        opengl32)

target_precompile_headers(RageV PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/rvpch.h")
set_target_properties(RageV PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)

target_compile_definitions(RageV
    PUBLIC
        $<$<CONFIG:Debug>:RV_DEBUG>
        $<$<CONFIG:Release>:RV_RELEASE>
        $<$<CONFIG:Dist>:RV_DIST>
        YAML_CPP_STATIC_DEFINE
    PRIVATE
        RV_PLATFORM_WINDOWS
        VK_NO_PROTOTYPES               # volk owns the entry points
        VK_USE_PLATFORM_WIN32_KHR
        GLFW_INCLUDE_NONE)             # we include vulkan/volk ourselves

# No link-time code generation here, unlike the executables.
#
# It was enabled for Release and Dist, from when RageV/Math delegated across a
# translation-unit boundary and needed the optimiser's help to inline it. It is
# incompatible with WINDOWS_EXPORT_ALL_SYMBOLS: /GL writes intermediate
# language into the object files, and the tool that scans them to build the
# export list cannot read those -- so the DLL would export nothing and every
# consumer would fail to link.
#
# The loss is smaller than it sounds. The trivial math operators are in the
# header, so they still inline; and cross-module inlining into the editor was
# never going to survive a DLL boundary anyway. The executables keep their own
# LTCG for their own code.

# Where a build run out of the build tree finds a project when nobody says.
# Absent from an installed build, which then relies on a .rvproject sitting
# beside the executable -- the shape a packaged game has.
target_compile_definitions(RageV PUBLIC
    RV_DEFAULT_PROJECT="${CMAKE_SOURCE_DIR}/SampleProject")

# The /WHOLEARCHIVE that used to be here is gone, and deliberately.
#
# It existed because a static library drops any object file nothing references,
# and a script file's only content is a static registrar -- so the script would
# compile, link, and never appear in the inspector. A DLL is linked from its
# object files directly rather than from an archive, so every translation unit
# is in it whether or not anything calls into it. The same is true of a project's
# game module, which is why a script there needs no equivalent.
#
# TemplateProbe.cpp still proves it: it is the file "New Script" generates, kept
# in the tree so every build checks that the generated shape both compiles and
# registers.

# The build-tree export: what lets a project's game module link the engine
# from outside this build tree.
#
# A project's Source/CMakeLists.txt does include("<here>/RageVTargets.cmake")
# and links RageV::RageV, and the imported target carries the include
# directories, compile definitions and per-config import library locations --
# the same interface in-tree consumers get, kept correct by CMake rather than
# by a hand-maintained list in every generated project.
#
# The set is RageV plus the transitive closure of its PUBLIC dependencies,
# because an export file may not reference a target outside its own set. imgui
# is the reason glfw is here: it links glfw PUBLIC.
#
# install(TARGETS) rather than export(TARGETS): only install() may name
# targets defined in other directories, and the vendor targets are. Nothing
# actually installs -- the EXPORT set is the point, and export(EXPORT) below
# writes it into the build tree.
install(TARGETS RageV spdlog imgui imguizmo yaml-cpp volk_headers glfw
    EXPORT RageVTargets)
export(EXPORT RageVTargets
    NAMESPACE RageV::
    FILE "${CMAKE_BINARY_DIR}/RageVTargets.cmake")

# Where that export file is, baked in so the editor can configure a project's
# game module at runtime -- the same trick RV_ENGINE_SCRIPTS_DIR uses. Only
# meaningful in a build tree; a packaged editor has RAGEV_ENGINE passed to it
# or does without.
#
# RV_CMAKE_EXE is the cmake that configured this build. If it built the
# engine, it can build a module against it, and unlike "cmake on PATH" it is
# guaranteed to exist on this machine -- the VS-bundled cmake usually is not
# on PATH at all. ModuleBuild falls back to PATH when the baked one is gone.
target_compile_definitions(RageV PUBLIC
    RV_ENGINE_EXPORT_DIR="${CMAKE_BINARY_DIR}"
    RV_CMAKE_EXE="${CMAKE_COMMAND}")

# RV_ENGINE_SCRIPTS_DIR is gone: "New Script..." writes into the project's
# own Source/ now, which the game module builds from. The engine's Scripts/
# folder still exists, but only engine development touches it.

if(RAGEV_ENABLE_VALIDATION)
    target_compile_definitions(RageV PRIVATE $<$<CONFIG:Debug>:RV_VULKAN_VALIDATION>)
endif()

# RV_PLATFORM_WINDOWS has to reach consumers too -- Core.h keys the API macros
# off it and #errors without it.
target_compile_definitions(RageV PUBLIC RV_PLATFORM_WINDOWS)

if(MSVC)
    target_compile_options(RageV PRIVATE /W3 /permissive-)
endif()

set_target_properties(RageV PROPERTIES FOLDER "Engine")

# Put RageV.dll beside whatever links it.
#
# Every application here gets its own output directory -- they ship different
# copies of assets/ and a shared one let whichever built last overwrite the
# other's -- and Windows looks for a DLL next to the executable, not in the
# directory above it. Without this an application builds cleanly and then fails
# to start with a dialog naming a file that is plainly there, one folder up.
#
# TARGET_RUNTIME_DLLS rather than a hard-coded RageV.dll: it resolves whatever
# the target actually depends on, so adding a shared dependency later does not
# mean remembering to add another copy rule to seven CMakeLists.
function(ragev_stage_engine target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Staging the engine DLL beside ${target}")
endfunction()
