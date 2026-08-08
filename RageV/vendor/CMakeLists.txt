# Third-party dependencies. Everything here is vendored as a git submodule or a
# source drop and built from source -- there are no prebuilt binaries and no
# dependency on an installed Vulkan SDK.

set(VENDOR_FOLDER "Dependencies")

# ---------------------------------------------------------------------------
# GLAD -- OpenGL loader (source drop)
# ---------------------------------------------------------------------------
add_library(glad STATIC GLAD/src/glad.c)
target_include_directories(glad PUBLIC GLAD/include)
set_target_properties(glad PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# stb -- image load/write (source drop)
# ---------------------------------------------------------------------------
add_library(stb STATIC stb_image/stb_image.cpp stb_image/stb_write_image.cpp)
target_include_directories(stb PUBLIC stb_image)
set_target_properties(stb PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# cgltf -- glTF 2.0 parsing (single header, no dependencies)
#
# Chosen over fastgltf, which the roadmap named: fastgltf needs simdjson, which
# means a second submodule and its CMake, in exchange for parse throughput that
# does not matter at editor scale. cgltf is one file with no dependencies and
# ships in bgfx, Filament, raylib and Unigine. If import speed ever becomes the
# bottleneck the swap is contained to one importer.
# ---------------------------------------------------------------------------
add_library(cgltf STATIC cgltf_impl/cgltf_impl.c)
target_include_directories(cgltf PUBLIC cgltf)
set_target_properties(cgltf PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# Jolt Physics
#
# Not written here for the same reason Godot stopped writing its own: a
# competitive rigid-body solver is not a good use of a solo project's time, and
# Jolt is what Godot 4.6 ships as its default.
#
# The options matter more than usual:
#   OVERRIDE_CXX_FLAGS  Jolt rewrites CMAKE_CXX_FLAGS_DEBUG/RELEASE globally,
#                       which would silently replace this project's own.
#   ENABLE_ALL_WARNINGS Warnings-as-errors inside a vendored dependency turns
#                       every compiler upgrade into a build break.
#   AVX / AVX2          On by default. Left off so a build runs on any x86-64
#                       rather than crashing with an illegal instruction on
#                       hardware that predates them; at this scale SSE4.2 costs
#                       nothing measurable.
#   CPP_EXCEPTIONS      On, to match the rest of the project -- mixing exception
#                       settings across a static library boundary is a class of
#                       bug nobody enjoys finding.
#   USE_ASSERTS         On. Jolt's asserts catch misuse (a body added twice, a
#                       shape with zero extent) that otherwise shows up as
#                       nonsense simulation much later.
# ---------------------------------------------------------------------------
#   USE_STATIC_MSVC_RUNTIME_LIBRARY
#                       Jolt defaults to the static CRT while this project uses
#                       the dynamic one. Mixing them is a hard link error, and
#                       the project's choice wins.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(USE_ASSERTS ON CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)

add_subdirectory(JoltPhysics/Build)
set_target_properties(Jolt PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# GLFW
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS      OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL         OFF CACHE BOOL "" FORCE)
add_subdirectory(GLFW EXCLUDE_FROM_ALL)
set_target_properties(glfw PROPERTIES FOLDER ${VENDOR_FOLDER})
foreach(t update_mappings)
    if(TARGET ${t})
        set_target_properties(${t} PROPERTIES FOLDER ${VENDOR_FOLDER})
    endif()
endforeach()

# ---------------------------------------------------------------------------
# glm -- header only
# ---------------------------------------------------------------------------
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE glm)
# GLM_FORCE_DEPTH_ZERO_TO_ONE matches Vulkan's [0,1] clip range; the OpenGL
# backend compensates once at the swapchain rather than keeping two conventions.
target_compile_definitions(glm INTERFACE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL)

# ---------------------------------------------------------------------------
# EnTT / PerlinNoise -- header only
# ---------------------------------------------------------------------------
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE EnTT/include)

add_library(perlin_noise INTERFACE)
target_include_directories(perlin_noise INTERFACE PerlinNoise)

# ---------------------------------------------------------------------------
# spdlog -- header only mode, no separate compilation
# ---------------------------------------------------------------------------
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE spdlog/include)

# ---------------------------------------------------------------------------
# yaml-cpp
# ---------------------------------------------------------------------------
set(YAML_CPP_BUILD_TOOLS     OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL         OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(yaml-cpp EXCLUDE_FROM_ALL)
set_target_properties(yaml-cpp PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# Dear ImGui (docking branch) + the GLFW / OpenGL3 / Vulkan backends
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC imgui imgui/backends)
target_link_libraries(imgui PUBLIC glfw volk_headers)
# The Vulkan backend must route through volk rather than link vulkan-1 directly.
target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_VULKAN_USE_VOLK
    IMGUI_DEFINE_MATH_OPERATORS)
set_target_properties(imgui PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# ImGuizmo
# ---------------------------------------------------------------------------
add_library(imguizmo STATIC ImGuizmo/src/ImGuizmo.cpp)
target_include_directories(imguizmo PUBLIC ImGuizmo/src)
target_link_libraries(imguizmo PUBLIC imgui)
set_target_properties(imguizmo PROPERTIES FOLDER ${VENDOR_FOLDER})

# ---------------------------------------------------------------------------
# Vulkan: headers + volk loader + VMA allocator
# No installed SDK is required to build; validation layers are picked up at
# runtime if the user happens to have them installed.
# ---------------------------------------------------------------------------
add_subdirectory(Vulkan-Headers EXCLUDE_FROM_ALL)

set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR CACHE STRING "" FORCE)
set(VOLK_PULL_IN_VULKAN ON  CACHE BOOL "" FORCE)
set(VOLK_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(volk EXCLUDE_FROM_ALL)
set_target_properties(volk PROPERTIES FOLDER ${VENDOR_FOLDER})

add_library(vma INTERFACE)
target_include_directories(vma INTERFACE VulkanMemoryAllocator/include)
target_link_libraries(vma INTERFACE volk_headers)
# VMA must not pull in the static loader; volk supplies the entry points.
target_compile_definitions(vma INTERFACE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

# ---------------------------------------------------------------------------
# glslang -- runtime GLSL -> SPIR-V. ENABLE_OPT=OFF keeps it standalone so we
# do not need SPIRV-Tools checked out under External/.
# ---------------------------------------------------------------------------
set(ENABLE_OPT               OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL              OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES  OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS            OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL           OFF CACHE BOOL "" FORCE)
set(ENABLE_SPIRV             ON  CACHE BOOL "" FORCE)
add_subdirectory(glslang EXCLUDE_FROM_ALL)
foreach(t glslang SPIRV OSDependent MachineIndependent GenericCodeGen
          glslang-default-resource-limits OGLCompiler SPVRemapper)
    if(TARGET ${t})
        set_target_properties(${t} PROPERTIES FOLDER "${VENDOR_FOLDER}/glslang")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# SPIRV-Cross -- SPIR-V reflection, and SPIR-V -> GLSL for the OpenGL backend
# ---------------------------------------------------------------------------
set(SPIRV_CROSS_CLI            OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS   OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL    OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL     OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP     OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API   OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL    ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL    ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL   ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "" FORCE)
add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
foreach(t spirv-cross-core spirv-cross-glsl spirv-cross-util)
    if(TARGET ${t})
        set_target_properties(${t} PROPERTIES FOLDER "${VENDOR_FOLDER}/SPIRV-Cross")
    endif()
endforeach()
