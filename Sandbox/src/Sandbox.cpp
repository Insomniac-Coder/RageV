#include <RageV.h>
#include "imgui.h"

class ExampleLayer : public RageV::Layer
{
public:
	ExampleLayer() : Layer("Example"), camera(-1.6f, 1.6f, -0.9f, 0.9f), m_CameraPos(0.0f), m_Color(1.0f) {

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

		std::string vertexSrc = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;

			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;

			void main()
			{	
				gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
			}					
		)";

		std::string fragmentSrc = R"(
			#version 330 core
			layout(location = 0) out vec4 a_Color;

			uniform vec3 u_Color;

			void main()
			{	
				a_Color = vec4(u_Color, 1.0);
			}					
		)";

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
		m_Shader.reset(RageV::Shader::Create(vertexSrc, fragmentSrc));
		m_TextureShader.reset(RageV::Shader::Create("assets/shaders/textureshader.glsl"));


		std::dynamic_pointer_cast<RageV::OpenGLShader>(m_TextureShader)->Bind();
		std::dynamic_pointer_cast<RageV::OpenGLShader>(m_TextureShader)->SetUniform("a_Tex", 0);
	}

	void  OnUpdate(RageV::Timestep ts) override
	{
		//RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
		RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		RageV::RenderCommand::Clear();
		camera.SetRotation(m_CameraRot);
		camera.SetPosition(m_CameraPos);
		RageV::Renderer::BeginScene(camera);

		glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
		std::dynamic_pointer_cast<RageV::OpenGLShader>(m_Shader)->Bind();
		std::dynamic_pointer_cast<RageV::OpenGLShader>(m_Shader)->SetUniform("u_Color", m_Color);

		for (int x = 0; x < 20; x++)
		{
			for (int y = 0; y < 20; y++)
			{
				glm::vec3 pos((x * 0.11f) - 1.f, (y * 0.11f) - 1.f, 0.0f);
				glm::mat4 sqTransform = glm::translate(glm::mat4(1.0f), pos) * scale;
				RageV::Renderer::Submit(m_Shader, m_SqVertexArray, sqTransform);
			}
		}
		m_Texture->Bind();
		RageV::Renderer::Submit(m_TextureShader, m_TextureSqVertexArray, glm::translate(glm::mat4(1.0f), glm::vec3(0.0)));
		m_Texture2->Bind();
		RageV::Renderer::Submit(m_TextureShader, m_TextureSqVertexArray, glm::translate(glm::mat4(1.0f), glm::vec3(0.0)));

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
		ImGui::Begin("Color Picker");
		ImGui::ColorEdit3("Color,", &m_Color[0]);
		ImGui::End();
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
	std::shared_ptr<RageV::Shader> m_Shader;
	std::shared_ptr<RageV::Shader> m_TextureShader;
	std::shared_ptr<RageV::VertexArray> m_SqVertexArray;
	std::shared_ptr<RageV::VertexArray> m_TextureSqVertexArray;
	std::shared_ptr<RageV::Texture2D> m_Texture, m_Texture2;
	RageV::OrthographicCamera camera;
	glm::vec3 m_CameraPos;
	glm::vec3 m_Color;
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