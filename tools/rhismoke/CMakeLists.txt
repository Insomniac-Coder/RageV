add_executable(rhismoke main.cpp)
target_link_libraries(rhismoke PRIVATE RageV::RageV glfw)
add_custom_command(TARGET rhismoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets" "$<TARGET_FILE_DIR:rhismoke>/assets")
set_target_properties(rhismoke PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rhismoke")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rhismoke)
