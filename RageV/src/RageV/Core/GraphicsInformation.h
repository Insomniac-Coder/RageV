#pragma once
#include <string>

namespace RageV
{
	struct GraphicsInfo
	{
		std::string GPUName;
		std::string APIName;
	};
	class GraphicsInformation
	{
		friend class Application;
	public:
		static GraphicsInfo GetGraphicsInfo();
	private:
		static void SetGraphicsInfo(GraphicsInfo& graphicsinfo);
		static GraphicsInfo m_GInfo;
	};

}