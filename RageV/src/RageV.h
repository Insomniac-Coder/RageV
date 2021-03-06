#pragma once

// For use by applications
#include "RageV/Core/Log.h"
#include "RageV/Core/Application.h"

//Layer stuff
#include "RageV/Core/Layer.h"
#include "RageV/ImGui/ImGuiLayer.h"

//Input polling stuff
#include "RageV/Core/Input.h"
#include "RageV/Core/KeyCodes.h"
#include "RageV/Core/MouseButtonCodes.h"

#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/RenderCommand.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Renderer/OrthographicCamera.h"
#include "RageV/Renderer/Buffer.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "RageV/Renderer/Shader.h"
#include "RageV/Renderer/Texture.h"
#include "RageV/Renderer/Framebuffer.h"
#include "Platform/OpenGL/OpenGLFrameBuffer.h"
#include "RageV/Renderer/OrthographicCameraController.h"
#include "RageV/Core/GraphicsInformation.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/SceneSerializer.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/ScriptableEntity.h"
#include "RageV/Scene/Components.h"

#include "RageV/Core/Timestep.h"
#include "RageV/Core/Timer.h"