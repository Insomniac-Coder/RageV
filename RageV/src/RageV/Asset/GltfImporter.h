#pragma once

// glTF 2.0 import.
//
// glTF only, by choice: Blender, Maya, Substance and every online library
// export it, and it is the one format whose material model already matches the
// renderer's -- metallic-roughness, the same channel packing, the same
// convention for which maps are sRGB. Supporting FBX as well would mean a
// second material translation with its own failure modes.
//
// The importer produces CPU-side data only. Turning that into GPU resources is
// the asset manager's job, so a headless tool can import and inspect a model
// without a device.

#include "Asset.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Animation/Skeleton.h"
#include <filesystem>
#include <string>
#include <vector>

namespace RageV::Assets
{
	struct ImportedTexture
	{
		// Relative to the model file.
		std::string Path;
		// Colour maps are sRGB, data maps are linear. Getting this wrong is the
		// most common cause of PBR that looks subtly washed out.
		bool SRGB = true;
	};

	struct ImportedMaterial
	{
		std::string Name = "Material";
		MaterialParams Params;

		// glTF's `alphaMode`, and FBX's transparency where a file states one.
		// **Dropped on the floor until the showroom's car arrived**, which is
		// why its windscreen, its lights and its four wheel-blur planes all
		// came in opaque -- the blur planes being the ones that gave it away,
		// since an opaque disc over a rim is a wheel with no spokes.
		BlendMode Blend = BlendMode::Opaque;

		// Indices into ImportedModel::Textures, or -1.
		int BaseColorTexture = -1;
		int NormalTexture = -1;
		int MetallicRoughnessTexture = -1;
		int OcclusionTexture = -1;
		int EmissiveTexture = -1;
	};

	// One drawable piece of geometry. A glTF mesh may hold several primitives
	// with different materials, and each becomes one of these -- the renderer
	// binds a material per draw, so they cannot be merged.
	struct ImportedPrimitive
	{
		std::string Name;
		std::vector<MeshVertex> Vertices;
		std::vector<uint32_t> Indices;
		int Material = -1;

		// Skinning, kept in parallel arrays rather than widened into
		// MeshVertex. Static meshes are almost all of them and would otherwise
		// pay thirty-two bytes a vertex for four indices and four weights they
		// never use.
		//
		// Empty when the primitive is not skinned, which is the test for it.
		// The indices address ImportedModel::Skeleton, already reordered --
		// glTF's own joint order is arbitrary and the skeleton's is not.
		std::vector<UVec4> Joints;
		std::vector<Vec4> Weights;

		bool IsSkinned() const { return !Joints.empty() && Joints.size() == Vertices.size(); }
	};

	struct ImportedNode
	{
		std::string Name = "Node";
		Vec3 Position{ 0.0f };
		Vec3 Rotation{ 0.0f };   // radians, XYZ euler
		Vec3 Scale{ 1.0f };

		int Parent = -1;
		// Indices into ImportedModel::Primitives.
		std::vector<int> Primitives;
	};

	struct ImportedModel
	{
		std::string Name;
		std::vector<ImportedPrimitive> Primitives;
		std::vector<ImportedMaterial> Materials;
		std::vector<ImportedTexture> Textures;
		// Parents always appear before their children, so one forward pass can
		// build the entity tree.
		std::vector<ImportedNode> Nodes;

		// The first skin in the file, if any, and every animation bound to it.
		//
		// One skeleton rather than a list: a file with two independent
		// characters in it is a file that should have been two files, and
		// supporting it would mean every skinned primitive carrying which
		// skeleton it belongs to for a case nobody exports.
		Skeleton Skeleton;
		std::vector<Anim::Clip> Clips;

		bool HasSkeleton() const { return !Skeleton.IsEmpty(); }

		bool IsEmpty() const { return Primitives.empty() && Nodes.empty(); }
	};

	class GltfImporter
	{
	public:
		// Returns false and logs on failure. Accepts .gltf and .glb, with
		// buffers either external, embedded as data URIs, or in the GLB chunk.
		//
		// Answers from cooked bytes when there are any -- the import cache
		// for a `.glb` seen before, or a pak in a shipped game -- and parses
		// the source otherwise. Every caller wanting *a model* wants this one.
		static bool Import(const std::filesystem::path& path, ImportedModel& out);

		// The same, from the source file only, with no cooked shortcut.
		//
		// Exists for the one caller that must not take the shortcut: the
		// import cache, whose job is to *produce* the cooked form. Calling
		// Import there would ask the cache for the answer it is in the middle
		// of computing, which is a recursion rather than a cache hit.
		static bool ImportSource(const std::filesystem::path& path, ImportedModel& out);
	};
}
