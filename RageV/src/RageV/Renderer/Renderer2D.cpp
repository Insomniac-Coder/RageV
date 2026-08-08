#include <rvpch.h>
#include "Renderer2D.h"
#include "Buffer.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "RenderCommand.h"
#include "Perlin.h"
#include "stb_write_image.h"
#include "stb_image.h"

namespace RageV
{
	struct VertexData
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec4 Color;
		glm::vec2 TexCoord;
		float TextureID;
		float TilingFactor;
	};

	struct Renderer2DData {
		// Must match MAX_LIGHTS in assets/shaders/quadshader.glsl.
		static constexpr unsigned int MaxLights = 8;

		unsigned int MaxQuads = 10000;
		unsigned int MaxVertices = 4 * MaxQuads;
		unsigned int MaxIndicies = 6 * MaxQuads;
		unsigned int MaxTextureSlots = 32;
		unsigned int WhiteTextureSlotId = 0;
		unsigned int CurrentTextureSlotId = 1;

		std::vector<VertexData> QuadVerticiesStorage;
		VertexData* QuadVerticiesBuffer = nullptr;
		VertexData* QuadVerticiesPtr = nullptr;

		std::shared_ptr<VertexArray> VertexArray2D;
		std::shared_ptr<VertexBuffer> VertexBuffer2D;
		std::shared_ptr<IndexBuffer> IndexBuffer2D;
		unsigned int QuadCount = 0;
		unsigned int IndiciesCount = 0;
		unsigned int DrawCalls = 0;

		glm::vec4 QuadVerts[4];

		ShaderManager Renderer2DShaderManager;
		std::shared_ptr<Shader> QuadShader;
		std::shared_ptr<Texture2D> WhiteTexture;

		// Built once. These used to be concatenated with std::to_string on every
		// light, every frame.
		std::string LightPosNames[MaxLights];
		std::string LightColorNames[MaxLights];
	};

	static std::unique_ptr<Renderer2DData> renderer2DData;

	void Renderer2D::Init()
	{
		renderer2DData = std::make_unique<Renderer2DData>();

		renderer2DData->QuadVerts[0] = {-0.5f, -0.5f, 0.0f, 1.0f};
		renderer2DData->QuadVerts[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
		renderer2DData->QuadVerts[2] = { 0.5f, 0.5f, 0.0f, 1.0f };
		renderer2DData->QuadVerts[3] = { -0.5f, 0.5f, 0.0f, 1.0f };

		//Flat color tiles
		renderer2DData->VertexArray2D = RageV::VertexArray::Create();

		renderer2DData->VertexBuffer2D = RageV::VertexBuffer::Create(sizeof(VertexData) * renderer2DData->MaxVertices);
		RageV::BufferLayout sqbufferLayout = {
			{ "a_Position", ShaderDataType::Float3 },
			{ "a_Normal", ShaderDataType::Float3 },
			{ "a_Color", ShaderDataType::Float4 },
			{ "a_TexCord", RageV::ShaderDataType::Float2 },
			{ "a_TextureID", RageV::ShaderDataType::Float },
			{ "a_TilingFactor", RageV::ShaderDataType::Float }
		};
		renderer2DData->VertexBuffer2D->SetBufferLayout(sqbufferLayout);
		renderer2DData->VertexArray2D->AddVertexBuffer(renderer2DData->VertexBuffer2D);

		unsigned int* QuadIndicies = new unsigned int[renderer2DData->MaxIndicies];

		unsigned int offset = 0;

		for (int i = 0; i < renderer2DData->MaxIndicies; i += 6)
		{
			QuadIndicies[i + 0] = 0 + offset;
			QuadIndicies[i + 1] = 1 + offset;
			QuadIndicies[i + 2] = 2 + offset;
			QuadIndicies[i + 3] = 2 + offset;
			QuadIndicies[i + 4] = 3 + offset;
			QuadIndicies[i + 5] = 0 + offset;
			
			offset += 4;
		}

		renderer2DData->IndexBuffer2D = RageV::IndexBuffer::Create(QuadIndicies, renderer2DData->MaxIndicies);
		renderer2DData->VertexArray2D->SetIndexBuffer(renderer2DData->IndexBuffer2D);

		delete[] QuadIndicies;

		renderer2DData->WhiteTexture = Texture2D::Create(1, 1);
		unsigned int whiteData = 0xffffffff;
		renderer2DData->WhiteTexture->SetData(&whiteData, sizeof(unsigned int));
		renderer2DData->WhiteTexture->Bind(); //White texture will remain bound to slot 0

		//shader stuff
		renderer2DData->Renderer2DShaderManager.LoadShader("assets/shaders/quadshader.glsl");
		renderer2DData->QuadShader = renderer2DData->Renderer2DShaderManager.GetShader("quadshader");
		renderer2DData->QuadShader->Bind();

		std::vector<int> samplers(renderer2DData->MaxTextureSlots);
		for (unsigned int i = 0; i < renderer2DData->MaxTextureSlots; i++)
			samplers[i] = (int)i;

		renderer2DData->QuadShader->SetIntArray("u_Textures", samplers.data(), renderer2DData->MaxTextureSlots);

		for (unsigned int i = 0; i < Renderer2DData::MaxLights; i++)
		{
			renderer2DData->LightPosNames[i]   = "u_LightPos[" + std::to_string(i) + "]";
			renderer2DData->LightColorNames[i] = "u_LightColor[" + std::to_string(i) + "]";
		}

		renderer2DData->QuadVerticiesStorage.resize((size_t)renderer2DData->MaxQuads * 4);
		renderer2DData->QuadVerticiesBuffer = renderer2DData->QuadVerticiesStorage.data();
		renderer2DData->QuadVerticiesPtr = renderer2DData->QuadVerticiesBuffer;
	}

	void Renderer2D::Shutdown()
	{
		// This used to be `delete renderer2DData.get()`, which freed the block
		// the unique_ptr still owned and then double-freed it at exit.
		renderer2DData.reset();
	}

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		renderer2DData->DrawCalls = 0;
		Renderer2D::ResetScene();
		renderer2DData->QuadShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
	}

	void Renderer2D::BeginScene(const Cameranew& camera, const glm::mat4& transform, const LightData& lightData)
	{
		const glm::mat4 viewprojectionmatrix = camera.GetProjection() * glm::inverse(transform);
		renderer2DData->DrawCalls = 0;
		Renderer2D::ResetScene();

		// Was a hash lookup per uniform, every frame.
		const auto& shader = renderer2DData->QuadShader;
		shader->SetMat4("u_ViewProjection", viewprojectionmatrix);
		shader->SetFloat3("u_CamPos", glm::vec3(transform[3]));

		// The shader declares fixed-size arrays; feeding it more lights than
		// that would write past the end of the uniform array.
		const int lightCount = (int)std::min<size_t>(lightData.size(), Renderer2DData::MaxLights);

		for (int i = 0; i < lightCount; i++)
		{
			const glm::vec3& lPos = std::get<0>(lightData[i]);
			const glm::vec3& lColor = std::get<1>(lightData[i]);
			shader->SetFloat3(renderer2DData->LightPosNames[i], lPos);
			shader->SetFloat3(renderer2DData->LightColorNames[i], lColor);
		}

		shader->SetInt1("u_LightCount", lightCount);
	}

	void Renderer2D::ResetScene()
	{
		renderer2DData->QuadCount = 0;
		renderer2DData->IndiciesCount = 0;
		renderer2DData->QuadVerticiesPtr = renderer2DData->QuadVerticiesBuffer;
	}

	void Renderer2D::EndScene()
	{
		// An empty batch used to still cost a SetData, a draw call, and a bump
		// of the draw-call counter the editor displays.
		if (renderer2DData->QuadCount == 0)
			return;

		renderer2DData->WhiteTexture->Bind();
		renderer2DData->DrawCalls++;
		renderer2DData->VertexBuffer2D->SetData(renderer2DData->QuadVerticiesBuffer, renderer2DData->QuadCount * 4 * sizeof(VertexData));

		RenderCommand::DrawIndexed(renderer2DData->VertexArray2D, renderer2DData->IndiciesCount);
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color)
	{
		if (renderer2DData->MaxQuads <= renderer2DData->QuadCount)
		{
			EndScene();
			ResetScene();
		}

		glm::vec2 texcoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

		for (int i = 0; i < 4; i++)
		{
			renderer2DData->QuadVerticiesPtr->Position = transform * renderer2DData->QuadVerts[i];
			renderer2DData->QuadVerticiesPtr->Normal = glm::mat3(glm::transpose(glm::inverse(transform))) * glm::vec3(0.0f, 0.0f, -1.0f);
			renderer2DData->QuadVerticiesPtr->Color = color;
			renderer2DData->QuadVerticiesPtr->TexCoord = texcoords[i];
			renderer2DData->QuadVerticiesPtr->TextureID = 0.0f;
			renderer2DData->QuadVerticiesPtr->TilingFactor = 1.0f;
			renderer2DData->QuadVerticiesPtr++;
		}
	
		renderer2DData->QuadCount++;
		renderer2DData->IndiciesCount += 6;

	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingfactor)
	{
		if (renderer2DData->MaxQuads <= renderer2DData->QuadCount)
		{
			EndScene();
			ResetScene();
		}

		texture->Bind(renderer2DData->CurrentTextureSlotId);

		glm::vec2 texcoords[4] = {{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }};

		for (int i = 0; i < 4; i++)
		{
			renderer2DData->QuadVerticiesPtr->Position = transform * renderer2DData->QuadVerts[i];
			renderer2DData->QuadVerticiesPtr->Normal = glm::mat3(glm::transpose(glm::inverse(transform))) * glm::vec3(0.0f, 0.0f, -1.0f);
			renderer2DData->QuadVerticiesPtr->Color = glm::vec4(1.0f);
			renderer2DData->QuadVerticiesPtr->TexCoord = texcoords[i];
			renderer2DData->QuadVerticiesPtr->TextureID = (float)renderer2DData->CurrentTextureSlotId;
			renderer2DData->QuadVerticiesPtr->TilingFactor = tilingfactor;
			renderer2DData->QuadVerticiesPtr++;
		}
		
		renderer2DData->QuadCount++;
		renderer2DData->IndiciesCount += 6;

		if (renderer2DData->CurrentTextureSlotId != renderer2DData->MaxTextureSlots - 1)
			renderer2DData->CurrentTextureSlotId++;
		else
			renderer2DData->CurrentTextureSlotId = 1;
	}

	unsigned int Renderer2D::GetDrawCallCount()
	{
		return renderer2DData->DrawCalls;
	}

	unsigned int Renderer2D::GetVerticesCount()
	{
		return renderer2DData->QuadCount * 4;
	}

	unsigned int Renderer2D::GetIndiciesCount()
	{
		return renderer2DData->IndiciesCount;
	}

	unsigned int Renderer2D::GetQuadCount()
	{
		return renderer2DData->QuadCount;
	}


}