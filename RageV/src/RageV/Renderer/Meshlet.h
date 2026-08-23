#pragma once
#include "RageV/Math/Math.h"

#include <cstdint>
#include <vector>

namespace RageV
{
	// A mesh cut into meshlets: runs of triangles small enough that a mesh
	// shader work group can own one outright (roadmap 8.3, second half).
	//
	// **Why cut at all.** A vertex pipeline is fed triangles one index at a
	// time and the hardware guesses at reuse through a cache; a mesh shader is
	// handed a whole cluster, loads each unique vertex exactly once, and can
	// throw the entire cluster away with one bounding-sphere test before any
	// vertex work happens. The cut is what turns "cull per draw" into "cull
	// per 124 triangles" without the CPU hearing about any of it.
	//
	// The limits are the EXT_mesh_shader sweet spot NVIDIA and AMD both
	// document: 64 vertices fills a work group one vertex per invocation, and
	// 124 triangles keeps the primitive indices -- packed one triangle per
	// 32-bit word -- inside the per-meshlet output budget every current
	// device offers.
	//
	// Plain sequential greedy, not a cache optimiser: triangles are taken in
	// index order and a meshlet closes when the next triangle would burst
	// either limit. Import order in this engine is already locality-friendly
	// (glTF and ufbx both emit strips of nearby faces), and correctness here
	// is a *coverage* property -- every triangle in exactly one meshlet --
	// which the plain builder makes easy to prove and scenetest asserts.
	struct Meshlet
	{
		// Where this meshlet's entries start in MeshletData's two arrays.
		uint32_t VertexOffset = 0;
		uint32_t TriangleOffset = 0;
		uint32_t VertexCount = 0;
		uint32_t TriangleCount = 0;

		// Object-space bounding sphere of the meshlet's vertices, xyz centre
		// and w radius. What the mesh stage culls against, so it must contain
		// every vertex -- scenetest holds it to that.
		Vec4 Sphere{ 0.0f };
	};

	struct MeshletData
	{
		static constexpr uint32_t kMaxVertices = 64;
		static constexpr uint32_t kMaxTriangles = 124;

		std::vector<Meshlet> Meshlets;

		// Per meshlet-vertex: the index into the mesh's own vertex buffer.
		std::vector<uint32_t> Vertices;

		// Per triangle: three *local* vertex slots packed a byte each --
		// local fits in a byte because kMaxVertices is 64.
		std::vector<uint32_t> Triangles;
	};

	// Cuts `indices` (triangle list, three per face) into meshlets. Positions
	// are read at `positionStrideFloats` apart, x y z first -- the layout
	// MeshVertex and SkinnedVertex agree on. An index count that is not a
	// multiple of three loses the remainder, exactly as a draw would.
	MeshletData BuildMeshlets(const float* positions, uint32_t positionStrideFloats,
							  const uint32_t* indices, uint32_t indexCount);
}
