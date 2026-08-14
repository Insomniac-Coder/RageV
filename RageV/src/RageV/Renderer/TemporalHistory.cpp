#include <rvpch.h>
#include "TemporalHistory.h"

namespace RageV
{
	using namespace RageV::RHI;

	void TemporalHistory::Prepare(RHIDevice& device, uint32_t width, uint32_t height,
								  Format format)
	{
		if (width == 0 || height == 0)
		{
			// A viewport dragged shut. Not an error, and not a reason to
			// destroy anything -- but the history no longer describes
			// anything, so say so.
			m_Valid = false;
			return;
		}

		if (m_Targets[0] && m_Width == width && m_Height == height && m_Format == format)
			return;

		RenderTargetDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.ColorAttachments = { { format } };
		// No depth. Nothing in a temporal resolve tests or writes it, and an
		// attachment nobody uses would still force every pipeline drawn here
		// to declare a matching depth format.
		desc.HasDepth = false;

		desc.DebugName = "TemporalHistoryA";
		m_Targets[0] = device.CreateRenderTarget(desc);
		desc.DebugName = "TemporalHistoryB";
		m_Targets[1] = device.CreateRenderTarget(desc);

		m_Width = width;
		m_Height = height;
		m_Format = format;

		// Freshly allocated images hold whatever the driver left in them.
		// Blending against that would put one frame of somebody else's memory
		// into the picture, fading out over the next thirty.
		m_Valid = false;
	}

	void TemporalHistory::Advance()
	{
		if (!m_Targets[0])
			return;

		m_Cursor ^= 1;

		// What was just written becomes what the next frame reads, so from
		// here on there is a history. Set after the swap rather than before,
		// because before the swap "valid" would describe the wrong target.
		m_Valid = true;
	}

	void TemporalHistory::Release()
	{
		m_Targets[0].reset();
		m_Targets[1].reset();
		m_Width = 0;
		m_Height = 0;
		m_Format = Format::Undefined;
		m_Valid = false;
	}
}
