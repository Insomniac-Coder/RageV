// The one translation unit that compiles stb_truetype, matching how stb_image
// is dropped in beside it.
//
// Only tools/rvfont links this. See ../CMakeLists.txt.

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
