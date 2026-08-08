#include <rvpch.h>
#include "ImGuiLayer.h"

#include "imgui.h"
#include "ImGuizmo.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_glfw.h"
#include "Platform/Vulkan/VulkanImGui.h"
#include "RageV/Core/Application.h"
#include "RageV/Renderer/Renderer.h"

#include "GLFW/glfw3.h"

namespace RageV
{
	ImGuiLayer::ImGuiLayer()
		: Layer("ImGuiLayer")
	{
	}

	ImGuiLayer::~ImGuiLayer()
	{
	}

	void ImGuiLayer::OnAttach()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		m_Backend = Renderer::HasDevice() ? Renderer::GetDevice().GetBackend() : RHI::Backend::OpenGL;

		// Multi-viewport spawns extra OS windows, each needing its own
		// swapchain. The OpenGL backend gets that from GLFW for free; on Vulkan
		// it means a second presentation path this engine does not drive yet.
		if (m_Backend == RHI::Backend::OpenGL)
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/Fonts/RobotoFlex-Regular.ttf", 18.0f);

		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_WindowBg] = ImVec4{ 0.05f, 0.05f, 0.05f, 1.0f };
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		switch (m_Backend)
		{
			case RHI::Backend::Vulkan:
			{
				if (!Vk::InitImGuiVulkan(Renderer::GetDevice(), window))
					RV_CORE_ERROR("Failed to initialise the ImGui Vulkan backend");
				break;
			}
			case RHI::Backend::OpenGL:
			{
				ImGui_ImplGlfw_InitForOpenGL(window, true);
				ImGui_ImplOpenGL3_Init("#version 450");
				break;
			}
		}
	}

	void ImGuiLayer::OnDetach()
	{
		switch (m_Backend)
		{
			case RHI::Backend::Vulkan:
				Vk::ShutdownImGuiVulkan();
				break;
			case RHI::Backend::OpenGL:
				ImGui_ImplOpenGL3_Shutdown();
				ImGui_ImplGlfw_Shutdown();
				break;
		}
		ImGui::DestroyContext();
	}

	void ImGuiLayer::OnEvent(Event& e)
	{
		if (m_BlockEvents)
		{
			ImGuiIO& io = ImGui::GetIO();
			e.m_Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
			e.m_Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
		}
	}

	void ImGuiLayer::Begin()
	{
		if (m_Backend == RHI::Backend::Vulkan)
			Vk::ImGuiVulkanNewFrame();
		else
			ImGui_ImplOpenGL3_NewFrame();

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiLayer::End()
	{
		ImGuiIO& io = ImGui::GetIO();
		Application& app = Application::Get();
		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

		ImGui::Render();

		// The UI is the only thing drawn to the swapchain; the scene renders
		// into an offscreen target that the viewport panel samples.
		if (RHI::RHICommandList* cmd = Renderer::GetCommandList())
		{
			RHI::RenderPassBeginInfo pass;
			pass.Target = nullptr;
			pass.ClearColor = true;
			// The overlay does no depth testing, and declaring a depth
			// attachment would force ImGui's pipeline to match its format.
			pass.UseDepth = false;
			pass.ClearDepth = false;
			pass.Clear.Color[0] = 0.05f;
			pass.Clear.Color[1] = 0.05f;
			pass.Clear.Color[2] = 0.05f;
			pass.Clear.Color[3] = 1.0f;

			cmd->PushDebugGroup("ImGui");
			cmd->BeginRenderPass(pass);

			if (m_Backend == RHI::Backend::Vulkan)
				Vk::ImGuiVulkanRenderDrawData(*cmd);
			else
				ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			cmd->EndRenderPass();
			cmd->PopDebugGroup();
		}

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup);
		}
	}
}
