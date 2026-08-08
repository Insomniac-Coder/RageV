#pragma once

// Indexed triangle geometry living in GPU buffers, plus generators for the
// primitives an editor needs to place something in a scene.
//
// Unlike Renderer2D's quads, meshes are not batched: each carries its own
// vertex and index buffers and is drawn with its transform supplied as push
// constants. Batching only pays when many objects share geometry, which is a
// job for instancing rather than for merging vertex streams on the CPU.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "glm/glm.hpp"

namespace RageV
{
	struct MeshVertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
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

		// Primitives are cached per device: placing a hundred cubes should not
		// allocate a hundred identical vertex buffers.
		static RHI::Ref<Mesh> GetPrimitive(RHI::RHIDevice& device, PrimitiveType type);
		static void ClearCache();

	private:
		RHI::Ref<RHI::RHIBuffer> m_VertexBuffer;
		RHI::Ref<RHI::RHIBuffer> m_IndexBuffer;
		uint32_t m_IndexCount = 0;
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
