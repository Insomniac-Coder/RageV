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
#include "glm/glm.hpp"

namespace RageV
{
	struct MeshVertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
	};

	// An axis-aligned box in the mesh's own space.
	//
	// Lives here because this is where geometry is created and therefore the
	// only place it can be computed for free. Frustum culling will want the
	// same type; when it arrives this probably moves to a header of its own.
	struct AABB
	{
		glm::vec3 Min{ 0.0f };
		glm::vec3 Max{ 0.0f };

		glm::vec3 Centre() const { return (Min + Max) * 0.5f; }
		glm::vec3 Extents() const { return (Max - Min) * 0.5f; }
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

		const RHI::Ref<RHI::RHIBuffer>& GetVertexBuffer() const { return m_VertexBuffer; }
		const RHI::Ref<RHI::RHIBuffer>& GetIndexBuffer()  const { return m_IndexBuffer; }
		uint32_t GetIndexCount() const { return m_IndexCount; }

		const AABB& GetBounds() const { return m_Bounds; }

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
		const std::vector<glm::vec3>& GetPositions() const { return m_Positions; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

		// Primitives are cached per device: placing a hundred cubes should not
		// allocate a hundred identical vertex buffers.
		static RHI::Ref<Mesh> GetPrimitive(RHI::RHIDevice& device, PrimitiveType type);
		static void ClearCache();

	private:
		RHI::Ref<RHI::RHIBuffer> m_VertexBuffer;
		RHI::Ref<RHI::RHIBuffer> m_IndexBuffer;
		uint32_t m_IndexCount = 0;

		AABB m_Bounds;
		std::vector<glm::vec3> m_Positions;
		std::vector<uint32_t> m_Indices;
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
