file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(RageVEditor ${EDITOR_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/RageVEditor.rc")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${EDITOR_SOURCES})

target_link_libraries(RageVEditor PRIVATE RageV::RageV)

# The editor resolves asset paths relative to the working directory, so mirror
# assets/ next to the executable and point the debugger at it.
add_custom_command(TARGET RageVEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:RageVEditor>/assets"
    COMMENT "Staging RageVEditor assets")

# copy_if_different so a backend switch made in the deployed ragev.ini is not
# reverted by the next build.
add_custom_command(TARGET RageVEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ragev.ini"
        "$<TARGET_FILE_DIR:RageVEditor>/ragev.ini"
    COMMENT "Staging RageVEditor config")

# Each app gets its own directory. They ship different copies of assets/ and a
# shared output directory let whichever built last overwrite the other's.
# Matches RageV: lets the optimiser inline glm through RageV/Math at the call
# sites in this target too. See RageV/CMakeLists.txt for why.
set_target_properties(RageVEditor PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_DIST TRUE
    FOLDER "Applications"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/RageVEditor"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RageVEditor>")

# The C# script assembly, when the .NET SDK was present at configure time.
ragev_stage_managed(RageVEditor)
