#pragma once

// Indexed triangle geometry living in GPU buffers, plus generators for the
// primitives an editor needs to place something in a scene.
//
// Unlike Renderer2D's quads, meshes are not batched: each carries its own
// vertex and index buffers and is drawn with its transform supplied as push
// constants. Batching only pays when many objects share geometry, which is a
// job for instancing rather than for merging vertex streams on the CPU.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Asset/Asset.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	struct MeshVertex
	{
		Vec3 Position;
		Vec3 Normal;
		Vec2 TexCoord;
	};

	// A vertex that a skeleton moves.
	//
	// Its own struct rather than four more fields on MeshVertex. Static
	// geometry is almost all the geometry in almost every scene, and widening
	// the common vertex would cost every one of them thirty-two bytes for
	// influences they do not have -- on the thousand-mesh stress scene that is
	// megabytes of bandwidth a frame to carry zeroes.
	//
	// The two therefore have separate pipelines and separate shaders. They
	// share the lighting through an include, so the thing that differs is the
	// only thing written twice: how a vertex reaches world space.
	struct SkinnedVertex
	{
		Vec3  Position;
		Vec3  Normal;
		Vec2  TexCoord;
		// Indices into the bone matrices, already in skeleton order -- the
		// importer remaps glTF's own joint order away before this is built.
		UVec4 Joints{ 0 };
		// Sum to one. The importer normalises rather than trusting the file.
		Vec4  Weights{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// An axis-aligned box in the mesh's own space.
	//
	// Lives here because this is where geometry is created and therefore the
	// only place it can be computed for free. Frustum culling will want the
	// same type; when it arrives this probably moves to a header of its own.
	struct AABB
	{
		Vec3 Min{ 0.0f };
		Vec3 Max{ 0.0f };

		Vec3 Centre() const { return (Min + Max) * 0.5f; }
		Vec3 Extents() const { return (Max - Min) * 0.5f; }
	};

	// Serialized by name, so the values must stay stable.
	enum class PrimitiveType : uint32_t
	{
		Cube = 0,
		Sphere = 1,
		Plane = 2,
		Cylinder = 3,
		Quad = 4,
	};

	const char* PrimitiveTypeName(PrimitiveType type);
	bool PrimitiveTypeFromName(const std::string& name, PrimitiveType& out);

	// Primitives are addressed as assets like everything else, so a component
	// needs one handle field rather than "a primitive enum or an asset".
	constexpr AssetHandle PrimitiveHandle(PrimitiveType type)
	{
		return AssetHandle(BuiltinAssets::kPrimitiveBase + (uint64_t)type);
	}

	class Mesh
	{
	public:
		Mesh(RHI::RHIDevice& device,
			 const std::vector<MeshVertex>& vertices,
			 const std::vector<uint32_t>& indices,
			 const std::string& debugName);

		// The skinned form. Same index buffer, wider vertices, and a flag the
		// renderer reads to pick a pipeline.
		Mesh(RHI::RHIDevice& device,
			 const std::vector<SkinnedVertex>& vertices,
			 const std::vector<uint32_t>& indices,
			 const std::string& debugName);

		// Which pipeline draws this. Not a guess from the buffer size: a
		// skinned mesh with no animator still has to be drawn by the skinned
		// shader, because its vertex layout is the wider one.
		bool IsSkinned() const { return m_Skinned; }

		const RHI::Ref<RHI::RHIBuffer>& GetVertexBuffer() const { return m_VertexBuffer; }
		const RHI::Ref<RHI::RHIBuffer>& GetIndexBuffer()  const { return m_IndexBuffer; }
		uint32_t GetIndexCount() const { return m_IndexCount; }

		// The bottom-level acceleration structure for this geometry, built on
		// first use and kept (ENGINE-NOTES 7am): static, the same lifetime as
		// the vertex buffer. For a skinned mesh it is the *bind pose* -- the
		// posed vertices exist only inside the vertex shader -- so a skinned
		// caster traces as its rest shape moved by its transform. Null on a
		// device that cannot trace, and stays null; callers take the answer as
		// "leave this mesh out".
		const RHI::Ref<RHI::RHIAccelerationStructure>& GetAccelerationStructure(RHI::RHIDevice& device);

		const AABB& GetBounds() const { return m_Bounds; }

		// Replaces the box computed from the vertices.
		//
		// For skinned meshes, whose drawn shape is not their vertices: the
		// bind pose's box is the one place a limb is guaranteed *not* to be
		// once a clip runs. `Anim::SkinnedBounds` produces the replacement and
		// the asset manager applies it, because only it has the clips.
		void SetBounds(const AABB& bounds) { m_Bounds = bounds; }

		// Positions and indices kept on the CPU.
		//
		// Needed because clicking in the viewport has to answer "what is under
		// this pixel" and the geometry that could answer it is on the GPU. The
		// alternative is an id buffer -- render entity ids to a second
		// attachment and read one pixel back -- which is pixel-exact and needs
		// a readback path plus an extra output in every shader. This is the
		// cheaper half of that trade, and it is also what frustum culling and
		// mesh colliders will want.
		//
		// Positions only: picking never asks about normals or texture
		// coordinates, and keeping those would double the cost for nothing.
		const std::vector<Vec3>& GetPositions() const { return m_Positions; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

		// Primitives are cached per device: placing a hundred cubes should not
		// allocate a hundred identical vertex buffers.
		static RHI::Ref<Mesh> GetPrimitive(RHI::RHIDevice& device, PrimitiveType type);
		static void ClearCache();

	private:
		RHI::Ref<RHI::RHIBuffer> m_VertexBuffer;
		RHI::Ref<RHI::RHIBuffer> m_IndexBuffer;
		uint32_t m_IndexCount = 0;
		uint32_t m_VertexCount = 0;
		uint32_t m_VertexStride = 0;
		RHI::Ref<RHI::RHIAccelerationStructure> m_Blas;
		bool m_BlasTried = false;

		AABB m_Bounds;
		std::vector<Vec3> m_Positions;
		std::vector<uint32_t> m_Indices;
		bool m_Skinned = false;
	};

	namespace Primitives
	{
		void Cube(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);
		void Plane(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);
		void Quad(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);
		void Sphere(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
					uint32_t segments = 32, uint32_t rings = 16);
		void Cylinder(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
					  uint32_t segments = 32);
	}
}
