#include <rvpch.h>
#include "Renderer2D.h"
#include "Buffer.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "RenderCommand.h"

namespace RageV
{

	struct Renderer2DData {
		std::shared_ptr<VertexArray> Renderer2DVertexArray;
		ShaderManager Renderer2DShaderManager;
		std::shared_ptr<Texture2D> WhiteTexture;
	};

	static std::unique_ptr<Renderer2DData> renderer2DData;

	void Renderer2D::Init()
	{
		renderer2DData = std::make_unique<Renderer2DData>();

		float sqvertices[5 * 4] = {
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
		0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f, 1.0f,
		-0.5f, 0.5f, 0.0f, 0.0f, 1.0f
		};

		unsigned int sqindices[6] = { 0, 1, 2, 2, 3, 0 };

		//Flat color tiles
		renderer2DData->Renderer2DVertexArray.reset(RageV::VertexArray::Create());
		std::shared_ptr<RageV::VertexBuffer> m_SqVertexBuffer;
		std::shared_ptr<RageV::IndexBuffer> m_SqIndexBuffer;

		m_SqVertexBuffer.reset(RageV::VertexBuffer::Create(sqvertices, sizeof(sqvertices)));
		RageV::BufferLayout sqbufferLayout = {
			{ "a_Position", ShaderDataType::Float3},
			{ "a_TexCord", RageV::ShaderDataType::Float2}
		};
		m_SqVertexBuffer->SetBufferLayout(sqbufferLayout);
		renderer2DData->Renderer2DVertexArray->AddVertexBuffer(m_SqVertexBuffer);

		m_SqIndexBuffer.reset(RageV::IndexBuffer::Create(sqindices, 6));
		renderer2DData->Renderer2DVertexArray->SetIndexBuffer(m_SqIndexBuffer);

		renderer2DData->WhiteTexture = Texture2D::Create(1, 1);
		unsigned int whiteData = 0xffffffff;
		renderer2DData->WhiteTexture->SetData(&whiteData, sizeof(unsigned int));

		//shader stuff
		renderer2DData->Renderer2DShaderManager.LoadShader("assets/shaders/quadshader.glsl");
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->Bind();
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetInt1("a_Tex", 0);
	}

	void Renderer2D::Shutdown()
	{
		delete renderer2DData.get();
	}

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
	}

	void Renderer2D::EndScene()
	{
	}

	void Renderer2D::DrawQuad(Transform2D& transform, glm::vec4& color)
	{
		renderer2DData->WhiteTexture->Bind();
		glm::mat4 sqTransform = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(transform.scale, 1.0f)), transform.position);
		if (transform.rotation != 0.0f)
		{
			sqTransform *= glm::rotate(glm::mat4(1.0f), glm::radians(transform.rotation), glm::vec3(0.0f, 0.0f, 1.0f ));
		}
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->Bind();
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetFloat1("u_TilingFactor", 1.0f);
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetFloat4("u_Color", color);
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetMat4("u_Transform", sqTransform);
		renderer2DData->Renderer2DVertexArray->Bind();
		RenderCommand::DrawIndexed(renderer2DData->Renderer2DVertexArray);

	}

	void Renderer2D::DrawQuad(Transform2D& transform, std::shared_ptr<Texture2D>& texture, float tilingfactor)
	{
		texture->Bind();
		glm::mat4 sqTransform = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(transform.scale, 1.0f)), transform.position);
		if (transform.rotation != 0.0f)
		{
			sqTransform *= glm::rotate(glm::mat4(1.0f), glm::radians(transform.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		}
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->Bind();
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetFloat1("u_TilingFactor", tilingfactor);
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetFloat4("u_Color", glm::vec4(1.0f));
		renderer2DData->Renderer2DShaderManager.GetShader("quadshader")->SetMat4("u_Transform", sqTransform);
		renderer2DData->Renderer2DVertexArray->Bind();
		RenderCommand::DrawIndexed(renderer2DData->Renderer2DVertexArray);
	}


}