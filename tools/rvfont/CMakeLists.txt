add_executable(rvfont main.cpp)

# stb_truetype for outlines, msdfgen for the field. **Neither is linked by the
# engine** -- see RageV/vendor/CMakeLists.txt. This tool bakes an atlas offline
# and the runtime loads a PNG and a table of numbers, so a shipped game carries
# no font parser and no distance-field generator.
target_link_libraries(rvfont PRIVATE RageV::RageV stb_truetype msdfgen stb yaml-cpp)

set_target_properties(rvfont PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rvfont")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rvfont)
