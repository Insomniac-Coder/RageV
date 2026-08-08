// Vulkan backend smoke test. Brings up a window, creates the device, compiles
// the real quad shader, builds a pipeline and descriptor sets from its
// reflection, and renders a fixed number of frames. Exercises the whole path
// -- swapchain, frames in flight, staging uploads, descriptor updates, dynamic
// rendering and present -- without needing the editor.
//
//   vksmoke [frameCount]
#include <rvpch.h>
#include "RageV/Core/Log.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace RageV;
using namespace RageV::RHI;

namespace
{
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec4 Color;
		glm::vec2 TexCoord;
		float     TextureIndex;
		float     TilingFactor;
	};
	static_assert(sizeof(Vertex) == 56, "Vertex must match the reflected 56-byte stride");

	// Mirrors the std140 SceneData block in quad.rvshader.
	struct SceneData
	{
		glm::mat4 ViewProjection;
		glm::vec4 CameraPosition;
		glm::vec4 LightPositions[8];
		glm::vec4 LightColors[8];
		int32_t   LightCount;
		int32_t   _pad[3];
	};
	static_assert(sizeof(SceneData) >= 340, "SceneData must cover the reflected 340-byte block");
}

int main(int argc, char** argv)
{
	Log::Init();

	const int frameCount = argc > 1 ? atoi(argv[1]) : 120;

	if (!glfwInit())
	{
		RV_CORE_ERROR("glfwInit failed");
		return 1;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(1280, 720, "RageV Vulkan smoke test", nullptr, nullptr);
	if (!window)
	{
		RV_CORE_ERROR("window creation failed");
		return 1;
	}

	DeviceDesc deviceDesc;
	deviceDesc.Backend = Backend::Vulkan;
	deviceDesc.Window = window;
	deviceDesc.Width = 1280;
	deviceDesc.Height = 720;
	deviceDesc.VSync = true;
	deviceDesc.EnableValidation = true;
	deviceDesc.FramesInFlight = 2;

	auto device = RHIDevice::Create(deviceDesc);
	if (!device)
	{
		RV_CORE_ERROR("device creation failed");
		return 1;
	}

	const DeviceCaps& caps = device->GetCaps();
	RV_CORE_INFO("Device: {0}", caps.DeviceName);
	RV_CORE_INFO("API: {0}", caps.APIName);
	RV_CORE_INFO("VRAM: {0} MB", caps.VideoMemoryBytes / (1024 * 1024));
	RV_CORE_INFO("Max texture size: {0}, max slots: {1}", caps.MaxTextureSize, caps.MaxTextureSlots);

	ShaderCompiler::Init();
	auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/quad.rvshader");
	if (!compiled)
	{
		RV_CORE_ERROR("shader compilation failed");
		return 1;
	}

	auto shader = device->CreateShader(*compiled);

	GraphicsPipelineDesc pipelineDesc;
	pipelineDesc.Name = "quad";
	pipelineDesc.Shader = shader;
	pipelineDesc.Topology = PrimitiveTopology::TriangleList;
	pipelineDesc.Rasterizer.Cull = CullMode::None;
	pipelineDesc.Blend = BlendPreset::AlphaBlend;
	pipelineDesc.ColorFormats = { device->GetSwapchainFormat() };
	pipelineDesc.DepthFormat = device->GetSwapchainDepthFormat();

	auto pipeline = device->CreatePipeline(pipelineDesc);
	RV_CORE_INFO("Pipeline created");

	// Two quads so the batch touches more than one primitive.
	const glm::vec4 corners[4] = {
		{ -0.5f, -0.5f, 0.0f, 1.0f }, {  0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f, 0.0f, 1.0f }, { -0.5f,  0.5f, 0.0f, 1.0f },
	};
	const glm::vec2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	auto pushQuad = [&](const glm::mat4& transform, const glm::vec4& color)
	{
		const uint32_t base = (uint32_t)vertices.size();
		for (int i = 0; i < 4; i++)
		{
			Vertex vertex{};
			vertex.Position = glm::vec3(transform * corners[i]);
			vertex.Normal = { 0.0f, 0.0f, -1.0f };
			vertex.Color = color;
			vertex.TexCoord = uvs[i];
			vertex.TextureIndex = 0.0f;
			vertex.TilingFactor = 1.0f;
			vertices.push_back(vertex);
		}
		for (uint32_t i : { 0u, 1u, 2u, 2u, 3u, 0u })
			indices.push_back(base + i);
	};

	pushQuad(glm::translate(glm::mat4(1.0f), { -0.6f, 0.0f, 0.0f }), { 0.9f, 0.2f, 0.2f, 1.0f });
	pushQuad(glm::translate(glm::mat4(1.0f), {  0.6f, 0.0f, 0.0f }), { 0.2f, 0.5f, 0.9f, 1.0f });

	BufferDesc vertexDesc;
	vertexDesc.Size = vertices.size() * sizeof(Vertex);
	vertexDesc.Usage = BufferUsage::Vertex;
	vertexDesc.Memory = MemoryDomain::HostVisible;
	vertexDesc.DebugName = "quad.vertices";
	auto vertexBuffer = device->CreateBuffer(vertexDesc);
	vertexBuffer->Upload(vertices.data(), vertexDesc.Size);

	// Device-local on purpose: exercises the staging-copy path.
	BufferDesc indexDesc;
	indexDesc.Size = indices.size() * sizeof(uint32_t);
	indexDesc.Usage = BufferUsage::Index;
	indexDesc.Memory = MemoryDomain::DeviceLocal;
	indexDesc.DebugName = "quad.indices";
	auto indexBuffer = device->CreateBuffer(indexDesc);
	indexBuffer->Upload(indices.data(), indexDesc.Size);

	BufferDesc uniformDesc;
	uniformDesc.Size = sizeof(SceneData);
	uniformDesc.Usage = BufferUsage::Uniform;
	uniformDesc.Memory = MemoryDomain::HostVisible;
	uniformDesc.DebugName = "scene.ubo";
	auto uniformBuffer = device->CreateBuffer(uniformDesc);

	// 1x1 white texture, uploaded through the staging path.
	TextureDesc whiteDesc;
	whiteDesc.Width = 1;
	whiteDesc.Height = 1;
	whiteDesc.Format = Format::R8G8B8A8_UNORM;
	whiteDesc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
	whiteDesc.DebugName = "white";
	auto white = device->CreateTexture(whiteDesc);
	const uint32_t whitePixel = 0xffffffff;
	white->Upload(&whitePixel, sizeof(whitePixel));

	auto sampler = device->CreateSampler(SamplerDesc{});

	auto sceneSet = device->CreateResourceSet(pipeline, 0);
	auto textureSet = device->CreateResourceSet(pipeline, 1);

	RV_CORE_INFO("Resources created; rendering {0} frames", frameCount);

	int rendered = 0;
	int skipped = 0;

	for (int frame = 0; frame < frameCount && !glfwWindowShouldClose(window); frame++)
	{
		glfwPollEvents();

		RHICommandList* cmd = device->BeginFrame();
		if (!cmd)
		{
			skipped++;
			continue;
		}

		SceneData scene{};
		const float aspect = (float)device->GetSwapchainWidth() / (float)device->GetSwapchainHeight();
		scene.ViewProjection = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);
		scene.CameraPosition = { 0.0f, 0.0f, 1.0f, 0.0f };
		scene.LightCount = 0;
		uniformBuffer->Upload(&scene, sizeof(scene));

		sceneSet->SetUniformBuffer(0, uniformBuffer, 0, sizeof(SceneData));
		sceneSet->Commit();

		// Every element of the sampler array must be written, even though the
		// draw only reads slot 0: the shader indexes it dynamically, so
		// validation treats all 32 as potentially accessed.
		for (uint32_t slot = 0; slot < 32; slot++)
			textureSet->SetTexture(0, white, sampler, slot);
		textureSet->Commit();

		cmd->PushDebugGroup("quads");

		RenderPassBeginInfo pass;
		pass.Target = nullptr;   // swapchain
		pass.Clear.Color[0] = 0.06f;
		pass.Clear.Color[1] = 0.06f;
		pass.Clear.Color[2] = 0.08f;
		pass.Clear.Color[3] = 1.0f;
		cmd->BeginRenderPass(pass);

		cmd->BindPipeline(pipeline);
		cmd->BindResourceSet(0, sceneSet);
		cmd->BindResourceSet(1, textureSet);
		cmd->BindVertexBuffer(0, vertexBuffer);
		cmd->BindIndexBuffer(indexBuffer, IndexType::UInt32);
		cmd->DrawIndexed((uint32_t)indices.size());

		cmd->EndRenderPass();
		cmd->PopDebugGroup();

		device->EndFrame();
		rendered++;
	}

	device->WaitIdle();

	RV_CORE_INFO("Rendered {0} frames, skipped {1}", rendered, skipped);

	// Resources must be released before the device: their destructors defer
	// through it.
	sceneSet.reset();
	textureSet.reset();
	white.reset();
	sampler.reset();
	uniformBuffer.reset();
	indexBuffer.reset();
	vertexBuffer.reset();
	pipeline.reset();
	shader.reset();
	device.reset();

	ShaderCompiler::Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	RV_CORE_INFO("OK");
	return rendered > 0 ? 0 : 1;
}
