#include <RageV.h>
#include "imgui.h"

class ExampleLayer : public RageV::Layer
{
public:
	ExampleLayer() : Layer("Example"), m_CameraController(1270.f/ 720.f, true), m_Color(1.0f) {

		float sqvertices [3 * 4] = {
			-0.5f, -0.5f, 0.0f,
			0.5f, -0.5f, 0.0f,
			0.5f, 0.5f, 0.0f,
			-0.5f, 0.5f, 0.0f
		};

		float sqvertices2 [5 * 4] = {
			-0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
			0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f, 1.0f,
			-0.5f, 0.5f, 0.0f, 0.0f, 1.0f
		};

		unsigned int sqindices[6] = { 0, 1, 2, 2, 3, 0 };

		//Flat color tiles
		m_SqVertexArray.reset(RageV::VertexArray::Create());
		std::shared_ptr<RageV::VertexBuffer> m_SqVertexBuffer;
		std::shared_ptr<RageV::IndexBuffer> m_SqIndexBuffer;

		m_SqVertexBuffer.reset(RageV::VertexBuffer::Create(sqvertices, sizeof(sqvertices)));
		RageV::BufferLayout sqbufferLayout = {
			{ "a_Position", RageV::ShaderDataType::Float3}
		};
		m_SqVertexBuffer->SetBufferLayout(sqbufferLayout);
		m_SqVertexArray->AddVertexBuffer(m_SqVertexBuffer);

		m_SqIndexBuffer.reset(RageV::IndexBuffer::Create(sqindices, 6));
		m_SqVertexArray->SetIndexBuffer(m_SqIndexBuffer);

		//Textured tile
		m_TextureSqVertexArray.reset(RageV::VertexArray::Create());
		std::shared_ptr<RageV::VertexBuffer> m_TextureSqVertexBuffer;
		std::shared_ptr<RageV::IndexBuffer> m_TextureSqIndexBuffer;

		m_TextureSqVertexBuffer.reset(RageV::VertexBuffer::Create(sqvertices2, sizeof(sqvertices2)));
		RageV::BufferLayout TextureSqBufferLayout = {
			{ "a_Position", RageV::ShaderDataType::Float3},
			{ "a_texCord", RageV::ShaderDataType::Float2}
		};

		m_TextureSqVertexBuffer->SetBufferLayout(TextureSqBufferLayout);
		m_TextureSqVertexArray->AddVertexBuffer(m_TextureSqVertexBuffer);

		m_TextureSqIndexBuffer.reset(RageV::IndexBuffer::Create(sqindices, 6));
		m_TextureSqVertexArray->SetIndexBuffer(m_TextureSqIndexBuffer);

		m_Texture = RageV::Texture2D::Create("assets/textures/instagram.png");
		m_Texture2 = RageV::Texture2D::Create("assets/textures/transparent.png");

		//shader stuff
		shaderManager.LoadShader("assets/shaders/simpleshader.glsl");
		shaderManager.LoadShader("assets/shaders/textureshader.glsl");

		auto shader = shaderManager.GetShader("textureshader");
		std::dynamic_pointer_cast<RageV::OpenGLShader>(shader)->Bind();
		std::dynamic_pointer_cast<RageV::OpenGLShader>(shader)->SetUniform("a_Tex", 0);
	}

	void  OnUpdate(RageV::Timestep ts) override
	{
		//RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
		m_CameraController.OnUpdate(ts);

		RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		RageV::RenderCommand::Clear();
		RageV::Renderer::BeginScene(m_CameraController.GetCamera());

		glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
		auto simpleshader = shaderManager.GetShader("simpleshader");
		auto textureshader = shaderManager.GetShader("textureshader");
		std::dynamic_pointer_cast<RageV::OpenGLShader>(simpleshader)->Bind();
		std::dynamic_pointer_cast<RageV::OpenGLShader>(simpleshader)->SetUniform("u_Color", m_Color);

		for (int x = 0; x < 20; x++)
		{
			for (int y = 0; y < 20; y++)
			{
				glm::vec3 pos((x * 0.11f) - 1.f, (y * 0.11f) - 1.f, 0.0f);
				glm::mat4 sqTransform = glm::translate(glm::mat4(1.0f), pos) * scale;
				RageV::Renderer::Submit(simpleshader, m_SqVertexArray, sqTransform);
			}
		}
		m_Texture->Bind();
		RageV::Renderer::Submit(textureshader, m_TextureSqVertexArray, glm::translate(glm::mat4(1.0f), glm::vec3(0.0)));
		m_Texture2->Bind();
		RageV::Renderer::Submit(textureshader, m_TextureSqVertexArray, glm::translate(glm::mat4(1.0f), glm::vec3(0.0)));

		RageV::Renderer::EndScene();
	}

	void OnImGuiRender() override
	{
		ImGui::Begin("Color Picker");
		ImGui::ColorEdit3("Color,", &m_Color[0]);
		ImGui::End();
	}

	void OnEvent(RageV::Event& e) override
	{
		m_CameraController.OnEvent(e);
		if (e.GetEventType() == RageV::EventType::KeyPressed)
		{
			RageV::KeyPressedEvent& ke = (RageV::KeyPressedEvent&)e;
			RV_INFO("{0} key pressed!", (char)ke.GetKeyCode());
		}
	}
private:
	std::shared_ptr<RageV::VertexArray> m_SqVertexArray;
	std::shared_ptr<RageV::VertexArray> m_TextureSqVertexArray;
	std::shared_ptr<RageV::Texture2D> m_Texture, m_Texture2;
	RageV::ShaderManager shaderManager;
	glm::vec3 m_Color;
	RageV::OrthographicCameraController m_CameraController;
};

class Sandbox : public RageV::Application {
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer());
	}
	~Sandbox()
	{

	}
};

RageV::Application* RageV::CreateApplication() {
	Sandbox* sandbox = new Sandbox();

	return sandbox;
}