# rvgen scans a project's Source/ for declaration-site script markers and
# writes the registration code they describe. Pure file work: no window, no
# device, no assets -- the smallest tool here.
add_executable(rvgen main.cpp)
target_link_libraries(rvgen PRIVATE RageV::RageV)
set_target_properties(rvgen PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rvgen")

# RageV.dll beside the executable -- ScriptGen lives in the engine so
# scenetest can exercise the same scanner in-process.
ragev_stage_engine(rvgen)
