# No asset staging and no window: packaging is file copying and one YAML
# rewrite, so it needs neither a device nor the renderer's shaders.
add_executable(rvpack main.cpp)
target_link_libraries(rvpack PRIVATE RageV::RageV glfw)
set_target_properties(rvpack PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rvpack")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rvpack)
