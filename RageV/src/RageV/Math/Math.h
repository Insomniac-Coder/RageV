#pragma once

// RageV's math. Include this; the split between Types.h and Functions.h is an
// implementation detail of how the header is organised, not something a caller
// should have to know.
//
// Nothing here includes a third-party header, and the engine does not link one
// for this: RageV implements its own vectors, matrices and quaternions. What
// keeps that honest is `scenetest`, which links glm purely as an oracle and
// checks every operation against it. See the note at the top of Functions.h.

#include "Types.h"
#include "Functions.h"
