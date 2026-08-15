add_executable(scenetest main.cpp)

# The declaration-site marker fixture, run through the *real* rvgen at build
# time and compiled into this executable. That is the end-to-end proof of the
# generator -- marker to scan to wrapper TU to a registration that exists at
# runtime -- without needing a game module build. TemplateProbe.cpp in the
# engine is the other half of the pair: the same markers with no generator
# must register nothing.
set(RVGEN_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rvgen/MarkerProbe.cpp")
set(RVGEN_FIXTURE_WRAPPER "${CMAKE_CURRENT_BINARY_DIR}/rvgen/MarkerProbe.cpp.rvgen.cpp")
add_custom_command(
    OUTPUT "${RVGEN_FIXTURE_WRAPPER}"
    COMMAND $<TARGET_FILE:rvgen>
        --source "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rvgen"
        --out "${CMAKE_CURRENT_BINARY_DIR}/rvgen"
    DEPENDS rvgen "${RVGEN_FIXTURE}"
    COMMENT "rvgen over the scenetest fixture")
target_sources(scenetest PRIVATE "${RVGEN_FIXTURE_WRAPPER}")
# glm, only here. The engine does not link it; this suite checks RageV::Math
# against it operation by operation, which is what makes owning that maths
# defensible. See GlmBridge.h.
#
# Not glfw. A second copy of GLFW in this executable has its own globals, and
# the engine DLL cannot see a window created against them -- see RunTests.
target_link_libraries(scenetest PRIVATE RageV::RageV glm)
# Renderer::Init compiles the 2D and 3D shaders, so the assets have to be here.
# The skinned test model comes with them: the import checks read it from beside
# the executable like everything else, and a test that skips because its input
# is missing is worse than one that is not there at all.
add_custom_command(TARGET scenetest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets" "$<TARGET_FILE_DIR:scenetest>/assets")
set_target_properties(scenetest PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/scenetest")

# The C# script assembly, when the .NET SDK was present at configure time.
ragev_stage_managed(scenetest)

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(scenetest)
