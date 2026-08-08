#pragma once
#include <string>
#include "RageV/Core/GraphicsInformation.h"

namespace RageV
{
	

	class GraphicsContext
	{
	public:
		virtual ~GraphicsContext() = default;
		virtual void SwapBuffers() = 0;
		virtual void Init() = 0;
		// Returned by value: implementations build this from driver queries, so
		// handing back a reference dangles the moment the call returns.
		virtual GraphicsInfo GetGraphicsInfo() const = 0;
	};

}