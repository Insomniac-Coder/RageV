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
}

void RageV::SceneHierarchyPanel::OnImGuiRender()
{
	ImGui::Begin("Scene Hierarchy");

	auto& view = m_SceneRef->m_Registry.view<TagComponent>();

	for (auto& item : view)
	{
		auto& tag = m_SceneRef->m_Registry.get<TagComponent>(item).Name;
		ImGuiTreeNodeFlags flags = ((m_Selected == Entity{ item, m_SceneRef.get() }) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;

		//returns wheter item was opened or not, imgui stuff
		bool isOpened = ImGui::TreeNodeEx((void*)(uint64_t)(unsigned int)item, flags, tag.c_str());
		if (ImGui::IsItemClicked())
		{
			m_Selected = Entity{ item, m_SceneRef.get() };
		}

		if (isOpened)
		{
			ImGui::TreePop();
		}
	}

	if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		m_Selected = {};

	ImGui::End();

	//Properties panel
	ImGui::Begin("Properties");
	if (m_Selected)
		ShowProperties(m_Selected);
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

void RageV::SceneHierarchyPanel::ShowProperties(Entity entity)
{
	if (m_Selected.HasComponent<TagComponent>())
	{
		TagComponent& tag = entity.GetComponent<TagComponent>();
		char buffer[256];
		memset(buffer, 0, sizeof(buffer));
		strcpy_s(buffer, tag.Name.c_str());
		if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
		{
			tag = std::string(buffer);
		}

	}
	if (m_Selected.HasComponent<TransformComponent>())
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
		if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), flags, "Transform"))
		{
			auto& transformComponent = entity.GetComponent<TransformComponent>();
			DrawVec3Control("Position", transformComponent.Position);
			glm::vec3 rotation = glm::degrees(transformComponent.Rotation);
			DrawVec3Control("Rotation", rotation);
			transformComponent.Rotation = glm::radians(rotation);
			DrawVec3Control("Scale", transformComponent.Scale, 1.0f);

			ImGui::TreePop();
		}
	}

	if (m_Selected.HasComponent<ColorComponent>())
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
		if (ImGui::TreeNodeEx((void*)typeid(ColorComponent).hash_code(), flags, "Color"))
		{
			glm::vec4& color = entity.GetComponent<ColorComponent>().Color;
			ImGui::ColorEdit4("Color", glm::value_ptr(color));

			ImGui::TreePop();
		}
	}

	if (m_Selected.HasComponent<CameraComponent>())
	{
		CameraComponent& camComponent = m_Selected.GetComponent<CameraComponent>();

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
		if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), flags, "Camera"))
		{
			const char* projectionTypes[] = { "Perspective", "Orthographic" };
			const char* currentProjectionType = projectionTypes[(int)camComponent.Camera.GetProjectionType()];

			ImGui::Checkbox("Primary", &camComponent.isPrimary);
			ImGui::Checkbox("Fixed Aspect Ratio", &camComponent.fixedAspectRatio);
			
			if (ImGui::BeginCombo("Projection Type", currentProjectionType))
			{
				for (int i = 0; i < 2; i++)
				{
					bool isSelected = currentProjectionType == projectionTypes[i];
					if (ImGui::Selectable(projectionTypes[i], isSelected))
					{
						currentProjectionType = projectionTypes[i];
						camComponent.Camera.SetProjectionType(SceneCamera::ProjectionType(i));
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			if (camComponent.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
			{
				float perspectiveFOV = camComponent.Camera.GetPerspectiveFOV();
				if (ImGui::DragFloat("Perspective FOV", &perspectiveFOV, 0.1f))
					camComponent.Camera.SetPerspectiveFOV(perspectiveFOV);

				float perspectiveNear = camComponent.Camera.GetPerspectiveNearClip();
				if (ImGui::DragFloat("Near Clip", &perspectiveNear, 0.1f))
					camComponent.Camera.SetPerspectiveNearClip(perspectiveNear);

				float perspectiveFar = camComponent.Camera.GetPerspectiveFarClip();
				if (ImGui::DragFloat("Far Clip", &perspectiveFar, 0.1f))
					camComponent.Camera.SetPerspectiveFarClip(perspectiveFar);
			}
			if (camComponent.Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
			{
				float orthSize = camComponent.Camera.GetOrthographicSize();
				if (ImGui::DragFloat("Orthographic Size", &orthSize, 0.1f))
					camComponent.Camera.SetOrthgraphicSize(orthSize);

				float orthNear = camComponent.Camera.GetOrthoNearClip();
				if (ImGui::DragFloat("Near Clip", &orthNear, 0.1f))
					camComponent.Camera.SetOrthoNearClip(orthNear);

				float orthFar = camComponent.Camera.GetOrthoFarClip();
				if (ImGui::DragFloat("Far Clip", &orthFar, 0.1f))
					camComponent.Camera.SetOrthoFarClip(orthFar);
			}

			ImGui::TreePop();
		}
	}
}
