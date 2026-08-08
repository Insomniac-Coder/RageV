#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "glm/gtc/type_ptr.hpp"
#include "EditorTheme.h"

RageV::SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& sceneref)
{
	SetSceneRef(sceneref);
}

void RageV::SceneHierarchyPanel::SetSceneRef(const std::shared_ptr<Scene>& sceneref)
{
	m_SceneRef = sceneref;
	m_Selected = {};
}

void RageV::SceneHierarchyPanel::OnImGuiRender(bool* showHierarchy, bool* showProperties)
{
	if (showHierarchy && !*showHierarchy)
	{
		if (showProperties && *showProperties)
		{
			ImGui::Begin("Properties", showProperties);
			if (m_Selected)
				ShowProperties(m_Selected);
			else
				ImGui::TextDisabled("Nothing selected.");
			ImGui::End();
		}
		return;
	}

	ImGui::Begin("Scene Hierarchy", showHierarchy);

	auto view = m_SceneRef->m_Registry.view<TagComponent>();

	for (auto& item : view)
	{
		auto& tag = m_SceneRef->m_Registry.get<TagComponent>(item).Name;
		ImGuiTreeNodeFlags flags = ((m_Selected == Entity{ item, m_SceneRef.get() }) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

		//returns wheter item was opened or not, imgui stuff
		const bool selected = (flags & ImGuiTreeNodeFlags_Selected) != 0;
		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, EditorTheme::Color::AccentMuted);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorTheme::Color::AccentMuted);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorTheme::Color::Accent);
		}
		bool isOpened = ImGui::TreeNodeEx((void*)(uint64_t)(unsigned int)item, flags, tag.c_str());
		if (selected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemClicked())
		{
			m_Selected = Entity{ item, m_SceneRef.get() };
		}

		bool isDeleted = false;

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete Entity"))
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

	if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Create Entity"))
			m_SceneRef->CreateEntity();

		ImGui::EndPopup();
	}

	ImGui::End();

	//Properties panel
	if (showProperties && *showProperties)
	{
		ImGui::Begin("Properties", showProperties);
		if (m_Selected)
			ShowProperties(m_Selected);
		else
			ImGui::TextDisabled("Nothing selected.");
		ImGui::End();
	}
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

	float lineHeight = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
	ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

	ImGui::PushStyleColor(ImGuiCol_Button, RageV::EditorTheme::Color::AxisX);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RageV::EditorTheme::Color::AccentHover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, RageV::EditorTheme::Color::AxisX);
	if (ImGui::Button("X", buttonSize))
		values.x = resetValue;
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
	ImGui::PopItemWidth();
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, RageV::EditorTheme::Color::AxisY);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RageV::EditorTheme::Color::AxisY);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, RageV::EditorTheme::Color::AxisY);
	if (ImGui::Button("Y", buttonSize))
		values.y = resetValue;
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
	ImGui::PopItemWidth();
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, RageV::EditorTheme::Color::AxisZ);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RageV::EditorTheme::Color::AxisZ);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, RageV::EditorTheme::Color::AxisZ);
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
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;

			auto& component = entity.GetComponent<T>();

			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
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
			if (ImGui::MenuItem("Light Component"))
			{
				m_Selected.AddComponent<LightComponent>();
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Mesh Component"))
			{
				m_Selected.AddComponent<MeshComponent>();
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

	DrawComponent<LightComponent>("Light", entity, [](auto& component)
		{
			const char* lightTypes[] = { "Directional", "Point", "Spot"};
			const char* currentLightType = lightTypes[(int)component.Light.GetLightType()];

			ImGui::PushID("Light");
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 150.f);
			ImGui::Text("Light Color");
			ImGui::NextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
			ImGui::ColorEdit3("##Light", glm::value_ptr(component.Light.GetLightColor()));
			ImGui::Columns(1);
			ImGui::PopStyleVar();
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 150.f);
			ImGui::Text("Light Type");
			ImGui::NextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
			if (ImGui::BeginCombo("##Light Type", currentLightType))
			{
				for (int i = 0; i < 3; i++)
				{
					bool isSelected = currentLightType == lightTypes[i];
					if (ImGui::Selectable(lightTypes[i], isSelected))
					{
						currentLightType = lightTypes[i];
						component.Light.SetLightType(Light::LightType(i));
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Columns(1);
			ImGui::PopStyleVar();

			ImGui::SeparatorText("Intensity");
			float intensity = component.Light.GetIntensity();
			// Positional lights fall off with inverse square, so useful values
			// run into the hundreds; a 0-1 slider would be useless for them.
			const bool positional = component.Light.GetLightType() != Light::LightType::Directional;
			if (ImGui::DragFloat("Intensity", &intensity, positional ? 1.0f : 0.05f, 0.0f,
								 positional ? 1000.0f : 20.0f))
				component.Light.SetIntensity(std::max(intensity, 0.0f));

			if (positional)
			{
				float range = component.Light.GetRange();
				if (ImGui::DragFloat("Range", &range, 0.25f, 0.01f, 500.0f))
					component.Light.SetRange(std::max(range, 0.01f));
			}

			if (component.Light.GetLightType() == Light::LightType::Spot)
			{
				float inner = component.Light.GetInnerCone();
				float outer = component.Light.GetOuterCone();
				if (ImGui::SliderFloat("Inner Cone", &inner, 0.0f, 89.0f))
					component.Light.SetInnerCone(std::min(inner, component.Light.GetOuterCone()));
				if (ImGui::SliderFloat("Outer Cone", &outer, 0.0f, 89.0f))
					component.Light.SetOuterCone(std::max(outer, component.Light.GetInnerCone()));
			}

			ImGui::PopID();
			ImGui::Spacing();
		}
	);

	DrawComponent<MeshComponent>("Mesh", entity, [](auto& component)
		{
			const char* primitives[] = { "Cube", "Sphere", "Plane", "Cylinder", "Quad" };
			const char* current = PrimitiveTypeName(component.Primitive);

			ImGui::PushID("Mesh");
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 150.f);
			ImGui::Text("Primitive");
			ImGui::NextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });
			if (ImGui::BeginCombo("##Primitive", current))
			{
				for (int i = 0; i < IM_ARRAYSIZE(primitives); i++)
				{
					PrimitiveType type;
					if (!PrimitiveTypeFromName(primitives[i], type))
						continue;

					const bool selected = component.Primitive == type;
					if (ImGui::Selectable(primitives[i], selected))
						component.Primitive = type;
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Columns(1);
			ImGui::PopStyleVar();

			ImGui::PopID();

			if (!component.Material)
			{
				ImGui::TextDisabled("Using the shared default material.");
				if (ImGui::Button("Create Material", ImVec2(-1.0f, 0.0f)) && Renderer::HasDevice())
					component.Material = std::make_shared<Material>(Renderer::GetDevice(), "Material");
			}
			else
			{
				auto& params = component.Material->GetParams();
				bool changed = false;

				ImGui::SeparatorText("Material");
				changed |= ImGui::ColorEdit4("Base Colour", glm::value_ptr(params.BaseColor));
				changed |= ImGui::SliderFloat("Metallic", &params.Metallic, 0.0f, 1.0f);
				changed |= ImGui::SliderFloat("Roughness", &params.Roughness, 0.0f, 1.0f);
				changed |= ImGui::SliderFloat("Occlusion", &params.Occlusion, 0.0f, 1.0f);
				changed |= ImGui::ColorEdit3("Emissive", glm::value_ptr(params.EmissiveColor));

				// Metals have no diffuse response, so a half-metallic surface is
				// not physical -- it is almost always an authoring mistake.
				if (params.Metallic > 0.05f && params.Metallic < 0.95f)
				{
					ImGui::TextColored(EditorTheme::Color::AccentHover, "Partially metallic");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Real materials are either metal or not. Intermediate "
										  "values only make sense where a texture blends between "
										  "the two across a surface.");
				}

				if (changed)
					component.Material->Invalidate();
			}

			ImGui::Spacing();
		}
	);

	DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
		{
			const char* projectionTypes[] = { "Perspective", "Orthographic" };
			const char* currentProjectionType = projectionTypes[(int)component.Camera.GetProjectionType()];

			ImGui::Checkbox("Primary", &component.isPrimary);
			ImGui::Checkbox("Fixed Aspect Ratio", &component.fixedAspectRatio);

			// The projection type is the 2D/3D switch, so it gets a pair of
			// segmented buttons rather than being buried in the combo below.
			// Perspective is 3D, orthographic is 2D.
			ImGui::Spacing();
			{
				const bool is3D = component.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective;
				const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

				if (is3D) ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Color::Accent);
				if (ImGui::Button("3D", ImVec2(width, 0.0f)))
					component.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
				if (is3D) ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Perspective projection.");

				ImGui::SameLine();

				if (!is3D) ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Color::Accent);
				if (ImGui::Button("2D", ImVec2(width, 0.0f)))
					component.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
				if (!is3D) ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Orthographic projection.");
			}
			ImGui::Spacing();

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
