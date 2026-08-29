#include <rvpch.h>
#include "RuntimeIrradianceField.h"

#include <cmath>

namespace RageV
{
	using namespace RageV::RHI;

	bool RuntimeIrradianceField::Update(RHIDevice& device, const Vec3& cameraPosition)
	{
		// **Snapped to whole cells.** An unsnapped centre slides the grid by a
		// fraction of a cell, so every cell permanently describes somewhere
		// slightly other than where it is read and the solve spends its life
		// chasing an offset it can never close. Snapped, the error is always a
		// whole number of cells, which one sweep clears.
		const Vec3 snapped(
			std::floor(cameraPosition.x / kSpacing + 0.5f) * kSpacing,
			std::floor(cameraPosition.y / kSpacing + 0.5f) * kSpacing,
			std::floor(cameraPosition.z / kSpacing + 0.5f) * kSpacing);

		if (!m_Volume)
		{
			m_Region = IrradianceVolume::Region{};
			m_Region.Centre = snapped;
			m_Region.Extents = Vec3(kCellsX * kSpacing * 0.5f,
									kCellsY * kSpacing * 0.5f,
									kCellsZ * kSpacing * 0.5f);
			// Axis aligned: it follows the view, it does not turn with it. A
			// grid that rotated would have every cell describing a different
			// place each time the camera looked around -- a cache that never
			// holds anything.
			m_Region.Rotation = Mat3(1.0f);
			m_Region.Width = kCellsX;
			m_Region.Height = kCellsY;
			m_Region.Depth = kCellsZ;
			m_Region.ZOffset = 0;
			m_Region.Spacing = kSpacing;

			m_Regions.assign(1, m_Region);

			Ref<IrradianceVolume> placed =
				IrradianceVolume::CreateAtlas(device, m_Regions);
			if (!placed)
				return false;   // no memory; the caller carries on without a field

			m_Volume = placed;
			m_Centre = snapped;
			// A new grid is black, so the baked field -- if the scene has one --
			// stays bound until this has been round once.
			m_Warm = false;
			return true;
		}

		// **Standing still is the common case and costs nothing.**
		if (Math::Length(snapped - m_Centre) < kRecentreDistance * 0.5f)
			return true;

		// **Moved: the box travels, the texture stays.**
		//
		// This is the whole point of carrying the region separately. The volume
		// is never rebuilt, so every cell solved so far survives -- what changes
		// is only where the grid claims to be. The cells are then describing
		// places a whole number of cells off, which the continuous sweep
		// corrects within a few frames; before this they were simply gone, and
		// the scene went dark while they refilled.
		//
		// `m_Warm` deliberately stays true. The grid is populated; it is not
		// starting again, and clearing it would put the handover back into a
		// state it has already left.
		m_Region.Centre = snapped;
		m_Regions.assign(1, m_Region);
		m_Centre = snapped;
		return true;
	}

	void RuntimeIrradianceField::Release()
	{
		m_Volume.reset();
		m_Regions.clear();
		m_Region = IrradianceVolume::Region{};
		m_Centre = Vec3(0.0f);
		m_Warm = false;
	}
}
