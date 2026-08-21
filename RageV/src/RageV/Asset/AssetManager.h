#pragma once
#include "Asset.h"
#include "AssetRegistry.h"
#include "GltfImporter.h"
#include "RageV/Core/Boot.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Animation/Skeleton.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Renderer/PostSettings.h"
#include "MaterialSerializer.h"
#include "Curve.h"
#include "ScriptGraph.h"
#include "Font.h"
#include "LutRecipe.h"
#include "TerrainData.h"

// Declared in the enclosing namespace on purpose. Inside
// `namespace RageV::Assets` these would declare new types that nothing ever
// defines, and the error surfaces at the use site rather than here.
namespace RageV
{
	class Scene;
	class Entity;
}

namespace RageV::Assets
{

	// Turns handles into loaded assets, and caches the result.
	//
	// Split from Registry on purpose: the registry knows what exists and
	// needs no GPU, so a headless tool can inspect a project. The manager
	// creates GPU resources and needs a device.
	class Manager
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Null when the handle is unknown or the source will not load. Loading
		// is on demand and cached, so asking repeatedly is cheap.
		static RHI::Ref<Mesh> GetMesh(AssetHandle handle);

		// The skeleton and clips a model brought with it, or null.
		//
		// Cached beside the mesh and keyed the same way, because they come out
		// of the same parse and a second one to fetch them would be the file
		// read twice.
		static const Skeleton* GetSkeleton(AssetHandle handle);
		static const std::vector<Anim::Clip>* GetClips(AssetHandle handle);

		// Whether a texture's values are *colour* or *numbers*.
		//
		// A base colour or emissive map is a picture: it was authored in sRGB
		// and has to be decoded to linear before it is lit. A normal,
		// roughness, metallic or occlusion map is data that merely happens to
		// be stored in an image, and decoding it bends every value.
		//
		// Getting this wrong does not fail. A normal map read as sRGB has its
		// X and Y pulled toward the centre, so every surface goes *flat* --
		// which reads as "the normal maps are not being applied" rather than as
		// a colour-space mistake, and that is exactly how it was found.
		enum class ColorSpace { Srgb, Linear };

		// A plain 2D texture -- a particle sprite, a material map. Mipped, and
		// cached like everything else here; a failure caches as null so a
		// missing file is not retried per frame.
		//
		// Cached separately per colour space, because the same file read the
		// two ways is two different textures and one cache would hand back
		// whichever was asked for first.
		//
		// sRGB by default because the first caller was particle sprites, which
		// are pictures. Every material map except base colour and emissive
		// wants Linear.
		static RHI::Ref<RHI::RHITexture> GetTexture(AssetHandle handle,
													ColorSpace space = ColorSpace::Srgb);

		// A material, resolved from its `.rmat` and its five texture handles.
		//
		// Null for an invalid handle, and the caller draws with the renderer's
		// shared default -- which is what an entity with no material assigned
		// gets, so "unassigned" and "assigned something broken" reach the same
		// place by the same route rather than by two.
		//
		// Cached by handle like everything else here. The cache is what makes
		// sharing mean anything: two entities pointing at one `.rmat` get the
		// *same* `Material` object, and therefore the same batch key, and
		// therefore one draw. A per-entity copy would render identically and
		// batch not at all.
		static RHI::Ref<Material> GetMaterial(AssetHandle handle);

		// Writes a `.rmat` beside the other assets and returns its handle.
		static AssetHandle CreateMaterial(const MaterialDesc& material,
										  const std::filesystem::path& relativePath);

		// Drops the cached material so the next GetMaterial rebuilds it from
		// the file. What the inspector calls after an edit -- without it a
		// changed `.rmat` keeps rendering as the old one, which is the same
		// stale-cache shape ReloadCurve exists for.
		static void ReloadMaterial(AssetHandle handle);

		// A keyed ramp -- size or alpha over a particle's life, or the colour
		// gradient. Never null for a valid handle: a missing or unreadable file
		// answers an *empty* curve, which evaluates to its fallback everywhere,
		// so a caller reads a value rather than remembering a branch. Cached,
		// including the failure, so a broken path is not reopened per frame.
		//
		// The pointer is owned by the cache and stays valid until ReloadCurve
		// or a project change, which is long enough to sample from and too
		// short to store.
		static const Curve* GetCurve(AssetHandle handle);

		// Writes a `.rcurve` beside the other assets and returns its handle.
		static AssetHandle CreateCurve(const Curve& curve,
									   const std::filesystem::path& relativePath);

		// The same curve as a flat table, which is what the simulation samples
		// and what the GPU path will upload. Cached beside the curve and
		// invalidated with it, so there is one place a stale ramp can come
		// from rather than two. Never null for a valid handle, same contract.
		static const Curve::Baked* GetBakedCurve(AssetHandle handle);

		// Drops the cached copy so the next GetCurve reads the file again.
		// What the editor calls when somebody finishes dragging a point --
		// without it, an edited curve keeps rendering as the old shape.
		static void ReloadCurve(AssetHandle handle);

		// How the camera naming this profile grades its frame.
		//
		// **By value, and it never fails.** An invalid handle, an unknown one
		// and a file that will not parse all answer the neutral grade -- the
		// same thing a camera with no profile renders. That is the one design
		// decision in this function: a null return would put the "and what if
		// there is no profile?" branch at every call site, where the editor's
		// two viewports, the runtime and the probe capture would each have to
		// keep their copy of it identical.
		//
		// No device needed. A grade is six numbers, so the suite can exercise
		// profiles headlessly.
		static PostSettings GetPostSettings(AssetHandle handle);

		// The same profile, *writable*, or null when the handle names none.
		//
		// This is the cached copy the renderer reads, so a write lands on the
		// next frame -- which is what a script dimming exposure for a flashbang
		// wants, and what the inspector's live preview is. It is deliberately
		// **not** written to disk: saving is a separate act, so a game altering
		// its own grade at runtime does not silently edit the asset.
		//
		// The pointer is owned by the cache and stays valid until
		// ReloadPostProfile or a project change -- long enough to write
		// through, too short to store.
		static PostSettings* GetPostProfile(AssetHandle handle);

		// The visual script behind a handle (8.10, ENGINE-NOTES 7bh).
		//
		// Owned by the cache and valid until ReloadScriptGraph or a project
		// change, like GetCurve -- but **null when the graph will not load**,
		// which is the opposite of what this said until 10.10.
		//
		// It used to answer a failed load with an empty graph and a valid
		// pointer, so that the panel opened on a blank canvas with the error
		// in the log. That is a blank canvas *over a file that has contents*,
		// and the first Save writes the blank back. `s_FontFailed` had the
		// right shape all along: a failure is an absent entry, because "empty"
		// and "unreadable" want different answers and an empty value cannot
		// say which it is.
		static const ScriptGraph* GetScriptGraph(AssetHandle handle);

		// Why the graph above would not load, for a caller that has to put it
		// in front of somebody. Empty when the handle loaded, or was never
		// asked for.
		static const std::string& GetScriptGraphError(AssetHandle handle);

		// The same file with everything this build cannot read taken out, and
		// `outMessage` saying what that was. **Deliberately not cached and
		// never written**: this is not what the file says, it is what the file
		// says minus something, and only the editor's "open without it" asks
		// for it -- after telling somebody the cost. It stays a graph in
		// memory until they press Save.
		static bool LoadScriptGraphWithoutUnknown(AssetHandle handle, ScriptGraph& out,
												  std::string* outMessage);

		// Writes a `.rvgraph` beside the other assets and returns its handle.
		static AssetHandle CreateScriptGraph(const ScriptGraph& graph,
											 const std::filesystem::path& relativePath);

		// Writes an already-known graph back to its own file. The panel's
		// save: unlike a curve, a graph is edited for minutes at a time and
		// the file is written on demand rather than after every drag.
		static bool SaveScriptGraph(AssetHandle handle, const ScriptGraph& graph);

		static void ReloadScriptGraph(AssetHandle handle);

		// Writes a `.rvpostprofile` beside the other assets and returns its
		// handle. What the camera's "New post profile..." does.
		static AssetHandle CreatePostProfile(const PostSettings& settings,
											 const std::filesystem::path& relativePath);

		// The colour-grading LUT a `.cube` describes, as a 3D texture.
		//
		// Null when the handle is unset, unknown, or the file will not parse --
		// and the failure is cached, because a profile pointing at a broken LUT
		// must not reopen the file sixty times a second. A null LUT grades
		// nothing, which is the same picture as no LUT at all: a malformed file
		// costs its look rather than the frame. ENGINE-NOTES 7t.
		// The 3D texture the tonemap samples. A `.cube` is parsed; a `.rvlut`
		// is baked from its recipe. Callers cannot tell, deliberately.
		static RHI::Ref<RHI::RHITexture> GetColorLut(AssetHandle handle);

		// The recipe behind a `.rvlut`, mutable and cached, for the inspector
		// to edit in place -- the same shape as GetPostProfile.
		//
		// **Null for a `.cube`**, which is not a failure: a baked table has no
		// recipe and the inspector shows what it can instead of pretending.
		static LutRecipe* GetLutRecipe(AssetHandle handle);

		// Writes a `.rvlut` and returns its handle, minted by the registry
		// after the file exists.
		static AssetHandle CreateLutRecipe(const LutRecipe& recipe,
										   const std::filesystem::path& relativePath);

		// Drops both the recipe and the baked texture, so the next frame
		// rebuilds from the file.
		static void ReloadColorLut(AssetHandle handle);

		// Drops the cached copy so the next GetPostSettings reads the file.
		// The inspector calls it after writing an edit through, which is what
		// makes a grade change visible on the next frame rather than the next
		// launch.
		static void ReloadPostProfile(AssetHandle handle);

		// Drops every cached profile.
		//
		// What the editor calls when play mode ends. A running game may have
		// written to a profile through GetPostProfile, and those writes are
		// runtime overrides -- the scene's own state is restored from the
		// snapshot on stop, and this is the same promise for the grade.
		static void ReloadAllPostProfiles();

		// A baked font: the metrics table from a `.rvfont`. Null when the
		// handle is unknown or the file will not load, and the failure is
		// cached, because text that cannot find its font must not reopen the
		// file once per frame.
		//
		// Unlike a curve, a failure is *not* papered over with an empty
		// default. A curve with no keys evaluates to a sensible fallback; a
		// font with no glyphs can only draw nothing, and silently drawing
		// nothing is how a missing font gets mistaken for a layout bug.
		//
		// No device needed -- this is numbers on the CPU, which is what lets
		// the test suite exercise layout headlessly. The atlas below is the
		// half that needs a GPU.
		static const Font* GetFont(AssetHandle handle);

		// The distance-field atlas that goes with it.
		//
		// **Linear and unmipped**, unlike every other 2D texture here. A
		// distance field is data: gamma-correcting it bends the distances so
		// the edge lands in the wrong place, and a mip chain averages
		// distances from opposite sides of a stroke into a value that means
		// nothing. Both mistakes render as text that is soft in a way no
		// amount of tuning the shader will fix.
		static RHI::Ref<RHI::RHITexture> GetFontAtlas(AssetHandle handle);

		// The heights of a terrain (ENGINE-NOTES 7ap), read once and kept:
		// numbers on the CPU, no device needed, so the suite can build chunk
		// geometry and sample heights headlessly. Null for a handle that is
		// not a readable `.rvterrain`, and remembered as such. Renderer/Terrain
		// builds the chunk meshes from this; the physics world builds the
		// height field from it; both read the same samples.
		static const TerrainData* GetTerrain(AssetHandle handle);

		// The same grid, mutable, for the brush (ENGINE-NOTES 7ar): the
		// authoritative copy every Terrain runtime is built from, so an edit
		// here survives the runtime being replaced. Marks the asset dirty;
		// SaveTerrain writes it back and re-indexes its sidecar, SaveDirtyTerrains
		// does so for every dirty one -- which the editor calls from its scene
		// save. Null for a handle that is not a readable terrain.
		static TerrainData* EditTerrain(AssetHandle handle);
		static bool IsTerrainDirty(AssetHandle handle);
		static bool SaveTerrain(AssetHandle handle);
		static void SaveDirtyTerrains();
		static bool HasDirtyTerrains();

		// An environment map, built from whatever image the handle names. The
		// conversion from a panorama is not cheap, so a failure is cached as
		// null too -- a scene pointing at a missing sky must not try again
		// sixty times a second.
		static RHI::Ref<RHI::RHITexture> GetCubemap(AssetHandle handle);

		// Its diffuse irradiance, convolved when the map was loaded.
		static RHI::Ref<RHI::RHITexture> GetIrradiance(AssetHandle handle);

		// The primitives, registered as virtual assets at Init.
		static AssetHandle GetPrimitiveHandle(PrimitiveType type);
		static bool IsPrimitive(AssetHandle handle, PrimitiveType& out);

		// Human-readable name for the inspector and content browser.
		static std::string GetDisplayName(AssetHandle handle);

		// Imports a model and builds the entity tree it describes, parented
		// under one root entity that carries the file's name. Returns the root.
		static Entity InstantiateModel(Scene& scene, AssetHandle handle);

		// Writes an entity and its descendants to a .rprefab beside the other
		// assets, and returns the new handle. The entity keeps its identity;
		// the file gets a copy.
		static AssetHandle CreatePrefab(Scene& scene, Entity root,
										const std::filesystem::path& relativePath);

		// Stamps out a copy with fresh ids. Returns the root.
		static Entity InstantiatePrefab(Scene& scene, AssetHandle handle);

		// Gets everything a scene refers to ready to load, reporting each one.
		//
		// **CPU only, and safe to call from the boot worker** -- it decodes
		// and cooks into the import cache but creates nothing on the GPU. The
		// device calls stay where they always were, on the main thread, and
		// are cheap by the time this has run: a read and an upload rather than
		// a read, an inflate and a mip build.
		//
		// That split is the whole reason loading can be threaded at all
		// without making the RHI thread-safe. See ENGINE-NOTES 7l.
		//
		// Progress is weighted by each source's size on disk, because this
		// scene's assets differ by a factor of sixty and a bar that steps once
		// per asset would cross 90% and sit there through the largest texture.
		static void PrepareScene(Scene& scene, Boot::Progress& progress);

		// Turns what PrepareScene found into GPU resources, **on the main
		// thread**, spending at most `budgetSeconds` per call and returning
		// true while work remains.
		//
		// Stepped rather than done in one go so the boot loop can draw a
		// frame between slices: without it the bar reaches 100%, vanishes,
		// and the window then freezes for the second it takes to upload
		// everything -- which is the original complaint, moved rather than
		// fixed.
		//
		// A budget rather than one asset per call, because those differ. One
		// per frame is fine for twenty assets and is *slower than not
		// bothering* for five hundred: at vsync that is eight seconds of
		// deliberate waiting to avoid one second of stall.
		static bool UploadPrepared(Boot::Progress& progress, float budgetSeconds);

		static void ClearCache();

		// Drop everything one source file produced, so the next request for it
		// loads from disk again. What the editor's asset watcher calls when a
		// file changes underneath a running session.
		//
		// **By path rather than by handle**, and that is not a convenience. A
		// model file holds several primitives and each gets a handle of its
		// own, so invalidating "the mesh" by the file's handle would drop the
		// first primitive and leave the rest of the model as it was -- a half
		// reloaded model, which is worse than one that did not reload at all
		// because it looks like the reload worked.
		//
		// Returns how many cached objects it dropped; zero means the file was
		// not loaded, which is not a failure.
		static size_t Invalidate(const std::filesystem::path& absoluteSource);
	};
}
