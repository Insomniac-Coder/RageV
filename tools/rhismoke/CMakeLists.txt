add_executable(rhismoke main.cpp)
# No glfw here on purpose. It is linked privately into RageV.dll and keeps its
# state in globals, so linking it again gives this executable a second copy the
# engine cannot see -- a real window the DLL reports as a null HWND. The window
# comes from Window::Create instead.
target_link_libraries(rhismoke PRIVATE RageV::RageV)
add_custom_command(TARGET rhismoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets" "$<TARGET_FILE_DIR:rhismoke>/assets")
set_target_properties(rhismoke PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rhismoke")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rhismoke)
