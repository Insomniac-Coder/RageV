#include <rvpch.h>
#include "Mesh.h"
#include <glm/gtc/constants.hpp>

namespace RageV
{
	using namespace RageV::RHI;

	const char* PrimitiveTypeName(PrimitiveType type)
	{
		switch (type)
		{
			case PrimitiveType::Cube:     return "Cube";
			case PrimitiveType::Sphere:   return "Sphere";
			case PrimitiveType::Plane:    return "Plane";
			case PrimitiveType::Cylinder: return "Cylinder";
			case PrimitiveType::Quad:     return "Quad";
		}
		return "Cube";
	}

	bool PrimitiveTypeFromName(const std::string& name, PrimitiveType& out)
	{
		if (name == "Cube")     { out = PrimitiveType::Cube;     return true; }
		if (name == "Sphere")   { out = PrimitiveType::Sphere;   return true; }
		if (name == "Plane")    { out = PrimitiveType::Plane;    return true; }
		if (name == "Cylinder") { out = PrimitiveType::Cylinder; return true; }
		if (name == "Quad")     { out = PrimitiveType::Quad;     return true; }
		return false;
	}

	Mesh::Mesh(RHIDevice& device,
			   const std::vector<MeshVertex>& vertices,
			   const std::vector<uint32_t>& indices,
			   const std::string& debugName)
		: m_IndexCount((uint32_t)indices.size())
	{
		// Static geometry, so device-local through the staging path rather than
		// host-visible: it is written once and read every frame.
		BufferDesc vertexDesc;
		vertexDesc.Size = vertices.size() * sizeof(MeshVertex);
		vertexDesc.Usage = BufferUsage::Vertex;
		vertexDesc.Memory = MemoryDomain::DeviceLocal;
		vertexDesc.DebugName = debugName + ".vertices";
		m_VertexBuffer = device.CreateBuffer(vertexDesc);
		m_VertexBuffer->Upload(vertices.data(), vertexDesc.Size);

		BufferDesc indexDesc;
		indexDesc.Size = indices.size() * sizeof(uint32_t);
		indexDesc.Usage = BufferUsage::Index;
		indexDesc.Memory = MemoryDomain::DeviceLocal;
		indexDesc.DebugName = debugName + ".indices";
		m_IndexBuffer = device.CreateBuffer(indexDesc);
		m_IndexBuffer->Upload(indices.data(), indexDesc.Size);
	}

	namespace
	{
		std::unordered_map<uint32_t, Ref<Mesh>> s_PrimitiveCache;
	}

	Ref<Mesh> Mesh::GetPrimitive(RHIDevice& device, PrimitiveType type)
	{
		const uint32_t key = (uint32_t)type;
		const auto it = s_PrimitiveCache.find(key);
		if (it != s_PrimitiveCache.end())
			return it->second;

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;

		switch (type)
		{
			case PrimitiveType::Cube:     Primitives::Cube(vertices, indices);     break;
			case PrimitiveType::Sphere:   Primitives::Sphere(vertices, indices);   break;
			case PrimitiveType::Plane:    Primitives::Plane(vertices, indices);    break;
			case PrimitiveType::Cylinder: Primitives::Cylinder(vertices, indices); break;
			case PrimitiveType::Quad:     Primitives::Quad(vertices, indices);     break;
		}

		auto mesh = std::make_shared<Mesh>(device, vertices, indices, PrimitiveTypeName(type));
		s_PrimitiveCache[key] = mesh;
		return mesh;
	}

	void Mesh::ClearCache()
	{
		// Must run before the device is destroyed: these hold GPU buffers.
		s_PrimitiveCache.clear();
	}

	// -------------------------------------------------------------------------
	// Primitive generation
	// -------------------------------------------------------------------------
	namespace Primitives
	{
		namespace
		{
			void AddQuadFace(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
							 const glm::vec3& origin, const glm::vec3& right, const glm::vec3& up,
							 const glm::vec3& normal)
			{
				const uint32_t base = (uint32_t)vertices.size();

				vertices.push_back({ origin,                 normal, { 0.0f, 0.0f } });
				vertices.push_back({ origin + right,         normal, { 1.0f, 0.0f } });
				vertices.push_back({ origin + right + up,    normal, { 1.0f, 1.0f } });
				vertices.push_back({ origin + up,            normal, { 0.0f, 1.0f } });

				for (uint32_t i : { 0u, 1u, 2u, 2u, 3u, 0u })
					indices.push_back(base + i);
			}
		}

		void Cube(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
		{
			vertices.clear();
			indices.clear();

			// Six independent faces rather than eight shared corners: a cube's
			// normals are discontinuous at the edges, so sharing vertices would
			// average them and round off the lighting.
			const glm::vec3 h(0.5f);

			AddQuadFace(vertices, indices, { -h.x, -h.y,  h.z }, {  1, 0, 0 }, { 0, 1, 0 }, {  0,  0,  1 }); // front
			AddQuadFace(vertices, indices, {  h.x, -h.y, -h.z }, { -1, 0, 0 }, { 0, 1, 0 }, {  0,  0, -1 }); // back
			AddQuadFace(vertices, indices, { -h.x, -h.y, -h.z }, {  0, 0, 1 }, { 0, 1, 0 }, { -1,  0,  0 }); // left
			AddQuadFace(vertices, indices, {  h.x, -h.y,  h.z }, {  0, 0,-1 }, { 0, 1, 0 }, {  1,  0,  0 }); // right
			AddQuadFace(vertices, indices, { -h.x,  h.y,  h.z }, {  1, 0, 0 }, { 0, 0,-1 }, {  0,  1,  0 }); // top
			AddQuadFace(vertices, indices, { -h.x, -h.y, -h.z }, {  1, 0, 0 }, { 0, 0, 1 }, {  0, -1,  0 }); // bottom
		}

		void Plane(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
		{
			vertices.clear();
			indices.clear();
			// Ground plane: lies in XZ, faces up.
			AddQuadFace(vertices, indices, { -0.5f, 0.0f, 0.5f }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 });
		}

		void Quad(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
		{
			vertices.clear();
			indices.clear();
			// Upright quad in XY, facing +Z, matching the 2D renderer's quad.
			AddQuadFace(vertices, indices, { -0.5f, -0.5f, 0.0f }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
		}

		void Sphere(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
					uint32_t segments, uint32_t rings)
		{
			vertices.clear();
			indices.clear();

			segments = std::max(3u, segments);
			rings = std::max(2u, rings);

			// UV sphere. The seam is duplicated (segments + 1 columns) so the
			// texture coordinate can reach 1.0 without wrapping back to 0.
			for (uint32_t ring = 0; ring <= rings; ring++)
			{
				const float v = (float)ring / (float)rings;
				const float phi = v * glm::pi<float>();

				for (uint32_t segment = 0; segment <= segments; segment++)
				{
					const float u = (float)segment / (float)segments;
					const float theta = u * glm::two_pi<float>();

					glm::vec3 position{
						std::sin(phi) * std::cos(theta),
						std::cos(phi),
						std::sin(phi) * std::sin(theta)
					};

					// Unit sphere centred on the origin, so the position is the
					// normal.
					vertices.push_back({ position * 0.5f, glm::normalize(position), { u, 1.0f - v } });
				}
			}

			const uint32_t stride = segments + 1;
			for (uint32_t ring = 0; ring < rings; ring++)
			{
				for (uint32_t segment = 0; segment < segments; segment++)
				{
					const uint32_t a = ring * stride + segment;
					const uint32_t b = a + stride;

					indices.push_back(a);
					indices.push_back(b);
					indices.push_back(a + 1);

					indices.push_back(a + 1);
					indices.push_back(b);
					indices.push_back(b + 1);
				}
			}
		}

		void Cylinder(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
					  uint32_t segments)
		{
			vertices.clear();
			indices.clear();

			segments = std::max(3u, segments);
			const float halfHeight = 0.5f;
			const float radius = 0.5f;

			// Side wall. Its normals point outward, so it cannot share vertices
			// with the caps, whose normals point along Y.
			for (uint32_t i = 0; i <= segments; i++)
			{
				const float u = (float)i / (float)segments;
				const float theta = u * glm::two_pi<float>();
				const glm::vec3 normal{ std::cos(theta), 0.0f, std::sin(theta) };

				vertices.push_back({ { normal.x * radius, -halfHeight, normal.z * radius }, normal, { u, 0.0f } });
				vertices.push_back({ { normal.x * radius,  halfHeight, normal.z * radius }, normal, { u, 1.0f } });
			}

			for (uint32_t i = 0; i < segments; i++)
			{
				const uint32_t a = i * 2;
				indices.insert(indices.end(), { a, a + 1, a + 2, a + 2, a + 1, a + 3 });
			}

			// Caps, as triangle fans around a centre vertex.
			auto addCap = [&](float y, const glm::vec3& normal, bool flip)
			{
				const uint32_t centre = (uint32_t)vertices.size();
				vertices.push_back({ { 0.0f, y, 0.0f }, normal, { 0.5f, 0.5f } });

				for (uint32_t i = 0; i <= segments; i++)
				{
					const float theta = (float)i / (float)segments * glm::two_pi<float>();
					const float x = std::cos(theta);
					const float z = std::sin(theta);
					vertices.push_back({ { x * radius, y, z * radius }, normal,
										 { x * 0.5f + 0.5f, z * 0.5f + 0.5f } });
				}

				for (uint32_t i = 0; i < segments; i++)
				{
					const uint32_t a = centre + 1 + i;
					if (flip)
						indices.insert(indices.end(), { centre, a + 1, a });
					else
						indices.insert(indices.end(), { centre, a, a + 1 });
				}
			};

			addCap( halfHeight, {  0.0f,  1.0f, 0.0f }, false);
			addCap(-halfHeight, {  0.0f, -1.0f, 0.0f }, true);
		}
	}
}
