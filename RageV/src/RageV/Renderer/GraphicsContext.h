#pragma once
#include <string>
#include "RageV/Core/GraphicsInformation.h"

namespace RageV
{
	

	class GraphicsContext 
	{
	public:
		virtual void SwapBuffers() = 0;
		virtual void Init() = 0;
		virtual const GraphicsInfo& GetGraphicsInfo() const = 0;
	};

}