#include <RageV.h>
#include "imgui.h"

class ExampleLayer : public RageV::Layer
{
public:
	ExampleLayer() : Layer("Example"), camera(-1.6f, 1.6f, -0.9f, 0.9f), m_CameraPos(0.0f), TrianglePos(0.0f) {
		float vertices[3 * 7] = {
			-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
			0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
			0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
		};

		float sqvertices[7 * 4] = {
			-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			-0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f
		};

		unsigned int indices[3] = { 0, 1, 2 };
		unsigned int sqindices[6] = { 0, 1, 2, 2, 3, 0 };

		m_VertexArray.reset(RageV::VertexArray::Create());
		std::shared_ptr<RageV::VertexBuffer> m_VertexBuffer;
		std::shared_ptr<RageV::IndexBuffer> m_IndexBuffer;

		m_VertexBuffer.reset(RageV::VertexBuffer::Create(vertices, sizeof(vertices)));
		RageV::BufferLayout bufferLayout = {
			{ "a_Position", RageV::ShaderDataType::Float3},
			{ "a_Color", RageV::ShaderDataType::Float4 }
		};
		m_VertexBuffer->SetBufferLayout(bufferLayout);
		m_VertexArray->AddVertexBuffer(m_VertexBuffer);

		m_IndexBuffer.reset(RageV::IndexBuffer::Create(indices, 3));
		m_VertexArray->SetIndexBuffer(m_IndexBuffer);

		std::string vertexSrc = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;
			out vec3 v_Pos;
			out vec4 v_Col;
			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;

			void main()
			{	
				v_Pos = a_Position;
				v_Col = a_Color;
				gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
			}					
		)";

		std::string fragmentSrc = R"(
			#version 330 core
			layout(location = 0) out vec4 a_Color;
			in vec3 v_Pos;
			in vec4 v_Col;

			void main()
			{	
				a_Color = v_Col;
			}					
		)";

		m_SqVertexArray.reset(RageV::VertexArray::Create());
		std::shared_ptr<RageV::VertexBuffer> m_SqVertexBuffer;
		std::shared_ptr<RageV::IndexBuffer> m_SqIndexBuffer;

		m_SqVertexBuffer.reset(RageV::VertexBuffer::Create(sqvertices, sizeof(sqvertices)));
		RageV::BufferLayout sqbufferLayout = {
			{ "a_Position", RageV::ShaderDataType::Float3},
			{ "a_Color", RageV::ShaderDataType::Float4 }
		};
		m_SqVertexBuffer->SetBufferLayout(sqbufferLayout);
		m_SqVertexArray->AddVertexBuffer(m_SqVertexBuffer);

		m_SqIndexBuffer.reset(RageV::IndexBuffer::Create(sqindices, 6));
		m_SqVertexArray->SetIndexBuffer(m_SqIndexBuffer);

		m_Shader.reset(RageV::Shader::Create(vertexSrc, fragmentSrc));

	}

	void  OnUpdate(RageV::Timestep ts) override
	{
		RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
		RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		RageV::RenderCommand::Clear();
		camera.SetRotation(m_CameraRot);
		camera.SetPosition(m_CameraPos);
		RageV::Renderer::BeginScene(camera);

		glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

		for (int x = 0; x < 20; x++)
		{
			for (int y = 0; y < 20; y++)
			{
				glm::vec3 pos((x * 0.11f) - 1.f, (y * 0.11f) - 1.f, 0.0f);
				glm::mat4 sqTransform = glm::translate(glm::mat4(1.0f), pos) * scale;
				RageV::Renderer::Submit(m_Shader, m_SqVertexArray, sqTransform);
			}
		}
		glm::mat4 triangleTransform = glm::translate(glm::mat4(1.0f), TrianglePos);
		RageV::Renderer::Submit(m_Shader, m_VertexArray, triangleTransform);
		RageV::Renderer::EndScene();

		if (RageV::Input::IsKeyPressed(RV_KEY_LEFT))
			m_CameraPos.x += 1.0f * ts;
		else if (RageV::Input::IsKeyPressed(RV_KEY_RIGHT))
			m_CameraPos.x -= 1.0f * ts;
		if (RageV::Input::IsKeyPressed(RV_KEY_UP))
			m_CameraPos.y -= 1.0f * ts;
		else if (RageV::Input::IsKeyPressed(RV_KEY_DOWN))
			m_CameraPos.y += 1.0f * ts;

		if (RageV::Input::IsKeyPressed(RV_KEY_A))
			m_CameraRot -= 90.0f * ts;
		else if (RageV::Input::IsKeyPressed(RV_KEY_D))
			m_CameraRot += 90.0f * ts;
	}

	void OnImGuiRender() override
	{
		
	}

	void OnEvent(RageV::Event& e) override
	{
		if (e.GetEventType() == RageV::EventType::KeyPressed)
		{
			RageV::KeyPressedEvent& ke = (RageV::KeyPressedEvent&)e;
			RV_INFO("{0} key pressed!", (char)ke.GetKeyCode());
		}
	}
private:
	std::shared_ptr<RageV::VertexArray> m_VertexArray;
	std::shared_ptr<RageV::Shader> m_Shader;
	std::shared_ptr<RageV::VertexArray> m_SqVertexArray;
	RageV::OrthographicCamera camera;
	glm::vec3 TrianglePos;
	glm::vec3 m_CameraPos;
	float m_CameraRot = 0.0f;
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