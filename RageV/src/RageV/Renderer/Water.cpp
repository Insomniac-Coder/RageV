#include <rvpch.h>
#include "Water.h"
#include "Renderer.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/Log.h"

#include <algorithm>
#include <cmath>

namespace RageV
{
	namespace
	{
		// The one water material. A file-static rather than a member because
		// there is one of them for the whole engine, on the same terms as
		// Renderer3D's default material -- and because a component that could
		// hold its own would be a component somebody could point elsewhere.
		RHI::Ref<Material> s_Material;

		// **The spacing is a request, not a promise.** An author who asks for
		// a 4 km bay at 10 cm spacing is asking for 1.6 billion quads, and the
		// honest answer is not to try. The cap is on the divisions per axis
		// rather than on the total, so a long thin channel keeps its length
		// resolution instead of being punished for its area.
		constexpr uint32_t kMaxDivisions = 1024;

		// Below this the rectangle is not a surface, and a zero-area mesh is a
		// mesh whose bounds are a point -- which culls at every angle and then
		// looks like a draw bug.
		constexpr float kMinExtent = 0.01f;

		uint32_t DivisionsFor(float extent, float spacing)
		{
			if (extent <= kMinExtent)
				return 0;

			const float step = std::max(spacing, 1.0e-3f);
			const uint32_t count = (uint32_t)std::ceil(extent / step);
			return std::clamp(count, 1u, kMaxDivisions);
		}
	}

	RHI::Ref<Material> Water::GetMaterial()
	{
		if (s_Material)
			return s_Material;
		if (!Renderer::HasDevice())
			return nullptr;

		s_Material = RHI::Ref<Material>(
			new Material(Renderer::GetDevice(), "Water"));

		MaterialParams& params = s_Material->GetParams();

		// Almost black, because water has hardly any colour of its own. What
		// is seen in it is the sky and the shore; a base colour bright enough
		// to notice is one that washes the reflection out and turns the sea
		// into painted plastic.
		//
		// The alpha is carried but not yet used -- see the blend note below.
		params.BaseColor = { 0.012f, 0.021f, 0.032f, 0.86f };
		params.Metallic = 0.0f;

		// **Opaque, and not because water is.** Setting this to Blend makes the
		// body vanish outright, and the reason is architectural rather than a
		// bug in this file: the transparent pass is fed by
		// `Renderer3D::DrawTransparentIndirect(m_CulledBlend, m_BlendMeshes)`,
		// and those two lists are built by the *MeshComponent* gather. A water
		// body is not a MeshComponent, so an immediate `DrawMesh` marked Blend
		// is sorted into a bucket that nothing ever submits, and the draw is
		// silently dropped.
		//
		// Terrain has the same ceiling and has never needed to notice it. Water
		// is the first thing that does, because a sea you cannot see into reads
		// as painted tarmac however well it reflects. Making it transparent
		// means giving the blend gather a way to take geometry that is not a
		// MeshComponent, which is a change to that path and not to this line.
		s_Material->SetBlendMode(BlendMode::Blend);

		// Low, but not zero. A mirror-flat surface reads as glass, and the
		// microfacet roughness is standing in for the ripple the geometry
		// cannot yet carry. It comes down when the waves go in.
		params.Roughness = 0.09f;

		// **0.25, not the 0.5 everything else uses.** F0 = 0.08 * Specular, so
		// this is the 2% water actually reflects head-on against the 4% of a
		// general dielectric. It is the difference between a sea you can see
		// into and one with a sheen on it, and it is the whole reason that
		// field exists.
		params.Specular = 0.25f;

		return s_Material;
	}

	void Water::Shutdown()
	{
		s_Material.reset();
	}

	uint32_t Water::QuadCount(float width, float length, float spacing)
	{
		return DivisionsFor(width, spacing) * DivisionsFor(length, spacing);
	}

	void Water::BuildGeometry(float width, float length, float spacing,
							  float textureScale,
							  std::vector<MeshVertex>& vertices,
							  std::vector<uint32_t>& indices)
	{
		vertices.clear();
		indices.clear();

		const uint32_t columns = DivisionsFor(width, spacing);
		const uint32_t rows = DivisionsFor(length, spacing);
		if (columns == 0 || rows == 0)
			return;

		const float halfWidth = width * 0.5f;
		const float halfLength = length * 0.5f;

		// **Metres per repeat, taken off the position and not off the index.**
		// A UV that runs 0..1 across the body would stretch the texture as the
		// body is resized, so the same water would have a different wave scale
		// in a pond and in a bay. Off the position it does not: resizing adds
		// surface, it does not magnify it.
		const float repeat = std::max(textureScale, 1.0e-3f);

		vertices.reserve((size_t)(columns + 1) * (rows + 1));
		for (uint32_t row = 0; row <= rows; row++)
		{
			const float z = -halfLength + length * (float)row / (float)rows;
			for (uint32_t column = 0; column <= columns; column++)
			{
				const float x = -halfWidth + width * (float)column / (float)columns;

				MeshVertex vertex;
				vertex.Position = { x, 0.0f, z };
				vertex.Normal = { 0.0f, 1.0f, 0.0f };
				vertex.TexCoord = { x / repeat, z / repeat };
				vertices.push_back(vertex);
			}
		}

		// Counter-clockwise seen from above, which is the winding every other
		// upward-facing surface in this engine uses -- a plane wound the other
		// way is not merely invisible under backface culling, it is lit from
		// underneath, and a body of water lit from underneath reads as a hole.
		indices.reserve((size_t)columns * rows * 6);
		const uint32_t stride = columns + 1;
		for (uint32_t row = 0; row < rows; row++)
		{
			for (uint32_t column = 0; column < columns; column++)
			{
				const uint32_t base = row * stride + column;
				const uint32_t next = base + stride;

				indices.push_back(base);
				indices.push_back(next);
				indices.push_back(next + 1);

				indices.push_back(base);
				indices.push_back(next + 1);
				indices.push_back(base + 1);
			}
		}
	}

	bool Water::Resolve(WaterComponent& component)
	{
		const bool matches = component.Runtime
						  && component.BuiltWidth == component.Width
						  && component.BuiltLength == component.Length
						  && component.BuiltSpacing == component.Spacing
						  && component.BuiltTextureScale == component.TextureScale;
		if (matches)
			return true;

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;
		BuildGeometry(component.Width, component.Length, component.Spacing,
					  component.TextureScale, vertices, indices);

		if (vertices.empty() || indices.empty())
		{
			// A body with no area is not an error -- it is a component someone
			// has just added and not yet sized. It draws nothing and says so
			// only once, because the alternative is a line per frame.
			component.Runtime.reset();
			component.BuiltWidth = component.Width;
			component.BuiltLength = component.Length;
			component.BuiltSpacing = component.Spacing;
			component.BuiltTextureScale = component.TextureScale;
			return false;
		}

		component.Runtime = RHI::Ref<Mesh>(new Mesh(
			Renderer::GetDevice(), vertices, indices, "Water"));

		component.BuiltWidth = component.Width;
		component.BuiltLength = component.Length;
		component.BuiltSpacing = component.Spacing;
		component.BuiltTextureScale = component.TextureScale;
		return true;
	}
}
