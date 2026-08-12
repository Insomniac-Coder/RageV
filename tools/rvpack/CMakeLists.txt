# No asset staging and no window: packaging is file copying and one YAML
# rewrite, so it needs neither a device nor the renderer's shaders.
add_executable(rvpack main.cpp)
target_link_libraries(rvpack PRIVATE RageV::RageV glfw)
set_target_properties(rvpack PROPERTIES
    FOLDER "Tools"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/rvpack")

# RageV.dll, beside this executable. See RageV/CMakeLists.txt.
ragev_stage_engine(rvpack)

# RageV.ScriptCore.dll in managed/, which packaging genuinely needs.
#
# This looks like it contradicts the note above -- packaging *is* file copying
# -- and it did until the packager started validating button bindings. Deciding
# whether a C# method exists means asking the .NET side, and Interop::Init
# resolves "managed/RageV.ScriptCore.dll" relative to the working directory.
#
# Without this, rvpack cannot boot .NET, cannot see a single C# method, and
# reports every working C# binding as dead. A check that refuses to package a
# game whose buttons all work is worse than no check.
ragev_stage_managed(rvpack)
