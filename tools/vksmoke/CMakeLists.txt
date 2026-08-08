add_executable(vksmoke main.cpp)
target_link_libraries(vksmoke PRIVATE RageV::RageV glfw)
add_custom_command(TARGET vksmoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets" "$<TARGET_FILE_DIR:vksmoke>/assets")
set_target_properties(vksmoke PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/vksmoke")
