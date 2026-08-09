file(GLOB_RECURSE RAGEV_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(RageV STATIC ${RAGEV_SOURCES})
add_library(RageV::RageV ALIAS RageV)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${RAGEV_SOURCES})

target_include_directories(RageV PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# glm is deliberately absent. RageV/Math implements its own vectors, matrices
# and quaternions; the vendored glm is now linked only into scenetest, where it
# serves as the oracle those implementations are checked against.
target_link_libraries(RageV
    PUBLIC
        entt
        spdlog
        imgui
        imguizmo
        yaml-cpp
        volk_headers
    PRIVATE
        glad
        stb
        cgltf
        Jolt
        miniaudio
        glfw
        volk
        vma
        perlin_noise
        glslang
        SPIRV
        glslang-default-resource-limits
        spirv-cross-core
        spirv-cross-glsl
        opengl32)

target_precompile_headers(RageV PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/rvpch.h")

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

# Link-time code generation, for Release and Dist only.
#
# Kept from when RageV/Math delegated across a translation-unit boundary and
# needed the optimiser's help to inline it. The trivial operators are in the
# header now so it matters less, but it still lets the out-of-line half --
# inversion, projections, quaternions -- fold into its callers, and it costs
# nothing but link time in configurations nobody iterates in.
#
# Not enabled for Debug, where the link time is the thing being spent. Not
# enabled for the vendored libraries either: they gain nothing and would cost
# minutes each.
set_target_properties(RageV PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_DIST TRUE)

# Where a build run out of the build tree finds a project when nobody says.
# Absent from an installed build, which then relies on a .rvproject sitting
# beside the executable -- the shape a packaged game has.
target_compile_definitions(RageV PUBLIC
    RV_DEFAULT_PROJECT="${CMAKE_SOURCE_DIR}/SampleProject")

# Every object file in RageV.lib is linked, whether or not anything references
# a symbol in it.
#
# Without this, a translation unit whose only contents are a static registrar --
# which is exactly what a script file is -- gets discarded, and the script
# compiles, links, and never appears in the inspector. There is no fix on the
# library side: a `#pragma comment(linker, "/include:...")` lives in the object
# file being discarded, so the directive never reaches the linker. The engine's
# own built-in scripts work around it by being registered from an explicitly
# called function, which a file somebody just created cannot do.
#
# The cost is link time and a larger binary, unmeasured -- the alternative is a
# script that silently does not exist, so it was not a trade worth measuring
# before making.
#
# Applied as an INTERFACE property so every consumer gets it -- an executable
# that forgets it would have scripts silently missing, which is the whole
# failure being fixed.
if(MSVC)
    target_link_options(RageV INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE:RageV>")
endif()

# Where "New Script..." puts a generated C++ script.
#
# Only meaningful in a build tree -- an installed editor has no engine source to
# add a file to, which is the honest reason C++ scripting is not a per-project
# feature. The editor checks that the folder exists before offering to write
# there, so a packaged build simply explains what to do by hand instead.
target_compile_definitions(RageV PUBLIC
    RV_ENGINE_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/RageV/src/RageV/Scripts")

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
