// cgltf is a single-header library; this is the one translation unit that
// carries its implementation. Kept in vendor/ rather than in the engine so it
// is not subject to the engine's forced precompiled header -- that is what
// broke the stb translation units when this project was first revived.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
