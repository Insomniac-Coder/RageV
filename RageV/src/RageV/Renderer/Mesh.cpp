#include <rvpch.h>
#include "Mesh.h"
#include "RageV/Math/Math.h"

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
		// The acceleration-structure input usage is asked for unconditionally
		// and dropped by a device that cannot trace (ENGINE-NOTES 7am): it is a
		// creation-time property, and a mesh made without it can never be
		// traced, so every mesh carries it where it means anything.
		BufferDesc vertexDesc;
		vertexDesc.Size = vertices.size() * sizeof(MeshVertex);
		vertexDesc.Usage = BufferUsage::Vertex | BufferUsage::AccelerationStructureInput;
		vertexDesc.Memory = MemoryDomain::DeviceLocal;
		vertexDesc.DebugName = debugName + ".vertices";
		m_VertexBuffer = device.CreateBuffer(vertexDesc);
		m_VertexBuffer->Upload(vertices.data(), vertexDesc.Size);
		m_VertexCount = (uint32_t)vertices.size();
		m_VertexStride = sizeof(MeshVertex);

		BufferDesc indexDesc;
		indexDesc.Size = indices.size() * sizeof(uint32_t);
		indexDesc.Usage = BufferUsage::Index | BufferUsage::AccelerationStructureInput;
		indexDesc.Memory = MemoryDomain::DeviceLocal;
		indexDesc.DebugName = debugName + ".indices";
		m_IndexBuffer = device.CreateBuffer(indexDesc);
		m_IndexBuffer->Upload(indices.data(), indexDesc.Size);

		// Kept for picking and, later, culling. Extracted here rather than
		// asked for again, since this is the one moment the data is in hand.
		m_Indices = indices;
		m_Positions.reserve(vertices.size());

		// Seeded from the first vertex rather than from infinities: an empty
		// mesh then has a zero-sized box at the origin instead of an inverted
		// one, and an inverted box passes every intersection test.
		if (!vertices.empty())
			m_Bounds.Min = m_Bounds.Max = vertices[0].Position;

		for (const MeshVertex& vertex : vertices)
		{
			m_Positions.push_back(vertex.Position);
			m_Bounds.Min = Math::Min(m_Bounds.Min, vertex.Position);
			m_Bounds.Max = Math::Max(m_Bounds.Max, vertex.Position);
		}
	}

	Mesh::Mesh(RHIDevice& device,
			   const std::vector<SkinnedVertex>& vertices,
			   const std::vector<uint32_t>& indices,
			   const std::string& debugName)
		: m_IndexCount((uint32_t)indices.size()), m_Skinned(true)
	{
		BufferDesc vertexDesc;
		vertexDesc.Size = vertices.size() * sizeof(SkinnedVertex);
		vertexDesc.Usage = BufferUsage::Vertex | BufferUsage::AccelerationStructureInput;
		vertexDesc.Memory = MemoryDomain::DeviceLocal;
		vertexDesc.DebugName = debugName + ".skinnedvertices";
		m_VertexBuffer = device.CreateBuffer(vertexDesc);
		m_VertexBuffer->Upload(vertices.data(), vertexDesc.Size);
		m_VertexCount = (uint32_t)vertices.size();
		m_VertexStride = sizeof(SkinnedVertex);

		BufferDesc indexDesc;
		indexDesc.Size = indices.size() * sizeof(uint32_t);
		indexDesc.Usage = BufferUsage::Index | BufferUsage::AccelerationStructureInput;
		indexDesc.Memory = MemoryDomain::DeviceLocal;
		indexDesc.DebugName = debugName + ".indices";
		m_IndexBuffer = device.CreateBuffer(indexDesc);
		m_IndexBuffer->Upload(indices.data(), indexDesc.Size);

		m_Indices = indices;
		m_Positions.reserve(vertices.size());

		if (!vertices.empty())
			m_Bounds.Min = m_Bounds.Max = vertices[0].Position;

		// The *bind pose* bounds, which is the honest thing to store and not
		// the whole truth: a limb swinging out leaves this box, so a skinned
		// mesh can be culled while part of it is still on screen. Fixing that
		// properly means bounds per clip, or a box grown to cover every pose.
		// Recorded in HANDOFF section 9 rather than pretended about.
		for (const SkinnedVertex& vertex : vertices)
		{
			m_Positions.push_back(vertex.Position);
			m_Bounds.Min = Math::Min(m_Bounds.Min, vertex.Position);
			m_Bounds.Max = Math::Max(m_Bounds.Max, vertex.Position);
		}
	}

	namespace
	{
		std::unordered_map<uint32_t, Ref<Mesh>> s_PrimitiveCache;
	}

	const Ref<RHIAccelerationStructure>& Mesh::GetAccelerationStructure(RHIDevice& device)
	{
		// Once, whichever way it goes: a device that cannot trace answers null
		// on the first ask, and asking again every frame would be asking the
		// same question of the same device.
		if (!m_BlasTried)
		{
			m_BlasTried = true;
			if (device.GetCaps().SupportsRayQuery && m_VertexBuffer && m_IndexBuffer && m_IndexCount >= 3)
			{
				AccelerationGeometryDesc geometry;
				geometry.Vertices = m_VertexBuffer;
				geometry.VertexStride = m_VertexStride;
				geometry.VertexCount = m_VertexCount;
				geometry.Indices = m_IndexBuffer;
				geometry.Type = IndexType::UInt32;
				geometry.IndexCount = m_IndexCount;
				geometry.DebugName = m_VertexBuffer->GetDesc().DebugName + ".blas";
				m_Blas = device.CreateBottomLevelAS(geometry);
			}
		}
		return m_Blas;
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
							 const Vec3& origin, const Vec3& right, const Vec3& up,
							 const Vec3& normal)
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
			const Vec3 h(0.5f);

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

			segments = Math::Max(3u, segments);
			rings = Math::Max(2u, rings);

			// UV sphere. The seam is duplicated (segments + 1 columns) so the
			// texture coordinate can reach 1.0 without wrapping back to 0.
			for (uint32_t ring = 0; ring <= rings; ring++)
			{
				const float v = (float)ring / (float)rings;
				const float phi = v * Math::Pi;

				for (uint32_t segment = 0; segment <= segments; segment++)
				{
					const float u = (float)segment / (float)segments;
					const float theta = u * Math::TwoPi;

					Vec3 position{
						Math::Sin(phi) * Math::Cos(theta),
						Math::Cos(phi),
						Math::Sin(phi) * Math::Sin(theta)
					};

					// Unit sphere centred on the origin, so the position is the
					// normal.
					vertices.push_back({ position * 0.5f, Math::Normalize(position), { u, 1.0f - v } });
				}
			}

			const uint32_t stride = segments + 1;
			for (uint32_t ring = 0; ring < rings; ring++)
			{
				for (uint32_t segment = 0; segment < segments; segment++)
				{
					const uint32_t a = ring * stride + segment;
					const uint32_t b = a + stride;

					// Counter-clockwise seen from outside, which is what the
					// pipeline calls front-facing. Wound the other way for a
					// long time: back-face culling then kept the near hemisphere
					// and drew the far one's inside, which has the same
					// silhouette and only looks wrong once anything reads the
					// normal -- so it survived until surfaces started
					// reflecting.
					indices.push_back(a);
					indices.push_back(a + 1);
					indices.push_back(b);

					indices.push_back(a + 1);
					indices.push_back(b + 1);
					indices.push_back(b);
				}
			}
		}

		void Cylinder(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
					  uint32_t segments)
		{
			vertices.clear();
			indices.clear();

			segments = Math::Max(3u, segments);
			const float halfHeight = 0.5f;
			const float radius = 0.5f;

			// Side wall. Its normals point outward, so it cannot share vertices
			// with the caps, whose normals point along Y.
			for (uint32_t i = 0; i <= segments; i++)
			{
				const float u = (float)i / (float)segments;
				const float theta = u * Math::TwoPi;
				const Vec3 normal{ Math::Cos(theta), 0.0f, Math::Sin(theta) };

				vertices.push_back({ { normal.x * radius, -halfHeight, normal.z * radius }, normal, { u, 0.0f } });
				vertices.push_back({ { normal.x * radius,  halfHeight, normal.z * radius }, normal, { u, 1.0f } });
			}

			for (uint32_t i = 0; i < segments; i++)
			{
				const uint32_t a = i * 2;
				indices.insert(indices.end(), { a, a + 1, a + 2, a + 2, a + 1, a + 3 });
			}

			// Caps, as triangle fans around a centre vertex.
			auto addCap = [&](float y, const Vec3& normal, bool flip)
			{
				const uint32_t centre = (uint32_t)vertices.size();
				vertices.push_back({ { 0.0f, y, 0.0f }, normal, { 0.5f, 0.5f } });

				for (uint32_t i = 0; i <= segments; i++)
				{
					const float theta = (float)i / (float)segments * Math::TwoPi;
					const float x = Math::Cos(theta);
					const float z = Math::Sin(theta);
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

			// The fan runs anticlockwise about +Y, so seen from above -- from
			// outside the top cap -- it runs clockwise, and has to be flipped.
			// The bottom cap is the one that needs no flip. These two were the
			// wrong way round, which culled both caps and left a tube.
			addCap( halfHeight, {  0.0f,  1.0f, 0.0f }, true);
			addCap(-halfHeight, {  0.0f, -1.0f, 0.0f }, false);
		}
	}
}
