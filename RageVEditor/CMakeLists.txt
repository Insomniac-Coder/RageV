file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(RageVEditor ${EDITOR_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${EDITOR_SOURCES})

target_link_libraries(RageVEditor PRIVATE RageV::RageV)

# The editor resolves asset paths relative to the working directory, so mirror
# assets/ next to the executable and point the debugger at it.
add_custom_command(TARGET RageVEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:RageVEditor>/assets"
    COMMENT "Staging RageVEditor assets")

# Each app gets its own directory. They ship different copies of assets/ and a
# shared output directory let whichever built last overwrite the other's.
set_target_properties(RageVEditor PROPERTIES
    FOLDER "Applications"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/RageVEditor"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RageVEditor>")
