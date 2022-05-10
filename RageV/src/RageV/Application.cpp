#include <rvpch.h>
#include "Application.h"
#include "RageV/Log.h"
#include "glad/glad.h"
#include "Input.h"

namespace RageV {
#define RV_BIND_FUNCTION(x) std::bind(&x, this, std::placeholders::_1)

	Application* Application::m_Instance = nullptr;

	Application::Application() {
		RV_CORE_ASSERT(!m_Instance, "Application instance already present present");
		m_Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(RV_BIND_FUNCTION(Application::OnEvent));

		m_ImGuiLayer = new ImGuiLayer();
 		PushOverlay(m_ImGuiLayer);

		glGenVertexArrays(1, &m_VertexArray);
		glBindVertexArray(m_VertexArray);

		float vertices[3 * 3] = {
			-0.5f, -0.5f, 0.0f,
			0.5f, -0.5f, 0.0f,
			0.0f, 0.5f, 0.0f
		};

		m_VertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 3 * sizeof(float), nullptr);


		unsigned int indices[3] = { 0, 1, 2 };
		m_IndexBuffer.reset(IndexBuffer::Create(indices, 3));


		std::string vertexSrc = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			out vec3 v_Pos;

			void main()
			{	
				v_Pos = a_Position;
				gl_Position = vec4(a_Position - 0.5, 1.0);
			}					
		)";

		std::string fragmentSrc = R"(
			#version 330 core
			layout(location = 0) out vec4 a_Color;
			in vec3 v_Pos;

			void main()
			{	
				a_Color = vec4(v_Pos * 0.5 + 0.5, 1.0);
			}					
		)";

		m_Shader.reset(new Shader(vertexSrc, fragmentSrc));
	}

	Application::~Application() {

	}

	void  Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::OnEvent(Event& e) {
		//RV_CORE_TRACE("{0}", e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(RV_BIND_FUNCTION(Application::OnWindowClose));

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);

			if (e.m_Handled)
				break;
		}

	}

	void Application::Run() {

		//WindowResizeEvent e(1280, 720);
		//RV_TRACE(e);
		while (m_Running) {

			m_Window->OnUpdate();

			m_Shader->Bind();
			glBindVertexArray(m_VertexArray);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr);

			for (Layer* layer : m_LayerStack)
				layer->OnUpdate();
			//auto [x, y] = Input::GetMousePosition();
			//RV_CORE_TRACE("{0}, {1}", x, y); 
			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
				layer->OnImGuiRender();

			m_ImGuiLayer->End();

		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;

		return true;
	}
}