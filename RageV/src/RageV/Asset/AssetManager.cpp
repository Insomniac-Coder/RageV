#include <rvpch.h>
#include "AssetManager.h"
#include "RageV/Core/Log.h"
#include "RageV/Renderer/TextureLoader.h"
#include "RageV/Renderer/EnvironmentIBL.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/SceneSerializer.h"
#include <fstream>
#include <sstream>

namespace RageV::Assets
{
	namespace
	{
		RHI::RHIDevice* s_Device = nullptr;

		// Loaded meshes, by handle. A model with several primitives produces
		// several meshes, so the cache is keyed on the sub-handle rather than
		// on the file.
		std::unordered_map<AssetHandle, RHI::Ref<Mesh>> s_Meshes;
		// Cached beside the meshes and cleared with them: they come out of the
		// same parse, so fetching them separately would read the file twice.
		std::unordered_map<AssetHandle, Skeleton> s_Skeletons;
		std::unordered_map<AssetHandle, std::vector<Anim::Clip>> s_Clips;

		// Environment maps, keyed on the handle rather than the path, so a scene
		// that reloads keeps the cube it already paid to build.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_Cubemaps;
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_Irradiance;

		constexpr PrimitiveType kPrimitives[] = {
			PrimitiveType::Cube, PrimitiveType::Sphere, PrimitiveType::Plane,
			PrimitiveType::Cylinder, PrimitiveType::Quad,
		};
	}

	AssetHandle Manager::GetPrimitiveHandle(PrimitiveType type)
	{
		return AssetHandle(BuiltinAssets::kPrimitiveBase + (uint64_t)type);
	}

	bool Manager::IsPrimitive(AssetHandle handle, PrimitiveType& out)
	{
		const uint64_t value = handle;
		if (value < BuiltinAssets::kPrimitiveBase ||
			value >= BuiltinAssets::kPrimitiveBase + std::size(kPrimitives))
			return false;

		out = (PrimitiveType)(value - BuiltinAssets::kPrimitiveBase);
		return true;
	}

	void Manager::Init(RHI::RHIDevice& device)
	{
		s_Device = &device;

		for (PrimitiveType type : kPrimitives)
		{
			Registry::RegisterVirtual(GetPrimitiveHandle(type), AssetType::Mesh,
										   PrimitiveTypeName(type));
		}
	}

	void Manager::Shutdown()
	{
		ClearCache();
		s_Device = nullptr;
	}

	void Manager::ClearCache()
	{
		s_Meshes.clear();
		s_Skeletons.clear();
		s_Clips.clear();
		s_Cubemaps.clear();
		s_Irradiance.clear();

		// The loader and the filter hold the same textures by path and by
		// pointer, and both used to be cleared only at shutdown -- so changing
		// project kept every old project's environment maps alive, and the
		// filter's map was keyed on addresses whose owners it did not know the
		// lifetime of. Cleared together or not at all.
		TextureLoader::ClearCache();
		EnvironmentIBL::ClearCache();
	}

	std::string Manager::GetDisplayName(AssetHandle handle)
	{
		if (!handle.IsValid())
			return "(none)";

		PrimitiveType primitive;
		if (IsPrimitive(handle, primitive))
			return PrimitiveTypeName(primitive);

		const AssetMetadata& metadata = Registry::GetMetadata(handle);
		if (!metadata.IsValid())
			return "(missing)";

		const size_t slash = metadata.Path.find_last_of('/');
		return slash == std::string::npos ? metadata.Path : metadata.Path.substr(slash + 1);
	}

	RHI::Ref<Mesh> Manager::GetMesh(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		PrimitiveType primitive;
		if (IsPrimitive(handle, primitive))
			return Mesh::GetPrimitive(*s_Device, primitive);

		const auto cached = s_Meshes.find(handle);
		if (cached != s_Meshes.end())
			return cached->second;

		// A model file holds several primitives; only the first is addressable
		// by the file's own handle. InstantiateModel is the entry point that
		// reaches the rest, and it populates this cache as it goes.
		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
			return nullptr;

		ImportedModel model;
		if (!GltfImporter::Import(path, model) || model.Primitives.empty())
		{
			// Cached as null so a broken file is not re-parsed every frame.
			s_Meshes[handle] = nullptr;
			return nullptr;
		}

		// A skinned primitive becomes a skinned mesh, and the skeleton it is
		// posed by is remembered under the same handle.
		if (model.HasSkeleton())
		{
			s_Skeletons[handle] = model.Skeleton;
			s_Clips[handle] = model.Clips;
		}

		if (model.Primitives[0].IsSkinned())
		{
			std::vector<SkinnedVertex> skinned;
			skinned.reserve(model.Primitives[0].Vertices.size());

			for (size_t i = 0; i < model.Primitives[0].Vertices.size(); i++)
			{
				const MeshVertex& source = model.Primitives[0].Vertices[i];

				SkinnedVertex vertex;
				vertex.Position = source.Position;
				vertex.Normal = source.Normal;
				vertex.TexCoord = source.TexCoord;
				vertex.Joints = model.Primitives[0].Joints[i];
				vertex.Weights = model.Primitives[0].Weights[i];

				skinned.push_back(vertex);
			}

			auto skinnedMesh = std::make_shared<Mesh>(*s_Device, skinned,
													  model.Primitives[0].Indices,
													  model.Primitives[0].Name);
			s_Meshes[handle] = skinnedMesh;
			return skinnedMesh;
		}

		auto mesh = std::make_shared<Mesh>(*s_Device, model.Primitives[0].Vertices,
										   model.Primitives[0].Indices, model.Primitives[0].Name);
		s_Meshes[handle] = mesh;
		return mesh;
	}

	const Skeleton* Manager::GetSkeleton(AssetHandle handle)
	{
		// GetMesh first: the skeleton is cached by the same parse, and asking
		// for it before anything has loaded the model would report that a
		// perfectly good rig has none.
		GetMesh(handle);

		const auto found = s_Skeletons.find(handle);
		return found != s_Skeletons.end() ? &found->second : nullptr;
	}

	const std::vector<Anim::Clip>* Manager::GetClips(AssetHandle handle)
	{
		GetMesh(handle);

		const auto found = s_Clips.find(handle);
		return found != s_Clips.end() ? &found->second : nullptr;
	}

	RHI::Ref<RHI::RHITexture> Manager::GetCubemap(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_Cubemaps.find(handle);
		if (cached != s_Cubemaps.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			s_Cubemaps[handle] = nullptr;
			return nullptr;
		}

		auto cube = TextureLoader::LoadCube(*s_Device, path.string());
		s_Cubemaps[handle] = cube;
		return cube;
	}

	RHI::Ref<RHI::RHITexture> Manager::GetIrradiance(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		// Cached by handle like the cube is. Without this it resolved a handle
		// to an absolute path and built a std::string every frame a scene was
		// drawn, to reach a cache that was already holding the answer.
		const auto cached = s_Irradiance.find(handle);
		if (cached != s_Irradiance.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			s_Irradiance[handle] = nullptr;
			return nullptr;
		}

		auto irradiance = TextureLoader::LoadIrradiance(*s_Device, path.string());
		s_Irradiance[handle] = irradiance;
		return irradiance;
	}

	AssetHandle Manager::CreatePrefab(Scene& scene, Entity root,
										   const std::filesystem::path& relativePath)
	{
		if (!root || !Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;

		std::error_code error;
		std::filesystem::create_directories(absolute.parent_path(), error);

		// The same subtree snapshot undo uses. A prefab is not a new format --
		// it is a scene file holding one tree, which is why instantiating one
		// and restoring a deleted one share a reader.
		auto shared = std::shared_ptr<Scene>(&scene, [](Scene*) {});
		SceneSerializer serializer(shared);
		const std::string yaml = serializer.SerializeSubtree(root);

		std::ofstream file(absolute);
		if (!file)
		{
			RV_CORE_ERROR("Could not write prefab '{0}'", absolute.string());
			return AssetHandle::Invalid();
		}
		file << yaml;
		file.close();

		// Picks up the new file and mints its sidecar.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
		{
			// The source tree becomes an instance of the prefab it just made,
			// which is what makes "create prefab" feel like extracting rather
			// than copying.
			if (!root.HasComponent<PrefabComponent>())
				root.AddComponent<PrefabComponent>(handle);
			else
				root.GetComponent<PrefabComponent>().Source = handle;
		}

		return handle;
	}

	Entity Manager::InstantiatePrefab(Scene& scene, AssetHandle handle)
	{
		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
			return {};

		std::ifstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not read prefab '{0}'", path.string());
			return {};
		}

		std::stringstream buffer;
		buffer << file.rdbuf();

		auto shared = std::shared_ptr<Scene>(&scene, [](Scene*) {});
		SceneSerializer serializer(shared);

		// Fresh ids: two instances that shared identities would mean every
		// reference to one resolved to both.
		Entity root = serializer.Instantiate(buffer.str());
		if (!root)
			return {};

		if (!root.HasComponent<PrefabComponent>())
			root.AddComponent<PrefabComponent>(handle);
		else
			root.GetComponent<PrefabComponent>().Source = handle;

		scene.UpdateWorldTransforms();
		return root;
	}

	Entity Manager::InstantiateModel(Scene& scene, AssetHandle handle)
	{
		if (!s_Device)
			return {};

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			RV_CORE_ERROR("No source file for asset {0}", (uint64_t)handle);
			return {};
		}

		ImportedModel model;
		if (!GltfImporter::Import(path, model))
			return {};

		// Textures resolve relative to the model, which is where glTF's URIs
		// point.
		const std::filesystem::path directory = path.parent_path();

		std::vector<RHI::Ref<RHI::RHITexture>> textures(model.Textures.size());
		for (size_t i = 0; i < model.Textures.size(); i++)
		{
			const std::filesystem::path texturePath = directory / model.Textures[i].Path;
			textures[i] = TextureLoader::Load2D(*s_Device, texturePath.string(),
												model.Textures[i].SRGB);
		}

		std::vector<RHI::Ref<Material>> materials(model.Materials.size());
		for (size_t i = 0; i < model.Materials.size(); i++)
		{
			const ImportedMaterial& source = model.Materials[i];

			auto material = std::make_shared<Material>(*s_Device, source.Name);
			material->GetParams() = source.Params;

			auto assign = [&](int index, void (Material::*setter)(const RHI::Ref<RHI::RHITexture>&))
			{
				if (index >= 0 && index < (int)textures.size() && textures[index])
					(material.get()->*setter)(textures[index]);
			};

			assign(source.BaseColorTexture, &Material::SetBaseColorMap);
			assign(source.NormalTexture, &Material::SetNormalMap);
			assign(source.MetallicRoughnessTexture, &Material::SetMetallicRoughnessMap);
			assign(source.OcclusionTexture, &Material::SetOcclusionMap);
			assign(source.EmissiveTexture, &Material::SetEmissiveMap);

			material->Invalidate();
			materials[i] = material;
		}

		// Every primitive gets a handle derived from the file's, so a mesh
		// inside a model is addressable and the cache can hold it.
		std::vector<RHI::Ref<Mesh>> meshes(model.Primitives.size());
		for (size_t i = 0; i < model.Primitives.size(); i++)
		{
			meshes[i] = std::make_shared<Mesh>(*s_Device, model.Primitives[i].Vertices,
											   model.Primitives[i].Indices,
											   model.Primitives[i].Name);
			s_Meshes[AssetHandle((uint64_t)handle + 1 + i)] = meshes[i];
		}

		// One root, so the import is a single thing to move, delete or undo.
		Entity root = scene.CreateEntity(model.Name);

		std::vector<Entity> entities(model.Nodes.size());
		for (size_t i = 0; i < model.Nodes.size(); i++)
		{
			const ImportedNode& node = model.Nodes[i];

			Entity entity = scene.CreateEntity(node.Name);
			auto& transform = entity.GetComponent<TransformComponent>();
			transform.Position = node.Position;
			transform.Rotation = node.Rotation;
			transform.Scale = node.Scale;

			entities[i] = entity;

			// A node may carry several primitives with different materials.
			// The first rides on the node itself; the rest become children, so
			// each keeps one mesh and one material.
			for (size_t p = 0; p < node.Primitives.size(); p++)
			{
				const int index = node.Primitives[p];
				if (index < 0 || index >= (int)meshes.size())
					continue;

				Entity target = entity;
				if (p > 0)
				{
					target = scene.CreateEntity(model.Primitives[index].Name);
					scene.SetParent(target, entity);
				}

				auto& mesh = target.AddComponent<MeshComponent>();
				mesh.Mesh = AssetHandle((uint64_t)handle + 1 + index);

				const int materialIndex = model.Primitives[index].Material;
				if (materialIndex >= 0 && materialIndex < (int)materials.size())
					mesh.Material = materials[materialIndex];
			}
		}

		// Parented after every entity exists, since a node's parent index
		// always precedes it but SetParent needs both to be real.
		for (size_t i = 0; i < model.Nodes.size(); i++)
		{
			const int parent = model.Nodes[i].Parent;
			scene.SetParent(entities[i], parent >= 0 ? entities[parent] : root);
		}

		scene.UpdateWorldTransforms();
		return root;
	}
}
