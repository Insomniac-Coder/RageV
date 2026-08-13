#include <rvpch.h>
#include "AssetManager.h"
#include "RageV/IO/VFS.h"
#include "RageV/Core/Log.h"
#include "RageV/Renderer/TextureLoader.h"
#include "RageV/Renderer/EnvironmentIBL.h"
#include "RageV/Renderer/ProbeArray.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/SceneSerializer.h"
#include "CurveSerializer.h"
#include "FontSerializer.h"
// The import splits glTF's packed metallic-roughness texture into the two
// greyscale maps the shader samples, which means reading a PNG and writing two.
#include "stb_image.h"
#include "stb_write_image.h"
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

		// Plain 2D textures -- particle sprites today, material maps whenever
		// materials become assets. Failures cache as null like the cube's do.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_Textures;
		// The same files read linearly: normal, roughness, metallic and
		// occlusion maps are numbers stored in an image, not pictures.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_DataTextures;
		// Materials, by handle. Shared on purpose: two entities pointing at one
		// `.rmat` must get the same object, or their batch keys differ and the
		// draws never merge -- which is the whole reason materials became
		// assets. Failures cache as null like the textures do.
		std::unordered_map<AssetHandle, RHI::Ref<Material>> s_Materials;
		// Curves are small and pure data, so they cache by value. A failure
		// caches as an empty curve for the same reason a texture caches as
		// null: a missing file must not be retried sixty times a second.
		std::unordered_map<AssetHandle, Curve> s_Curves;
		std::unordered_map<AssetHandle, Curve::Baked> s_BakedCurves;

		// Fonts cache by value like curves -- a metrics table is a few thousand
		// numbers. A failure caches as an *absent* entry marked in s_FontFailed
		// rather than as an empty font, because "no glyphs" and "no font" want
		// different answers from the caller and an empty table cannot say which
		// it is.
		std::unordered_map<AssetHandle, Font> s_Fonts;
		std::unordered_set<AssetHandle> s_FontFailed;
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_FontAtlases;

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
		s_Textures.clear();
		s_DataTextures.clear();
		s_Materials.clear();
		s_Curves.clear();
		s_BakedCurves.clear();
		s_Fonts.clear();
		s_FontFailed.clear();
		s_FontAtlases.clear();

		// The loader and the filter hold the same textures by path and by
		// pointer, and both used to be cleared only at shutdown -- so changing
		// project kept every old project's environment maps alive, and the
		// filter's map was keyed on addresses whose owners it did not know the
		// lifetime of. Cleared together or not at all.
		TextureLoader::ClearCache();
		EnvironmentIBL::ClearCache();
		// The probe arrays hold slices convolved from those same textures, and
		// remember which source filled each slot by address. Keeping them would
		// be keeping a filtered copy of a freed cube, and a slot that believes
		// it is current.
		ProbeArray::ClearCache();
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

	RHI::Ref<RHI::RHITexture> Manager::GetTexture(AssetHandle handle, ColorSpace space)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		// One cache per colour space. The same PNG decoded both ways is two
		// different textures, and a single cache would return whichever was
		// asked for first -- so a normal map would come back sRGB if some
		// sprite had loaded it earlier.
		auto& cache = space == ColorSpace::Srgb ? s_Textures : s_DataTextures;

		const auto cached = cache.find(handle);
		if (cached != cache.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			cache[handle] = nullptr;
			return nullptr;
		}

		auto texture = TextureLoader::Load2D(*s_Device, path.string(),
											 space == ColorSpace::Srgb);
		cache[handle] = texture;
		return texture;
	}

	RHI::Ref<Material> Manager::GetMaterial(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_Materials.find(handle);
		if (cached != s_Materials.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		MaterialDesc desc;
		if (path.empty() || !MaterialSerializer::Load(desc, path))
		{
			// Cached as null so a broken path is not reopened per frame, and
			// the caller falls back to the default material -- the same place
			// an unassigned handle lands.
			s_Materials[handle] = nullptr;
			return nullptr;
		}

		auto material = std::make_shared<Material>(*s_Device, desc.Name);
		material->GetParams() = desc.Params;

		// The maps, and with them the flags. Assigning a null texture clears
		// the slot and its flag, so a handle that resolves to nothing lands on
		// the scalar parameter rather than on a missing binding.
		auto assign = [&](AssetHandle map, void (Material::*setter)(const RHI::Ref<RHI::RHITexture>&),
						  ColorSpace space)
		{
			if (map.IsValid())
				(material.get()->*setter)(GetTexture(map, space));
		};

		// Two of these are pictures and five are data. Reading a normal map
		// through sRGB pulls its X and Y toward the centre and flattens every
		// surface using it -- which looks like the normal maps not being
		// applied at all, and is how this was found.
		assign(desc.BaseColorMap, &Material::SetBaseColorMap, ColorSpace::Srgb);
		assign(desc.EmissiveMap, &Material::SetEmissiveMap, ColorSpace::Srgb);

		assign(desc.NormalMap, &Material::SetNormalMap, ColorSpace::Linear);
		assign(desc.OcclusionMap, &Material::SetOcclusionMap, ColorSpace::Linear);
		assign(desc.RoughnessMap, &Material::SetRoughnessMap, ColorSpace::Linear);
		assign(desc.MetallicMap, &Material::SetMetallicMap, ColorSpace::Linear);
		assign(desc.SpecularMap, &Material::SetSpecularMap, ColorSpace::Linear);
		assign(desc.HeightMap, &Material::SetHeightMap, ColorSpace::Linear);

		material->Invalidate();

		s_Materials[handle] = material;
		return material;
	}

	AssetHandle Manager::CreateMaterial(const MaterialDesc& material,
										const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!MaterialSerializer::Save(material, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreateCurve and CreatePrefab.
		Registry::Refresh();

		// Deliberately *not* seeded into the cache from `material` here, unlike
		// CreateCurve. A curve is the value it was created from; a Material is
		// a GPU object that has to be built from the desc, and building it here
		// would duplicate GetMaterial's resolve step -- which is where the
		// texture handles turn into textures and the map flags get set.
		return Registry::GetHandle(relativePath.generic_string());
	}

	void Manager::ReloadMaterial(AssetHandle handle)
	{
		s_Materials.erase(handle);
	}

	const Curve* Manager::GetCurve(AssetHandle handle)
	{
		// No device check, unlike the textures either side of this: a curve is
		// numbers on the CPU. That is what lets the suite exercise it, and what
		// will let a headless tool bake one.
		if (!handle.IsValid())
			return nullptr;

		const auto cached = s_Curves.find(handle);
		if (cached != s_Curves.end())
			return &cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		Curve curve;
		if (path.empty() || !CurveSerializer::Load(curve, path))
		{
			// Cached even on failure, and returned rather than null: an emitter
			// pointing at a missing curve should fall back to a value, not to a
			// branch every caller has to remember. An empty curve evaluates to
			// its stated fallback everywhere.
			s_Curves[handle] = Curve();
			return &s_Curves[handle];
		}

		s_Curves[handle] = std::move(curve);
		return &s_Curves[handle];
	}

	const Font* Manager::GetFont(AssetHandle handle)
	{
		// No device check: a metrics table is numbers, and keeping it that way
		// is what lets the suite test text layout with no GPU at all.
		if (!handle.IsValid())
			return nullptr;

		if (s_FontFailed.count(handle) != 0)
			return nullptr;

		const auto cached = s_Fonts.find(handle);
		if (cached != s_Fonts.end())
			return &cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		Font font;
		if (path.empty() || !FontSerializer::Load(font, path))
		{
			// Remembered, so a scene pointing at a font that is not there does
			// not reopen the file once per frame for the rest of the session.
			s_FontFailed.insert(handle);
			return nullptr;
		}

		s_Fonts[handle] = std::move(font);
		return &s_Fonts[handle];
	}

	RHI::Ref<RHI::RHITexture> Manager::GetFontAtlas(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_FontAtlases.find(handle);
		if (cached != s_FontAtlases.end())
			return cached->second;

		const Font* font = GetFont(handle);
		if (!font)
			return nullptr;

		// The `.rvfont` names its atlas by filename, and it is resolved beside
		// the metrics rather than against the asset root. The two are one
		// output of one tool run, so a project that moves a font into a
		// subfolder moves both and nothing has to be edited.
		const std::filesystem::path metrics = Registry::GetAbsolutePath(handle);
		const std::filesystem::path atlas = metrics.parent_path() / font->AtlasFile;

		// Through the VFS, like every content read: an atlas inside a pak does
		// not exist on disk, and this check saying so cost the packaged HUD
		// its font -- the E2E's pixel diff is what caught it.
		if (!IO::VFS::Exists(atlas))
		{
			RV_CORE_ERROR("Font {0} names an atlas that is not there: {1}",
						  metrics.filename().string(), font->AtlasFile);
			s_FontAtlases[handle] = nullptr;
			return nullptr;
		}

		// Linear, and no mips. Both matter and both are silent when wrong:
		// treating a distance field as sRGB bends the distances so the edge
		// lands somewhere else, and a mip chain averages distances from
		// opposite sides of a stroke into a number that describes nothing.
		// Either one renders as text that is soft for no visible reason.
		RHI::Ref<RHI::RHITexture> texture =
			TextureLoader::Load2D(*s_Device, atlas.string(), /*srgb*/ false,
								  /*generateMips*/ false);

		s_FontAtlases[handle] = texture;
		return texture;
	}

	const Curve::Baked* Manager::GetBakedCurve(AssetHandle handle)
	{
		if (!handle.IsValid())
			return nullptr;

		const auto cached = s_BakedCurves.find(handle);
		if (cached != s_BakedCurves.end())
			return &cached->second;

		// Through GetCurve, so the "a valid handle always answers something"
		// contract is stated once. A missing file bakes an empty curve, which
		// is a table of zeroes -- the fallback, sampled rather than branched on.
		const Curve* curve = GetCurve(handle);
		if (!curve)
			return nullptr;

		s_BakedCurves[handle] = curve->BakeTable();
		return &s_BakedCurves[handle];
	}

	AssetHandle Manager::CreateCurve(const Curve& curve, const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!CurveSerializer::Save(curve, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreatePrefab.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
			s_Curves[handle] = curve;

		return handle;
	}

	void Manager::ReloadCurve(AssetHandle handle)
	{
		// The editor calls this when somebody has finished dragging a point.
		// Without it the cache above is exactly the stale-data bug the probe's
		// mip chain was: the file changes and the picture does not.
		s_Curves.erase(handle);
		s_BakedCurves.erase(handle);
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

		// The model's textures, as *handles* rather than loaded images.
		//
		// A glTF names its textures by relative URI; the registry addresses
		// everything by handle. Importing one therefore means registering the
		// file it points at, which is what makes the material below storable:
		// a `.rmat` can hold a handle and cannot hold a loaded texture.
		//
		// A texture already in the project keeps the handle it has -- the
		// registry is keyed on path -- so re-importing a model does not mint a
		// second handle for a file two models share.
		Registry::Refresh();

		std::vector<AssetHandle> textures(model.Textures.size());
		for (size_t i = 0; i < model.Textures.size(); i++)
		{
			const std::filesystem::path texturePath = directory / model.Textures[i].Path;

			std::error_code error;
			const std::filesystem::path relative =
				std::filesystem::relative(texturePath, Registry::Root(), error);

			if (error || relative.empty())
			{
				RV_CORE_WARN("Model texture '{0}' is outside the project and cannot be "
							 "addressed by handle", texturePath.string());
				continue;
			}

			textures[i] = Registry::GetHandle(relative.generic_string());
			if (!textures[i].IsValid())
			{
				RV_CORE_WARN("Model texture '{0}' has no asset handle; its material will "
							 "fall back to the scalar parameter", relative.generic_string());
			}
		}

		// glTF's packed metallic-roughness texture, split into the two greyscale
		// maps the engine actually samples.
		//
		// The shader takes roughness and metallic separately, because that is
		// how every texture library ships them. glTF is the exception: it packs
		// roughness into green and metallic into blue of one image. Rather than
		// carry a second code path through the material, the descriptor set and
		// the shader for the sake of one file format, the import splits it once.
		//
		// Written to disk rather than split in memory, and that is not
		// incidental: a material stores *handles*, and a texture that exists
		// only in memory has none -- so an in-memory split would render
		// correctly and vanish on save, which is the exact bug this whole task
		// was about.
		auto splitMetallicRoughness = [&](int textureIndex,
										  AssetHandle& roughnessOut, AssetHandle& metallicOut)
		{
			if (textureIndex < 0 || textureIndex >= (int)model.Textures.size())
				return;

			const std::filesystem::path packed = directory / model.Textures[textureIndex].Path;

			int width = 0, height = 0, channels = 0;
			stbi_uc* pixels = stbi_load(packed.string().c_str(), &width, &height, &channels, 4);
			if (!pixels)
			{
				RV_CORE_WARN("Could not read '{0}' to split its metallic-roughness channels",
							 packed.string());
				return;
			}

			std::vector<stbi_uc> roughness((size_t)width * height);
			std::vector<stbi_uc> metallic((size_t)width * height);
			for (size_t i = 0; i < roughness.size(); i++)
			{
				roughness[i] = pixels[i * 4 + 1];   // green
				metallic[i]  = pixels[i * 4 + 2];   // blue
			}
			stbi_image_free(pixels);

			const std::filesystem::path stem = packed.parent_path() / packed.stem();

			auto write = [&](const char* suffix, const std::vector<stbi_uc>& channel,
							 AssetHandle& out)
			{
				const std::filesystem::path path = stem.string() + suffix + ".png";
				if (stbi_write_png(path.string().c_str(), width, height, 1,
								   channel.data(), width) == 0)
				{
					RV_CORE_WARN("Could not write '{0}'", path.string());
					return;
				}

				std::error_code error;
				const std::filesystem::path relative =
					std::filesystem::relative(path, Registry::Root(), error);
				if (error || relative.empty())
					return;

				Registry::Refresh();
				out = Registry::GetHandle(relative.generic_string());
			};

			write("_roughness", roughness, roughnessOut);
			write("_metallic", metallic, metallicOut);
		};

		// Each imported material becomes a real `.rmat` beside the model.
		//
		// This is the part that makes an import survive being saved. The maps
		// used to be set on a Material object hanging off the component, which
		// no scene file could write -- so a textured model looked correct until
		// the first save and was untextured from then on. Now the maps live in
		// an asset and the component stores its handle.
		std::vector<AssetHandle> materials(model.Materials.size());
		for (size_t i = 0; i < model.Materials.size(); i++)
		{
			const ImportedMaterial& source = model.Materials[i];

			MaterialDesc desc;
			desc.Name = source.Name.empty() ? "Material" : source.Name;
			desc.Params = source.Params;

			auto assign = [&](int index, AssetHandle& slot)
			{
				if (index >= 0 && index < (int)textures.size())
					slot = textures[index];
			};

			assign(source.BaseColorTexture, desc.BaseColorMap);
			assign(source.NormalTexture, desc.NormalMap);
			assign(source.OcclusionTexture, desc.OcclusionMap);
			assign(source.EmissiveTexture, desc.EmissiveMap);

			// Not assigned -- split. See above.
			splitMetallicRoughness(source.MetallicRoughnessTexture,
								   desc.RoughnessMap, desc.MetallicMap);

			// Named after the model and the material's index, not just its
			// name: a glTF may carry two materials called "Material", and two
			// models certainly may.
			std::error_code error;
			const std::filesystem::path modelRelative =
				std::filesystem::relative(path, Registry::Root(), error);
			const std::string stem = error ? path.stem().string()
										   : modelRelative.stem().string();

			const std::filesystem::path materialPath =
				modelRelative.parent_path() /
				(stem + "_" + std::to_string(i) + "_" + desc.Name + ".rmat");

			materials[i] = CreateMaterial(desc, materialPath);
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
