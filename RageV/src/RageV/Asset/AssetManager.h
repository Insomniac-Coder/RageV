#pragma once
#include "Asset.h"
#include "AssetRegistry.h"
#include "GltfImporter.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Animation/Skeleton.h"
#include "RageV/Renderer/Material.h"

namespace RageV
{
	class Scene;
	class Entity;

	// Turns handles into loaded assets, and caches the result.
	//
	// Split from AssetRegistry on purpose: the registry knows what exists and
	// needs no GPU, so a headless tool can inspect a project. The manager
	// creates GPU resources and needs a device.
	class AssetManager
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
		static const std::vector<AnimationClip>* GetClips(AssetHandle handle);

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
