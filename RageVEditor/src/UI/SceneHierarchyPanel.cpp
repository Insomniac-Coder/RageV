#include "SceneHierarchyPanel.h"
#include "imgui.h"

RageV::SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& sceneref)
{
	SetSceneRef(sceneref);
}

void RageV::SceneHierarchyPanel::SetSceneRef(const std::shared_ptr<Scene>& sceneref)
{
	m_SceneRef = sceneref;
}

void RageV::SceneHierarchyPanel::OnImGuiRender()
{
	ImGui::Begin("Scene Hierarchy");

	auto& view = m_SceneRef->m_Registry.view<TagComponent>();

	for (auto& item : view)
	{
		auto& tag = m_SceneRef->m_Registry.get<TagComponent>(item).Name;
		ImGuiTreeNodeFlags flags = ((m_Selected == Entity{item, m_SceneRef.get()}) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;

		//returns wheter item was opened or not, imgui stuff
		bool isOpened = ImGui::TreeNodeEx((void*)(uint64_t)(unsigned int)item, flags, tag.c_str());
		if (ImGui::IsItemClicked())
		{
			m_Selected = { item, m_SceneRef.get() };
		}

		if (isOpened)
		{
			ImGui::TreePop();
		}
	}

	ImGui::End();
}
