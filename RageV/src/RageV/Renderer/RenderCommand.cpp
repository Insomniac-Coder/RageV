#include <rvpch.h>
#include "RenderCommand.h"
#include "Platform/OpenGL/OpenGLRenderAPI.h"

namespace RageV
{

	RenderAPI* RenderCommand::m_RenderAPI = new OpenGLRenderAPI;

}