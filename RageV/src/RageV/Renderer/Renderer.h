#pragma once

namespace RageV
{

	enum class RendererAPI
	{
		None = 0,
		OpenGL = 1
	};


	class Renderer
	{
	public:
		inline static RendererAPI& GetAPI() { return m_API; }

	private:
		static RendererAPI m_API;
	};
}