#pragma once

// RageV's math. Include this; the split between Types.h and Functions.h is an
// implementation detail of how the header is organised, not something a caller
// should have to know.
//
// Nothing here includes a third-party header. glm is confined to Math.cpp and
// to GlmBridge.h, which is internal — see the note at the top of Types.h for
// why an alias would not have been good enough.

#include "Types.h"
#include "Functions.h"
