#include <rvpch.h>
#include "RuntimeIrradianceField.h"

#include <cmath>
#include <vector>

namespace RageV
{
	using namespace RageV::RHI;

	bool RuntimeIrradianceField::Update(RHIDevice& device, const Vec3& cameraPosition)
	{
		// **Snapped to whole cells.** An unsnapped centre slides the grid by a
		// fraction of a cell every time it is placed, so every cell permanently
		// describes somewhere slightly other than where it is read, and the
		// solve spends its whole life chasing an offset it can never close.
		// Snapping makes the error a whole number of cells, which one sweep
		// fixes.
		const Vec3 snapped(
			std::floor(cameraPosition.x / kSpacing + 0.5f) * kSpacing,
			std::floor(cameraPosition.y / kSpacing + 0.5f) * kSpacing,
			std::floor(cameraPosition.z / kSpacing + 0.5f) * kSpacing);

		// Standing and still covering the view: nothing to do, and in
		// particular nothing that would discard what has been solved.
		if (m_Volume
			&& Math::Length(cameraPosition - m_Centre) <= kRecentreDistance)
		{
			return true;
		}

		IrradianceVolume::Region region;
		region.Centre = snapped;
		region.Extents = Vec3(kCellsX * kSpacing * 0.5f,
							  kCellsY * kSpacing * 0.5f,
							  kCellsZ * kSpacing * 0.5f);
		// Axis aligned: it follows the view, it does not turn with it. A grid
		// that rotated would have every cell describing a different place each
		// time the camera looked around, which is a cache that never holds.
		region.Rotation = Mat3(1.0f);
		region.Width = kCellsX;
		region.Height = kCellsY;
		region.Depth = kCellsZ;
		region.ZOffset = 0;
		region.Spacing = kSpacing;

		const std::vector<IrradianceVolume::Region> regions{ region };
		Ref<IrradianceVolume> placed = IrradianceVolume::CreateAtlas(device, regions);
		if (!placed)
		{
			// Out of memory, or a device that cannot make the textures. Keep
			// whatever is standing rather than going dark: a stale box is a
			// better picture than none, and the next call tries again.
			return m_Volume != nullptr;
		}

		m_Volume = placed;
		m_Centre = snapped;
		// A new grid is black, so the handover starts over: the baked field, if
		// the scene has one, stays bound until this has been round once.
		m_Warm = false;
		return true;
	}

	void RuntimeIrradianceField::Release()
	{
		m_Volume.reset();
		m_Centre = Vec3(0.0f);
		m_Warm = false;
	}
}
