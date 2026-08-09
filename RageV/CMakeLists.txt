file(GLOB_RECURSE RAGEV_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(RageV STATIC ${RAGEV_SOURCES})
add_library(RageV::RageV ALIAS RageV)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${RAGEV_SOURCES})

target_include_directories(RageV PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(RageV
    PUBLIC
        glm
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
# RageV/Math declares its operators and defines them in one translation unit, so
# that no public header has to include glm. Without this, `a + b` on two vectors
# would be a call across a translation-unit boundary in the hot path of a
# renderer. With it, the optimiser sees glm's bodies at every call site and folds
# them in -- the arithmetic is glm's, and the abstraction costs nothing.
#
# Not enabled for Debug: it would slow the link for a configuration nobody
# measures, and the call boundary is affordable there. Not enabled for the
# vendored libraries either -- they gain nothing here and would cost minutes of
# link time each.
set_target_properties(RageV PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_DIST TRUE)

# Where a build run out of the build tree finds a project when nobody says.
# Absent from an installed build, which then relies on a .rvproject sitting
# beside the executable -- the shape a packaged game has.
target_compile_definitions(RageV PUBLIC
    RV_DEFAULT_PROJECT="${CMAKE_SOURCE_DIR}/SampleProject")

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
