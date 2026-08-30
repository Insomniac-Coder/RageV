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
