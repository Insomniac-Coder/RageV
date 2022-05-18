#include <rvpch.h>
#include "GraphicsInformation.h"

namespace RageV
{
	GraphicsInfo GraphicsInformation::m_GInfo;


	GraphicsInfo GraphicsInformation::GetGraphicsInfo()
	{
		return m_GInfo;
	}

	void GraphicsInformation::SetGraphicsInfo(GraphicsInfo& graphicsinfo)
	{
		m_GInfo = graphicsinfo;
	}


}