// Names for the water dials, which live in ObjectData -- see
// include/scene_block.glsl for why they are in that block and not one of their
// own. No #version here: an include is spliced into a file that already has
// one, and it must follow scene_block.glsl.
//
// **Only the vertex stage reads these.** The fragment is handed what it needs
// as flat varyings instead, so it never declares a push-constant block of its
// own. Two declarations of one block that have to agree byte for byte is a
// trap with no upside: they drift, and what it looks like when they do is a
// colour dial that moves the foam.

#define RV_WATER_SHALLOW      u_Object.WaterShallow.rgb
#define RV_WATER_GRADIENT     u_Object.WaterShallow.a
#define RV_WATER_DEEP         u_Object.WaterDeep.rgb
#define RV_WATER_TIME         u_Object.WaterDeep.a
#define RV_WATER_HEIGHT       u_Object.WaterWave.x
#define RV_WATER_LENGTH       u_Object.WaterWave.y
#define RV_WATER_CHOPPINESS   u_Object.WaterWave.z
#define RV_WATER_SPEED        u_Object.WaterWave.w
#define RV_WATER_DIRECTION    u_Object.WaterExtra.x
#define RV_WATER_FOAM         u_Object.WaterExtra.y
#define RV_WATER_PREV_TIME    u_Object.WaterExtra.z
#define RV_WATER_SPACING      u_Object.WaterExtra.w

// The body's rectangle, metres across X and along Z -- what turns a local
// position into the 0..1 coordinate the foam buffer is addressed by.
#define RV_WATER_SIZE         u_Object.WaterSize.xy

// One float carrying two answers, filled by the renderer and not the scene:
// zero means the see-through backdrop is not bound this pass and the fragment
// falls back to plain blending; non-zero means it is, and the *sign* is the
// backend's -- what turns an NDC y-offset into a texture-row offset, which
// Vulkan and OpenGL disagree about. Packed this way because the two facts
// travel together: a refraction offset is only ever applied to a backdrop
// that exists.
#define RV_WATER_FLAGS        u_Object.WaterSize.z

// **WR-16 S4b: the sea's lamps were shaded somewhere else.** Non-zero means
// the two pictures the lamp passes wrote are bound and hold this pixel's
// lamp light -- what scattered into the water and what glinted off it -- so
// the loop here walks the sun alone. Zero puts the walk back, which is what
// every path but the sea's does anyway.
#define RV_WATER_LAMPS        u_Object.WaterSize.w
