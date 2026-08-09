file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(RageVRuntime ${RUNTIME_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/RageVRuntime.rc")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${RUNTIME_SOURCES})

target_link_libraries(RageVRuntime PRIVATE RageV::RageV)

# Only shaders and fonts. A game needs the renderer's own assets to start and
# nothing else from the editor -- its content comes from a project folder, which
# is the entire point of 4.1.
#
# The list is explicit rather than a copy of assets/, so that adding something
# to the editor's folder cannot quietly grow every shipped game.
add_custom_command(TARGET RageVRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets/shaders"
        "$<TARGET_FILE_DIR:RageVRuntime>/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets/Fonts"
        "$<TARGET_FILE_DIR:RageVRuntime>/assets/Fonts"
    # The window icon, which is separate from the one compiled into the
    # executable -- see WindowsWindow::SetWindowIcon. Without these a shipped
    # game has the right icon in Explorer and a blank one in alt-tab.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets/icon-32.png"
        "${CMAKE_SOURCE_DIR}/RageVEditor/assets/icon-48.png"
        "$<TARGET_FILE_DIR:RageVRuntime>/assets"
    COMMENT "Staging engine assets for the runtime")

# copy_if_different so a backend switch made in the deployed file survives the
# next build.
add_custom_command(TARGET RageVRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ragev.ini"
        "$<TARGET_FILE_DIR:RageVRuntime>/ragev.ini"
    COMMENT "Staging RageVRuntime config")

# Matches RageV: lets the optimiser inline glm through RageV/Math at the call
# sites in this target too. See RageV/CMakeLists.txt for why.
set_target_properties(RageVRuntime PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_DIST TRUE
    FOLDER "Applications"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/RageVRuntime"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RageVRuntime>")

# The C# script assembly, when the .NET SDK was present at configure time.
ragev_stage_managed(RageVRuntime)

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(RageVRuntime)
