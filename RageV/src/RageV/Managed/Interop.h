#pragma once

// The boundary between the engine and managed code.
//
// Two directions, and they use different mechanisms on purpose.
//
// **Managed calling native: a table of function pointers, handed over once.**
// Not `[DllImport]`. P/Invoke by name needs the native symbols exported from a
// shared library, and this engine is a static library linked into an executable
// -- there is no `RageV.dll` to import from, and inventing one so that C# can
// find `Entity_SetPosition` would be a build-system change to work around a
// choice nobody made. A table also skips the marshalling stub P/Invoke
// generates per call, which matters when a script touches a transform every
// step of every entity.
//
// **Native calling managed: `[UnmanagedCallersOnly]` entry points**, reached by
// the function pointers hostfxr hands back. Those methods must be static, take
// only blittable arguments, and must not let an exception escape -- an
// exception crossing an unmanaged frame terminates the process rather than
// unwinding, so every entry point on the managed side catches at its edge.
//
// **Everything crossing is blittable.** An entity is a UUID, not a pointer:
// play mode restores a scene by recreating entities, so a pointer handed to a
// script would dangle the moment Stop is pressed. Vectors are three floats with
// a matching [StructLayout(LayoutKind.Sequential)] on the other side. Strings
// are UTF-8 pointers, copied at the boundary in whichever direction they go,
// because a managed string's storage may move and a std::string's certainly
// does not survive the call.
//
// If anything in NativeApi changes shape -- an argument, an order, a struct
// layout -- bump Interop.ProtocolVersion on both sides. That check is the only
// thing standing between a partial rebuild and a stack corruption somewhere
// unrelated, minutes later.

#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"

#include <cstdint>
#include <filesystem>

namespace RageV
{
	class Scene;
}

namespace RageV::Managed
{
	// The functions managed code may call, in a fixed order.
	//
	// Layout is ABI. Adding to the end is the only safe edit; inserting in the
	// middle silently rebinds every field after it on one side only.
	struct RayHitData;

	struct NativeApi
	{
		// --- diagnostics -----------------------------------------------------
		// level: 0 trace, 1 info, 2 warn, 3 error. Message is UTF-8.
		void (__cdecl* Log)(int32_t level, const char* message);

		// --- entities --------------------------------------------------------
		// Zero when nothing matches, which is also the invalid entity.
		uint64_t (__cdecl* FindEntityByName)(const char* name);
		int32_t  (__cdecl* EntityExists)(uint64_t entity);

		// Writes at most `capacity` bytes including the terminator, and returns
		// the length that *would* have been written -- so a caller that got
		// truncated can tell, rather than silently shipping a clipped name.
		int32_t  (__cdecl* GetEntityName)(uint64_t entity, char* buffer, int32_t capacity);

		// --- transform -------------------------------------------------------
		// Local, relative to the parent, matching the native script API.
		int32_t (__cdecl* GetPosition)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* SetPosition)(uint64_t entity, const Vec3* value);
		int32_t (__cdecl* GetRotation)(uint64_t entity, Vec3* out);   // radians
		int32_t (__cdecl* SetRotation)(uint64_t entity, const Vec3* value);
		int32_t (__cdecl* GetScale)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* SetScale)(uint64_t entity, const Vec3* value);

		// --- input -----------------------------------------------------------
		// By action name, never by key code -- the same rule the native API has.
		int32_t (__cdecl* IsActionDown)(const char* action);
		int32_t (__cdecl* WasActionPressed)(const char* action);
		float   (__cdecl* GetAxis)(const char* axis);

		// --- time ------------------------------------------------------------
		float (__cdecl* GetFixedDeltaTime)();
		float (__cdecl* GetTime)();

		// --- appended for protocol 2 -----------------------------------------
		//
		// Appended, not inserted. Every field above keeps its offset, which is
		// the only reason a version bump is a readable error rather than a call
		// through a pointer to the wrong function.
		int32_t (__cdecl* WasActionReleased)(const char* action);

		int32_t (__cdecl* GetWorldPosition)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* GetForward)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* GetRight)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* GetUp)(uint64_t entity, Vec3* out);

		uint64_t (__cdecl* Spawn)(const char* name);
		// Deferred to the end of the step, exactly as the native API is: a
		// script destroying itself mid-update would delete the object running.
		void (__cdecl* Destroy)(uint64_t entity);

		// All no-ops on an entity with no rigid body, and outside play mode.
		void (__cdecl* AddForce)(uint64_t entity, const Vec3* force);
		void (__cdecl* AddImpulse)(uint64_t entity, const Vec3* impulse);
		void (__cdecl* SetLinearVelocity)(uint64_t entity, const Vec3* velocity);
		int32_t (__cdecl* GetLinearVelocity)(uint64_t entity, Vec3* out);

		// --- appended for protocol 4: the rest of the native surface ---------
		//
		// Everything a C++ script can call, minus the component templates,
		// which have no cross-boundary shape -- script fields are the
		// cross-language mechanism for tuning, and the guides say so.
		//
		// Assets cross as *paths* relative to the asset root, resolved through
		// the registry, because AssetHandle is an opaque number a C# script
		// has no way to obtain.

		int32_t  (__cdecl* SetEntityName)(uint64_t entity, const char* name);
		void     (__cdecl* LookAt)(uint64_t entity, const Vec3* target, const Vec3* up);

		// UUIDs into the buffer, count-that-would-fit returned -- the
		// GetEntityName contract, for entities.
		int32_t  (__cdecl* FindEntitiesByName)(const char* name, uint64_t* buffer, int32_t capacity);
		uint64_t (__cdecl* GetParent)(uint64_t entity);
		// parent == 0 clears, moving the entity to the root.
		void     (__cdecl* SetParent)(uint64_t entity, uint64_t parent);
		int32_t  (__cdecl* GetChildren)(uint64_t entity, uint64_t* buffer, int32_t capacity);

		uint64_t (__cdecl* SpawnPrefab)(const char* assetPath);

		// Returns whether it hit; the details land in `out`. Direction need
		// not be normalised -- the ray extends to its length, as the native
		// API's does.
		int32_t  (__cdecl* Raycast)(const Vec3* origin, const Vec3* direction, RayHitData* out);

		uint64_t (__cdecl* PlaySource)(uint64_t entity);
		void     (__cdecl* StopSource)(uint64_t entity);
		int32_t  (__cdecl* IsSourcePlaying)(uint64_t entity);

		// An empty clip path plays the entity's own AudioSourceComponent clip
		// as a one-shot, which is the ImpactSound pattern without needing
		// component access.
		uint64_t (__cdecl* PlayOneShot)(uint64_t entity, const char* clipPath, float volume);
		uint64_t (__cdecl* PlayOneShot2D)(const char* clipPath, float volume);
		void     (__cdecl* StopVoice)(uint64_t voice);

		// Components, by their registry names, with field values as text --
		// the same registry and the same text forms the inspector and the
		// scene file use. This is the boundary's answer to GetComponent<T>:
		// a template cannot cross, a name-and-text contract can, and because
		// the ComponentRegistry drives it, a component gaining a field is
		// visible from C# without anyone updating a binding.
		int32_t (__cdecl* HasComponent)(uint64_t entity, const char* component);
		int32_t (__cdecl* AddComponent)(uint64_t entity, const char* component);
		int32_t (__cdecl* RemoveComponent)(uint64_t entity, const char* component);
		// Length-that-would-fit, like GetEntityName; -1 when the entity,
		// component or field does not exist -- distinguishable from an empty
		// string field, which returns 0.
		int32_t (__cdecl* GetComponentField)(uint64_t entity, const char* component,
											 const char* field, char* buffer, int32_t capacity);
		int32_t (__cdecl* SetComponentField)(uint64_t entity, const char* component,
											 const char* field, const char* value);

		// --- appended for protocol 5: one-shots with pitch, and from a point --
		//
		// New entries rather than new parameters on the old ones: the table is
		// append-only, and the protocol-4 entries keep their exact shape. The
		// C# wrappers route through these three; the old two stay as the ABI
		// they already are.

		// From an arbitrary point -- a particle burst, a ricochet, somewhere
		// no entity stands. Pitch 1 plays the clip as recorded.
		uint64_t (__cdecl* PlayOneShotAt)(const char* clipPath, const Vec3* position,
										  float volume, float pitch);
		// The protocol-4 pair, carrying pitch. The empty-path contract on the
		// entity variant is unchanged: it plays the entity's own source clip.
		uint64_t (__cdecl* PlayOneShotPitched)(uint64_t entity, const char* clipPath,
											   float volume, float pitch);
		uint64_t (__cdecl* PlayOneShot2DPitched)(const char* clipPath, float volume, float pitch);

		// --- appended for protocol 6: the per-frame hook ---------------------

		// How far this frame falls between the last simulation step and the
		// next, 0 to 1. Already applied to simulated bodies before OnFrame
		// runs; this is for smoothing something the engine cannot see.
		float (__cdecl* GetInterpolationAlpha)();

		// --- appended for protocol 7: the game's UI --------------------------
		//
		// Only what the component bridge cannot already do. Setting a colour is
		// absent on purpose: a UI entity has two of them, the image's and the
		// text's, so one "SetUITint" would have to pick and would be wrong half
		// the time -- SetComponentField(e, "UIImageComponent", "Color", ...)
		// says which, and already works.

		// The UITextComponent's string. Zero when the entity has no such
		// component, which is the SetEntityName contract.
		int32_t (__cdecl* SetUIText)(uint64_t entity, const char* text);
		// Length-that-would-fit, the GetEntityName contract: -1 without the
		// component, which stays distinguishable from a label that is
		// legitimately empty and returns 0.
		int32_t (__cdecl* GetUIText)(uint64_t entity, char* buffer, int32_t capacity);

		// A completed press on this entity's UIButtonComponent, true for one
		// simulation step. Not a component field, because it describes what the
		// pointer did rather than what the author chose, and a field is a thing
		// the scene file stores.
		int32_t (__cdecl* WasUIButtonClicked)(uint64_t entity);

		// Whether the UI has the pointer. **The one every game needs**: without
		// it, clicking a button also fires the weapon behind it.
		int32_t (__cdecl* IsPointerOverUI)();

		// --- appended for protocol 8: the scene's render settings ------------
		//
		// By name and as text, exactly like the component bridge and for the
		// same reason: `SceneEnvironment` is a struct, and a struct cannot
		// cross this boundary while a name and a piece of text can. A C++
		// script never needed this -- it can already write
		// `GetScene().GetEnvironment()` -- so this pair is what makes the two
		// languages equal on the render settings.
		//
		// Driven by `RenderSettingsRegistry`, so a setting added there reaches
		// C# without a new entry here. The names are the scene file's own
		// keys, so what a script writes and what a save contains cannot drift.
		//
		// Values take effect on the next frame the graph is built, which is
		// the same frame for anything set in OnTick or OnFrame. They are *not*
		// written back to the scene file: this is a runtime override, and a
		// game turning its own bloom down should not silently edit the asset.

		// Length-that-would-fit, the GetEntityName contract; -1 for a name
		// that is not a setting, which stays distinguishable from a setting
		// whose value is legitimately the empty string.
		int32_t (__cdecl* GetRenderSetting)(const char* name, char* buffer, int32_t capacity);
		int32_t (__cdecl* SetRenderSetting)(const char* name, const char* value);
	};

	// One raycast hit, as it crosses the boundary. Mirrors RageV::RayHit,
	// flattened the same way CollisionData is; whether anything was hit
	// travels as the call's return value rather than a bool with an
	// unguaranteed layout.
	struct RayHitData
	{
		uint64_t Entity;
		Vec3     Position;
		Vec3     Normal;
		float    Distance;
	};

	// One contact, as it crosses the boundary.
	//
	// Mirrors RageV::Collision, flattened: Entity becomes a UUID and the bool
	// becomes an int32, because neither has a guaranteed layout across the
	// boundary and `bool` in particular is one byte here and four somewhere
	// else depending on who is asking.
	struct CollisionData
	{
		uint64_t Other;
		int32_t  Trigger;
		Vec3     Point;
		Vec3     Normal;
		float    ImpactSpeed;
	};

	// Which callback a contact is for. Matches the managed enum.
	enum class ContactKind : int32_t
	{
		CollisionEnter = 0,
		CollisionStay  = 1,
		CollisionExit  = 2,
		TriggerEnter   = 3,
		TriggerStay    = 4,
		TriggerExit    = 5,
	};

	// The managed script lifecycle, as function pointers into the assembly.
	//
	// Separate from NativeApi because it runs the other way: these are entry
	// points the engine calls, bound once at Init.
	struct ManagedApi
	{
		// Returns a handle, or 0 when the type does not exist or does not
		// derive from Script. Never throws across the boundary.
		int32_t (__cdecl* Create)(const char* typeName, uint64_t entity);
		void    (__cdecl* Destroy)(int32_t handle);

		void (__cdecl* InvokeCreate)(int32_t handle);

		// The two rates. Tick is the fixed simulation step, Frame is the frame
		// -- and unlike NativeApi, this table is not append-only: nothing
		// outside the engine binds it, and the engine and RageV.ScriptCore
		// always ship together. Renaming InvokeUpdate to InvokeTick here costs
		// nothing that renaming it in the class library did not already cost.
		void (__cdecl* InvokeTick)(int32_t handle, float deltaTime);
		void (__cdecl* InvokeFrame)(int32_t handle, float deltaTime);

		void (__cdecl* InvokeDestroy)(int32_t handle);
		void (__cdecl* InvokeContact)(int32_t handle, int32_t kind, const CollisionData* contact);

		// How many script instances are alive. For leak checks -- a handle that
		// is never released is a script that never stops running.
		int32_t (__cdecl* LiveCount)();

		// Loads a project's compiled script assembly. Returns how many Script
		// subclasses it holds, or -1 on failure -- "it loaded" and "it has any
		// scripts in it" being different answers, and the second the one the
		// person who just pressed Build wants.
		int32_t (__cdecl* LoadAssembly)(const char* path);

		// Every loadable script type, newline-separated, with the same
		// length-that-would-have-fit contract GetEntityName uses.
		int32_t (__cdecl* ListScriptTypes)(char* buffer, int32_t capacity);

		// --- appended for protocol 3: script fields --------------------------
		//
		// Values cross as text, in the invariant culture. The scene file is text
		// and the inspector edits text-like values anyway, so a tagged union
		// would be two languages' worth of upkeep to avoid a float parse on an
		// operation that happens when somebody types, not per step.

		// How many editable fields a script type has, or -1 if it is unknown.
		int32_t (__cdecl* GetFieldCount)(const char* typeName);

		// Writes "name\ndefault" into the buffer and returns the field's type
		// as a ScriptFieldType. Two strings in one call, because the inspector
		// reads this once when a script is chosen.
		int32_t (__cdecl* DescribeField)(const char* typeName, int32_t index,
										 char* buffer, int32_t capacity);

		int32_t (__cdecl* GetFieldValue)(int32_t handle, const char* name,
										 char* buffer, int32_t capacity);
		int32_t (__cdecl* SetFieldValue)(int32_t handle, const char* name, const char* value);

		// --- methods a scene may name ----------------------------------------
		//
		// The C# half of what ScriptRegistry::Method does for C++. There is no
		// managed equivalent of the registration, because reflection does not
		// need one -- which is why this is two entries and not three.

		// Bindable method names on a *type*, newline-separated, with the
		// GetEntityName contract. From the type rather than an instance
		// because the inspector asks while the scene is stopped.
		int32_t (__cdecl* ListMethods)(const char* typeName, char* buffer, int32_t capacity);

		// Calls one on a live instance. 1 if it ran.
		int32_t (__cdecl* InvokeMethod)(int32_t handle, const char* name);
	};

	// What kind of thing a script field is. Matches RageV.ScriptFieldType.
	enum class ScriptFieldType : int32_t
	{
		Unsupported = -1,
		Bool = 0,
		Int = 1,
		Float = 2,
		String = 3,
		Vector3 = 4,
	};

	// One editable field on a script type, as the inspector sees it.
	struct ScriptFieldDesc
	{
		std::string Name;
		std::string Default;
		ScriptFieldType Type = ScriptFieldType::Unsupported;
	};

	class Interop
	{
	public:
		// Boots the runtime if it is not already up, checks the protocol, and
		// hands the table over. False means C# is unavailable; ask
		// DotNetHost::GetUnavailableReason for a sentence fit to show a user.
		static bool Init(const std::filesystem::path& assembly);
		static void Shutdown();

		static bool IsReady();

		// Which scene the entity functions act on. Set when play mode starts
		// and cleared when it stops.
		//
		// A raw pointer rather than a shared_ptr on purpose: managed code must
		// not be able to keep a scene alive past Stop, and this is the one place
		// the lifetime rule can be stated.
		static void SetScene(Scene* scene);
		static Scene* GetScene();

		// The table, for tests and for anything that wants to call the same
		// functions managed code sees.
		static const NativeApi& Api();

		// The managed lifecycle entry points. Null members when not ready.
		static const ManagedApi& Managed();

		// The version both sides were built against.
		//
		// 1: the first table.
		// 2: appended physics, world transform and spawn/destroy; added the
		//    script lifecycle and CollisionData.
		// 3: appended script field reflection.
		// 4: appended the rest of the native surface -- hierarchy, prefabs,
		//    raycasts, audio, components by name.
		// 5: appended one-shots with pitch, and one from a point.
		// 6: scripts gained a second rate. OnUpdate became OnTick, OnFrame
		//    joined it, and the interpolation alpha became readable.
		// 7: the game's UI -- a label's text, a button's click, and whether the
		//    UI took the pointer.
		static constexpr int32_t kProtocolVersion = 8;

		// The editable fields of a script type, for the inspector. Empty when
		// C# is not running or the type is unknown -- both of which the
		// inspector shows rather than treating as an error.
		static std::vector<ScriptFieldDesc> DescribeFields(const std::string& typeName);
	};
}
