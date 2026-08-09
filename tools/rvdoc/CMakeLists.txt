add_executable(rvdoc main.cpp)
target_link_libraries(rvdoc PRIVATE RageV::RageV)
set_target_properties(rvdoc PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rvdoc")

# Unlike the other tools, rvdoc has a fixed input and output, so there is no
# reason to make anyone remember them. Building this target regenerates the
# manual in place.
add_custom_target(manual
    COMMAND rvdoc --in "${CMAKE_SOURCE_DIR}/docs/manual" --out "${CMAKE_SOURCE_DIR}/docs/site"
    DEPENDS rvdoc
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating the developer manual"
    VERBATIM)
set_target_properties(manual PROPERTIES FOLDER "Tools")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rvdoc)
