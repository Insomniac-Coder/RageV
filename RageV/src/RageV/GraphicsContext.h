#pragma once

namespace RageV
{

	class GraphicsContext 
	{
	public:
		virtual void SwapBuffers() = 0;
		virtual void Init() = 0;
	};

}