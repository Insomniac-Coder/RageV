add_executable(scenetest main.cpp)
target_link_libraries(scenetest PRIVATE RageV::RageV glfw)
# Renderer::Init compiles the 2D and 3D shaders, so the assets have to be here.
add_custom_command(TARGET scenetest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets" "$<TARGET_FILE_DIR:scenetest>/assets")
set_target_properties(scenetest PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/scenetest")
