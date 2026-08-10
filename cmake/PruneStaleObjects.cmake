# Deletes object files whose source no longer exists, and the exports.def
# generated from them.
#
# Why this exists: deleting a source file leaves its .obj in the objects
# directory, and the DLL's exports.def is only regenerated when the .def
# itself is out of date -- a *removed* input does not age it. The link then
# fails on exported symbols from a file that no longer exists, which reads as
# madness until you know this. Removing a script from the engine is a routine
# act now, so the cleanup is automatic rather than folklore.
#
# Runs PRE_BUILD: the stale objects and the .def go before compilation, and
# the def rule regenerates from the surviving objects because its output is
# missing.
#
# Arguments: -DOBJECT_DIR=<RageV.dir/Config> -DEXPECTED=<file of obj names>

if(NOT EXISTS "${OBJECT_DIR}" OR NOT EXISTS "${EXPECTED}")
    return()
endif()

file(STRINGS "${EXPECTED}" RV_EXPECTED)
file(GLOB RV_PRESENT "${OBJECT_DIR}/*.obj")

set(RV_PRUNED FALSE)
foreach(RV_OBJECT ${RV_PRESENT})
    get_filename_component(RV_NAME "${RV_OBJECT}" NAME)
    list(FIND RV_EXPECTED "${RV_NAME}" RV_FOUND)
    if(RV_FOUND EQUAL -1)
        file(REMOVE "${RV_OBJECT}")
        message(STATUS "Pruned stale ${RV_NAME} (its source is gone)")
        set(RV_PRUNED TRUE)
    endif()
endforeach()

if(RV_PRUNED)
    file(REMOVE "${OBJECT_DIR}/exports.def")
endif()
