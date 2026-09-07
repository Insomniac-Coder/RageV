"""RT-first T2 + T3.
T2: the shadow-map fields (distance, split lambda, normal offset, cascades,
resolution) are greyed with a note while shadows are ray-traced, instead of
hidden -- SSR, SSGI/voxel GI and SSAO already grey through their
"TakesOver" predicates; the RT fields keep their existing visibility.
T3: the instance record gains a fourth vec4, `Extra`, whose x is the
object's id (the record's index + 1) for every stage -- static, skinned,
layered, water -- instead of borrowing `Indices.x`, which the skinned stage
uses as its bone base. The vertex stages carry it as v_ObjectId."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new, count=1):
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old[:70]); return s.replace(o, n)

# ------------------------------------------------------------------- T2
p = 'RageV/src/RageV/Scene/ComponentRegistry.cpp'; s, nl = load(p)
s = rep(s, nl, """		bool UsesCascades(const void* block)
		{""", """		// The note the shadow-map dials carry while shadows are ray-traced
		// (RT-first T2, 2026-09-06): greyed, not hidden -- the owner's call.
		constexpr const char* kCascadesUnderRays =
			"Shadows are ray-traced while ray tracing is on; the cascades are not drawn.";

		bool UsesCascades(const void* block)
		{""")
for name in ('Distance', 'Split lambda', 'Normal offset', 'Cascades'):
    old = 'Named("%s", OnlyWhen(UsesCascades,' % name
    assert s.count(old) == 1, name
    i = s.index(old)
    j = s.index(')))),', i)
    s = s[:i] + 'Named("%s", OnlyWhen(CastsShadows, DisabledWhen(RayTracingOn, kCascadesUnderRays,' % name + s[i + len(old):j] + ')))))' + s[j + len(')))),') - 1:]
old = 'Named("Resolution", OnlyWhen(CastsShadows,'
assert s.count(old) == 1
i = s.index(old); j = s.index(')))),', i)
s = s[:i] + 'Named("Resolution", OnlyWhen(CastsShadows, DisabledWhen(RayTracingOn, kCascadesUnderRays,' + s[i + len(old):j] + ')))))' + s[j + len(')))),') - 1:]
save(p, s)

# ------------------------------------------------------------------- T3
p = 'RageVEditor/assets/shaders/include/scene_block.glsl'; s, nl = load(p)
s = rep(s, nl, """	vec4 Indices;
};""", """	vec4 Indices;
	// x: the object's id (the record's index + 1) for the G-buffer, every
	// stage alike (RT-first T3); y, z, w spare.
	vec4 Extra;
};"""); save(p, s)

p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
s = rep(s, nl, """			Vec4 Indices{ 0.0f };
		};
		static_assert(sizeof(InstanceData) == 256,
					  "Must match InstanceData in include/scene_vertex.glsl");""",
"""			Vec4 Indices{ 0.0f };
			// x: the object's id, the record's index + 1, for the G-buffer's
			// surface id (RT-first T3). Every fill sets it; y, z, w spare.
			Vec4 Extra{ 0.0f };
		};
		static_assert(sizeof(InstanceData) == 272,
					  "Must match InstanceData in include/scene_block.glsl");""")
s = rep(s, nl, """		// x: the object's id for the G-buffer (RT-first step 1a): this record's
		// index plus one, stable for as long as the scene's draw order is.
		instance.Indices = { (float)(index + 1), (float)probe, record, isStatic ? 1.0f : 0.0f };""",
"""		instance.Indices = { 0.0f, (float)probe, record, isStatic ? 1.0f : 0.0f };
		// The object's id for the G-buffer (RT-first T3): this record's index
		// plus one, stable for as long as the scene's draw order is.
		instance.Extra = { (float)(index + 1), 0.0f, 0.0f, 0.0f };""")
for line in ("""		instance.Indices = { 0.0f, (float)probe, 0.0f, isStatic ? 1.0f : 0.0f };""",
             """		instance.Indices = { 0.0f, (float)probe, 0.0f, 0.0f };""",
             """		instance.Indices = { (float)base, (float)probe, 0.0f, (float)prevBase };"""):
    n = s.count(line.replace('\n', nl))
    assert n >= 1, line
    s = s.replace(line.replace('\n', nl), line.replace('\n', nl) + nl +
                  "\t\tinstance.Extra = { (float)(&instance - s_Data->Instances.data() + 1), 0.0f, 0.0f, 0.0f };")
save(p, s)

p = 'RageVEditor/assets/shaders/include/static_vertex.glsl'; s, nl = load(p)
s = rep(s, nl, """	v_ObjectId      = instance.Indices.x;""", """	v_ObjectId      = instance.Extra.x;"""); save(p, s)
p = 'RageVEditor/assets/shaders/pbr_skinned.rvshader'; s, nl = load(p)
s = rep(s, nl, """	v_ObjectId      = 0.0;""", """	v_ObjectId      = instance.Extra.x;"""); save(p, s)
p = 'RageVEditor/assets/shaders/include/water_vertex.glsl'; s, nl = load(p)
s = rep(s, nl, """	v_ObjectId      = 0.0;""", """	v_ObjectId      = instance.Extra.x;"""); save(p, s)
print('T2 + T3 patched')
