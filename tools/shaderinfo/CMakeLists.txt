add_executable(shaderinfo main.cpp)
target_link_libraries(shaderinfo PRIVATE RageV::RageV)
add_custom_command(TARGET shaderinfo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets" "$<TARGET_FILE_DIR:shaderinfo>/assets")
set_target_properties(shaderinfo PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaderinfo")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(shaderinfo)
