#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "glm/gtc/type_ptr.hpp"

RageV::SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& sceneref)
{
	SetSceneRef(sceneref);
}

void RageV::SceneHierarchyPanel::SetSceneRef(const std::shared_ptr<Scene>& sceneref)
{
	m_SceneRef = sceneref;
	m_Selected = {};
}

void RageV::SceneHierarchyPanel::OnImGuiRender()
{
	ImGui::Begin("Scene Hierarchy");

	auto& view = m_SceneRef->m_Registry.view<TagComponent>();

	for (auto& item : view)
	{
		auto& tag = m_SceneRef->m_Registry.get<TagComponent>(item).Name;
		ImGuiTreeNodeFlags flags = ((m_Selected == Entity{ item, m_SceneRef.get() }) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

		//returns wheter item was opened or not, imgui stuff
		bool isOpened = ImGui::TreeNodeEx((void*)(uint64_t)(unsigned int)item, flags, tag.c_str());
		if (ImGui::IsItemClicked())
		{
			m_Selected = Entity{ item, m_SceneRef.get() };
		}

		bool isDeleted = false;

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete GameOject"))
				isDeleted = true;

			ImGui::EndPopup();
		}

		if (isOpened)
		{
			ImGui::TreePop();
		}

		if (isDeleted)
		{
			Entity temp{ item, m_SceneRef.get()};
			m_SceneRef->DeleteEntity(temp);
			if(temp == m_Selected)
				m_Selected = {};
		}
	}

	if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		m_Selected = {};

	if (ImGui::BeginPopupContextWindow(0, 1, false))
	{
		if (ImGui::MenuItem("Create GameOject"))
			m_SceneRef->CreateEntity();

		ImGui::EndPopup();
	}

	ImGui::End();

	//Properties panel
	ImGui::Begin("Properties");
	if (m_Selected)
	{
		ShowProperties(m_Selected);
		ImGui::Separator();
	}
	ImGui::End();
}

static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
{
	ImGui::PushID(label.c_str());

	ImGui::Columns(2);
	ImGui::SetColumnWidth(0, columnWidth);
	ImGui::Text(label.c_str());
	ImGui::NextColumn();

	ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

	float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
	ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
	if (ImGui::Button("X", buttonSize))
		values.x = resetValue;
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
	ImGui::PopItemWidth();
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
	if (ImGui::Button("Y", buttonSize))
		values.y = resetValue;
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
	ImGui::PopItemWidth();
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
	if (ImGui::Button("Z", buttonSize))
		values.z = resetValue;
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
	ImGui::PopItemWidth();

	ImGui::PopStyleVar();

	ImGui::Columns(1);

	ImGui::PopID();
}

namespace RageV
{
	template<typename T, typename UIFunc>
	static void DrawComponent(const std::string& name, Entity& entity, UIFunc uiFunction, bool removeable = true)
	{
		if (entity.HasComponent<T>())
		{
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowItemOverlap;

			auto& component = entity.GetComponent<T>();

			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
			ImGui::Separator();
			bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), flags, name.c_str());
			ImGui::PopStyleVar();
			
			if (removeable)
			{
				ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
				if (ImGui::Button("*", ImVec2{ lineHeight, lineHeight }))
					ImGui::OpenPopup("Settings");
			}
			bool remove = false;
			if (ImGui::BeginPopup("Settings"))
			{
				if (ImGui::MenuItem("Remove Component"))
					remove = true;

				ImGui::EndPopup();
			}
			if (open)
			{
				uiFunction(component);
				ImGui::TreePop();
			}

			if (remove)
			{
				entity.RemoveComponent<T>();
			}
		}
	}
}

void RageV::SceneHierarchyPanel::ShowProperties(Entity entity)
{
	if (m_Selected.HasComponent<TagComponent>())
	{
		TagComponent& tag = entity.GetComponent<TagComponent>();
		char buffer[256];
		memset(buffer, 0, sizeof(buffer));
		strcpy_s(buffer, tag.Name.c_str());
		ImGui::PushID("Tag");
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, 100.f);
		ImGui::Text("Tag");
		ImGui::NextColumn();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
		if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
		{
			tag = std::string(buffer);
		}
		ImGui::Columns(1);
		ImGui::PopStyleVar();
		ImGui::PopID();

		ImGui::SameLine();
		ImGui::PushItemWidth(-1);
		if (ImGui::Button("Add Component"))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			if (ImGui::MenuItem("Camera"))
			{
				m_Selected.AddComponent<CameraComponent>();
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Color Component"))
			{
				m_Selected.AddComponent<ColorComponent>();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::PopItemWidth();
	}
	
	DrawComponent<TransformComponent>("Transform", entity, [](auto& component) 
		{
			DrawVec3Control("Position", component.Position);
			glm::vec3 rotation = glm::degrees(component.Rotation);
			DrawVec3Control("Rotation", rotation);
			component.Rotation = glm::radians(rotation);
			DrawVec3Control("Scale", component.Scale, 1.0f);
			ImGui::Spacing();
		},
		false
	);

	DrawComponent<ColorComponent>("Color", entity, [](auto& component)
		{
			ImGui::PushID("Color");
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 100.f);
			ImGui::Text("Color");
			ImGui::NextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
			ImGui::ColorEdit4("##Color", glm::value_ptr(component.Color));
			ImGui::SameLine();
			ImGui::Columns(1);
			ImGui::PopStyleVar();
			ImGui::PopID();
			ImGui::Spacing();
		}
	);

	DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
		{
			const char* projectionTypes[] = { "Perspective", "Orthographic" };
			const char* currentProjectionType = projectionTypes[(int)component.Camera.GetProjectionType()];

			ImGui::Checkbox("Primary", &component.isPrimary);
			ImGui::Checkbox("Fixed Aspect Ratio", &component.fixedAspectRatio);

			ImGui::PushID("ProjectionType");
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 150.f);
			ImGui::Text("Projection Type");
			ImGui::NextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
			if (ImGui::BeginCombo("##Projection Type", currentProjectionType))
			{
				for (int i = 0; i < 2; i++)
				{
					bool isSelected = currentProjectionType == projectionTypes[i];
					if (ImGui::Selectable(projectionTypes[i], isSelected))
					{
						currentProjectionType = projectionTypes[i];
						component.Camera.SetProjectionType(SceneCamera::ProjectionType(i));
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Columns(1);
			ImGui::PopStyleVar();
			ImGui::PopID();

			if (component.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
			{
				float perspectiveFOV = component.Camera.GetPerspectiveFOV();

				ImGui::PushID("perspectiveFOV");
				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 150.f);
				ImGui::Text("Perspective FOV");
				ImGui::NextColumn();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
				if (ImGui::DragFloat("", &perspectiveFOV, 0.1f))
					component.Camera.SetPerspectiveFOV(perspectiveFOV);
				ImGui::Columns(1);
				ImGui::PopStyleVar();
				ImGui::PopID();


				float perspectiveNear = component.Camera.GetPerspectiveNearClip();

				ImGui::PushID("perspectiveNearClip");
				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 150.f);
				ImGui::Text("Near Clip");
				ImGui::NextColumn();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
				if (ImGui::DragFloat("", &perspectiveNear, 0.1f))
					component.Camera.SetPerspectiveNearClip(perspectiveNear);
				ImGui::Columns(1);
				ImGui::PopStyleVar();
				ImGui::PopID();

				float perspectiveFar = component.Camera.GetPerspectiveFarClip();

				ImGui::PushID("perspectiveFarClip");
				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 150.f);
				ImGui::Text("Far Clip");
				ImGui::NextColumn();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
				if (ImGui::DragFloat("", &perspectiveFar, 0.1f))
					component.Camera.SetPerspectiveFarClip(perspectiveFar);
				ImGui::Columns(1);
				ImGui::PopStyleVar();
				ImGui::PopID();
			}
			if (component.Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
			{
				float orthSize = component.Camera.GetOrthographicSize();

				ImGui::PushID("orthographicSize");
				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 150.f);
				ImGui::Text("Orthographic Size");
				ImGui::NextColumn();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
				if (ImGui::DragFloat("", &orthSize, 0.1f))
					component.Camera.SetOrthgraphicSize(orthSize);
				ImGui::Columns(1);
				ImGui::PopStyleVar();
				ImGui::PopID();

				float orthNear = component.Camera.GetOrthoNearClip();

				ImGui::PushID("orthographicNearClip");
				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 150.f);
				ImGui::Text("Near Clip");
				ImGui::NextColumn();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
				if (ImGui::DragFloat("", &orthNear, 0.1f))
					component.Camera.SetOrthoNearClip(orthNear);
				ImGui::Columns(1);
				ImGui::PopStyleVar();
				ImGui::PopID();

				float orthFar = component.Camera.GetOrthoFarClip();

				ImGui::PushID("orthographicFarClip");
				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 150.f);
				ImGui::Text("Far Clip");
				ImGui::NextColumn();
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
				if (ImGui::DragFloat("", &orthFar, 0.1f))
					component.Camera.SetOrthoFarClip(orthFar);
				ImGui::Columns(1);
				ImGui::PopStyleVar();
				ImGui::PopID();
			}
			ImGui::Spacing();
		}
	);

}
