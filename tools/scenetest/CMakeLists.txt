add_executable(scenetest main.cpp)
# glm, only here. The engine does not link it; this suite checks RageV::Math
# against it operation by operation, which is what makes owning that maths
# defensible. See GlmBridge.h.
target_link_libraries(scenetest PRIVATE RageV::RageV glfw glm)
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
