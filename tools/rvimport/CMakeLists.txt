# A device, because meshes upload as they are built -- but no shaders and no
# assets staged beside it: nothing here draws, and the materials it writes are
# files rather than GPU objects.
add_executable(rvimport main.cpp)
target_link_libraries(rvimport PRIVATE RageV::RageV glfw)
set_target_properties(rvimport PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rvimport")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rvimport)
