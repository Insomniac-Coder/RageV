#pragma once
#include "Asset.h"
#include "AssetRegistry.h"
#include "GltfImporter.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Animation/Skeleton.h"
#include "RageV/Renderer/Material.h"
#include "Curve.h"
#include "Font.h"

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

		// A plain 2D texture -- a particle sprite, an image. sRGB, mipped, and
		// cached like everything else here; a failure caches as null so a
		// missing file is not retried per frame.
		static RHI::Ref<RHI::RHITexture> GetTexture(AssetHandle handle);

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

		static void ClearCache();
	};
}
