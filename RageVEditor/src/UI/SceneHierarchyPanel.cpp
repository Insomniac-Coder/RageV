#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "RageV/Managed/Interop.h"
#include "imgui_internal.h"
#include "RageV/Math/Math.h"
#include "EditorTheme.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Scene/ScriptRegistry.h"

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

	m_PendingDelete = {};
	m_PendingReparent = false;
	m_PendingCreateChild = false;

	// Roots only; children are drawn by the recursion.
	auto view = m_SceneRef->m_Registry.view<TagComponent, RelationshipComponent>();
	for (auto& item : view)
	{
		if (!view.get<RelationshipComponent>(item).Parent.IsValid())
			DrawEntityNode(Entity{ item, m_SceneRef.get() });
	}

	if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		m_Selected = {};

	// Dropping onto empty space unparents, which is otherwise impossible to
	// express by dragging.
	ImGui::Dummy(ImGui::GetContentRegionAvail());
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RAGEV_ENTITY"))
		{
			m_PendingReparentChild = Entity{ *(const entt::entity*)payload->Data, m_SceneRef.get() };
			m_PendingReparentParent = {};
			m_PendingReparent = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Create Entity"))
		{
			// The id is chosen here rather than inside the command so that redo
			// recreates the same entity rather than a new one.
			const UUID id;
			auto command = std::make_unique<CreateEntityCommand>(m_SceneRef, id, "Entity");
			if (m_Commands) m_Commands->Push(std::move(command));
			else            command->Execute();
			m_Selected = m_SceneRef->GetEntityByUUID(id);
		}

		ImGui::EndPopup();
	}

	// Structural changes are applied after the walk: mutating the hierarchy
	// mid-traversal invalidates the child lists the recursion is iterating.
	if (m_PendingCreateChild)
	{
		const UUID id;
		auto create = std::make_unique<CreateEntityCommand>(m_SceneRef, id, "Entity");
		if (m_Commands) m_Commands->Push(std::move(create));
		else            create->Execute();

		Entity child = m_SceneRef->GetEntityByUUID(id);
		auto parent = std::make_unique<ReparentCommand>(m_SceneRef, child, m_PendingCreateChildParent);
		if (m_Commands) m_Commands->Push(std::move(parent));
		else            parent->Execute();

		m_Selected = child;
	}

	if (m_PendingReparent)
	{
		auto command = std::make_unique<ReparentCommand>(m_SceneRef, m_PendingReparentChild,
														m_PendingReparentParent);
		if (!command->IsValid())
		{
			RV_WARN("Cannot parent an entity to itself or to its own descendant");
		}
		else if (m_Commands)
		{
			m_Commands->Push(std::move(command));
		}
		else
		{
			command->Execute();
		}
	}

	if (m_PendingDelete)
	{
		// Selection may be a descendant of what is being deleted, so it is
		// cleared whenever the deleted subtree contains it.
		if (m_Selected == m_PendingDelete || m_SceneRef->IsDescendantOf(m_Selected, m_PendingDelete))
			m_Selected = {};

		auto command = std::make_unique<DeleteEntityCommand>(m_SceneRef, m_PendingDelete);
		if (m_Commands) m_Commands->Push(std::move(command));
		else            command->Execute();
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

void RageV::SceneHierarchyPanel::DrawEntityNode(Entity entity)
{
	const auto& children = m_SceneRef->GetChildren(entity);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (m_Selected == entity)
		flags |= ImGuiTreeNodeFlags_Selected;
	// A leaf still needs to be a drop target, so it keeps the arrow slot but
	// cannot be expanded.
	if (children.empty())
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	const bool selected = (flags & ImGuiTreeNodeFlags_Selected) != 0;
	if (selected)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, EditorTheme::Color::AccentMuted);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorTheme::Color::AccentMuted);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorTheme::Color::Accent);
	}

	// Keyed by UUID rather than by handle: EnTT recycles handles, so a deleted
	// entity could hand its expansion state to an unrelated one.
	const uint64_t id = entity.GetUUID();
	const bool opened = ImGui::TreeNodeEx((void*)id, flags, "%s", entity.GetName().c_str());

	if (selected)
		ImGui::PopStyleColor(3);

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		m_Selected = entity;

	// --- drag and drop ------------------------------------------------------
	if (ImGui::BeginDragDropSource())
	{
		const entt::entity handle = entity;
		ImGui::SetDragDropPayload("RAGEV_ENTITY", &handle, sizeof(handle));
		ImGui::TextUnformatted(entity.GetName().c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RAGEV_ENTITY"))
		{
			m_PendingReparentChild = Entity{ *(const entt::entity*)payload->Data, m_SceneRef.get() };
			m_PendingReparentParent = entity;
			m_PendingReparent = true;
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Create Child"))
		{
			m_PendingCreateChildParent = entity;
			m_PendingCreateChild = true;
		}
		if (ImGui::MenuItem("Unparent", nullptr, false, (bool)m_SceneRef->GetParent(entity)))
		{
			m_PendingReparentChild = entity;
			m_PendingReparentParent = {};
			m_PendingReparent = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Entity"))
			m_PendingDelete = entity;

		ImGui::EndPopup();
	}

	if (opened && !children.empty())
	{
		// Copied rather than iterated in place. Every structural change in this
		// function is deferred, so nothing should mutate the list here -- but a
		// reference into a vector that some future menu item appends to is a
		// dangling read waiting to happen, and the copy is one allocation.
		const std::vector<UUID> snapshot = children;
		for (UUID childID : snapshot)
		{
			if (Entity child = m_SceneRef->GetEntityByUUID(childID))
				DrawEntityNode(child);
		}
		ImGui::TreePop();
	}
}

// -----------------------------------------------------------------------------
// Properties
//
// Generated from ComponentRegistry rather than hand-written per component. The
// previous version was ~300 lines of ImGui in which a component's widgets, its
// serialization and its "Add Component" entry were three separate places to
// remember -- which is how a light's intensity came to exist in the inspector
// and not on disk.
// -----------------------------------------------------------------------------
namespace
{
	using namespace RageV;

	// Label in a fixed-width left column, widget filling the rest, so every row
	// lines up regardless of which component drew it.
	void BeginField(const char* label, const char* tooltip)
	{
		ImGui::PushID(label);
		ImGui::Columns(2, nullptr, false);
		ImGui::SetColumnWidth(0, 140.0f);
		ImGui::TextUnformatted(label);

		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);

		ImGui::NextColumn();
		ImGui::PushItemWidth(-1.0f);
	}

	void EndField()
	{
		ImGui::PopItemWidth();
		ImGui::Columns(1);
		ImGui::PopID();
	}

	bool DrawVec3(const char* label, Vec3& values, float resetValue)
	{
		bool changed = false;
		ImGui::PushID(label);

		ImGui::Columns(2, nullptr, false);
		ImGui::SetColumnWidth(0, 140.0f);
		ImGui::TextUnformatted(label);
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

		const float lineHeight = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
		const ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

		const ImVec4 axisColors[3] = { EditorTheme::Color::AxisX,
									   EditorTheme::Color::AxisY,
									   EditorTheme::Color::AxisZ };
		const char* axisLabels[3] = { "X", "Y", "Z" };
		const char* dragIds[3] = { "##X", "##Y", "##Z" };
		float* components[3] = { &values.x, &values.y, &values.z };

		for (int axis = 0; axis < 3; axis++)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, axisColors[axis]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Color::AccentHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, axisColors[axis]);
			if (ImGui::Button(axisLabels[axis], buttonSize))
			{
				*components[axis] = resetValue;
				changed = true;
			}
			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			changed |= ImGui::DragFloat(dragIds[axis], components[axis], 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			if (axis < 2)
				ImGui::SameLine();
		}

		ImGui::PopStyleVar();
		ImGui::Columns(1);
		ImGui::PopID();
		return changed;
	}

	bool DrawField(const FieldDesc& field, void* component)
	{
		void* value = field.Access(component);
		const FieldHint& hint = field.Hint;
		bool changed = false;

		switch (field.Type)
		{
			case FieldType::Bool:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);
				changed = ImGui::Checkbox("##value", (bool*)value);
				EndField();
				break;
			}
			case FieldType::Int:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);
				changed = ImGui::DragInt("##value", (int*)value, hint.Speed,
										 (int)hint.Min, (int)hint.Max);
				EndField();
				break;
			}
			case FieldType::Enum:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);
				int& current = *(int*)value;
				const char* preview = (hint.EnumNames && current >= 0 && current < hint.EnumCount)
									? hint.EnumNames[current] : "?";

				if (ImGui::BeginCombo("##value", preview))
				{
					for (int i = 0; i < hint.EnumCount; i++)
					{
						const bool isSelected = current == i;
						if (ImGui::Selectable(hint.EnumNames[i], isSelected))
						{
							current = i;
							changed = true;
						}
						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				EndField();
				break;
			}
			case FieldType::Float:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);
				float* target = (float*)value;

				if (hint.Kind == FieldHint::Widget::Slider)
					changed = ImGui::SliderFloat("##value", target, hint.Min, hint.Max);
				else
					changed = ImGui::DragFloat("##value", target, hint.Speed, hint.Min, hint.Max);
				EndField();
				break;
			}
			case FieldType::Vec3:
			{
				if (hint.Kind == FieldHint::Widget::Color)
				{
					BeginField(field.DisplayName.c_str(), hint.Tooltip);
					changed = ImGui::ColorEdit3("##value", Math::ValuePtr(*(Vec3*)value));
					EndField();
				}
				else if (hint.Kind == FieldHint::Widget::Degrees)
				{
					// Stored in radians. Converting here rather than at the call
					// site means every angle field behaves the same way.
					Vec3 degrees = Math::Degrees(*(Vec3*)value);
					if (DrawVec3(field.Name, degrees, 0.0f))
					{
						*(Vec3*)value = Math::Radians(degrees);
						changed = true;
					}
				}
				else
				{
					const float reset = std::string(field.Name) == "Scale" ? 1.0f : 0.0f;
					changed = DrawVec3(field.Name, *(Vec3*)value, reset);
				}
				break;
			}
			case FieldType::Vec4:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);
				if (hint.Kind == FieldHint::Widget::Color)
					changed = ImGui::ColorEdit4("##value", Math::ValuePtr(*(Vec4*)value));
				else
					changed = ImGui::DragFloat4("##value", Math::ValuePtr(*(Vec4*)value), hint.Speed);
				EndField();
				break;
			}
			case FieldType::Asset:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);

				AssetHandle& handle = *(AssetHandle*)value;
				const std::string name = Assets::Manager::GetDisplayName(handle);

				// A button rather than a label: it is the drop target, and a
				// target you cannot see is one nobody finds.
				ImGui::Button(name.c_str(), ImVec2(-1.0f, 0.0f));

				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RAGEV_ASSET"))
					{
						const AssetHandle dropped = *(const AssetHandle*)payload->Data;
						const AssetMetadata& metadata = Assets::Registry::GetMetadata(dropped);

						// Refused rather than stored: a handle of the wrong
						// type resolves to nothing, and silently accepting it
						// would present as the field simply not working.
						if (hint.Accepts == AssetType::None || metadata.Type == hint.Accepts)
						{
							handle = dropped;
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}

				if (ImGui::IsItemHovered() && handle.IsValid())
					ImGui::SetTooltip("%s\n\nDrop an asset from the Content browser to change it.",
									  Assets::Registry::GetMetadata(handle).Path.c_str());

				EndField();
				break;
			}
			case FieldType::String:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);

				// Scripts are picked from what is registered, not typed. A
				// free-text field lets a typo produce an entity that does
				// nothing with no indication why.
				if (std::string(field.Name) == "Script")
				{
					std::string& current = *(std::string*)value;
					const std::vector<std::string> names = ScriptRegistry::GetNames();

					if (ImGui::BeginCombo("##value", current.empty() ? "(none)" : current.c_str()))
					{
						if (ImGui::Selectable("(none)", current.empty()))
						{
							current.clear();
							changed = true;
						}
						for (const std::string& name : names)
						{
							if (ImGui::Selectable(name.c_str(), name == current))
							{
								current = name;
								changed = true;
							}
						}
						ImGui::EndCombo();
					}

					EndField();
					break;
				}

				std::string& text = *(std::string*)value;
				char buffer[256];
				memset(buffer, 0, sizeof(buffer));
				strncpy_s(buffer, text.c_str(), sizeof(buffer) - 1);
				if (ImGui::InputText("##value", buffer, sizeof(buffer)))
				{
					text = buffer;
					changed = true;
				}
				EndField();
				break;
			}
		}

		return changed;
	}

	// Materials are the one thing a field list cannot describe: the component
	// holds a Ref, not a value. Phase 1 makes them assets and this goes away.
	void DrawMaterial(MeshComponent& mesh)
	{
		if (!mesh.Material)
		{
			ImGui::TextDisabled("Using the shared default material.");
			if (ImGui::Button("Create Material", ImVec2(-1.0f, 0.0f)) && Renderer::HasDevice())
				mesh.Material = std::make_shared<Material>(Renderer::GetDevice(), "Material");
			return;
		}

		auto& params = mesh.Material->GetParams();
		bool changed = false;

		ImGui::SeparatorText("Material");
		changed |= ImGui::ColorEdit4("Base Colour", Math::ValuePtr(params.BaseColor));
		changed |= ImGui::SliderFloat("Metallic", &params.Metallic, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat("Roughness", &params.Roughness, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat("Occlusion", &params.Occlusion, 0.0f, 1.0f);
		changed |= ImGui::ColorEdit3("Emissive", Math::ValuePtr(params.EmissiveColor));

		// Metals have no diffuse response, so a half-metallic surface is not
		// physical -- it is almost always an authoring mistake.
		if (params.Metallic > 0.05f && params.Metallic < 0.95f)
		{
			ImGui::TextColored(EditorTheme::Color::AccentHover, "Partially metallic");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Real materials are either metal or not. Intermediate "
								  "values only make sense where a texture blends between "
								  "the two across a surface.");
		}

		if (changed)
			mesh.Material->Invalidate();
	}
}

void RageV::SceneHierarchyPanel::ShowProperties(Entity entity)
{
	// --- add component -------------------------------------------------------
	if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
		ImGui::OpenPopup("AddComponent");

	if (ImGui::BeginPopup("AddComponent"))
	{
		bool any = false;
		for (const ComponentDesc& desc : ComponentRegistry::All())
		{
			if (!desc.AddableFromMenu || desc.TryGet(entity))
				continue;

			any = true;
			if (ImGui::MenuItem(desc.DisplayName))
			{
				auto command = std::make_unique<AddComponentCommand>(m_SceneRef, entity.GetUUID(),
																	 desc.Name);
				if (m_Commands) m_Commands->Push(std::move(command));
				else            command->Execute();
				ImGui::CloseCurrentPopup();
			}
		}

		if (!any)
			ImGui::TextDisabled("This entity has every component.");

		ImGui::EndPopup();
	}

	// --- components ----------------------------------------------------------
	const ComponentDesc* pendingRemove = nullptr;

	for (const ComponentDesc& desc : ComponentRegistry::All())
	{
		void* component = desc.TryGet(entity);
		if (!component)
			continue;

		ImGui::PushID(desc.Name);

		const ImVec2 available = ImGui::GetContentRegionAvail();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
		const float lineHeight = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
		ImGui::Separator();

		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
										 ImGuiTreeNodeFlags_SpanAvailWidth |
										 ImGuiTreeNodeFlags_FramePadding |
										 ImGuiTreeNodeFlags_AllowOverlap;
		const bool open = ImGui::TreeNodeEx("##header", flags, "%s", desc.DisplayName);
		ImGui::PopStyleVar();

		if (desc.Removable)
		{
			ImGui::SameLine(available.x - lineHeight * 0.5f);
			if (ImGui::Button("*", ImVec2{ lineHeight, lineHeight }))
				ImGui::OpenPopup("ComponentSettings");

			if (ImGui::BeginPopup("ComponentSettings"))
			{
				if (ImGui::MenuItem("Remove Component"))
					pendingRemove = &desc;
				ImGui::EndPopup();
			}
		}

		if (open)
		{
			bool changed = false;

			for (const FieldDesc& field : desc.Fields)
			{
				// Fields that do not apply in the current state are hidden
				// rather than disabled: a directional light has no cone, and a
				// greyed-out one implies it might.
				if (field.Hint.VisibleIf && !field.Hint.VisibleIf(component))
					continue;

				// Read before drawing: ImGui writes the new value during the
				// draw call, so this is the value from before the edit.
				const FieldValue before = ReadFieldValue(field, component);

				if (DrawField(field, component))
				{
					changed = true;
					if (!m_PendingEdit.Active)
					{
						m_PendingEdit.Active = true;
						m_PendingEdit.Entity = entity.GetUUID();
						m_PendingEdit.Component = desc.Name;
						m_PendingEdit.Field = field.Name;
						m_PendingEdit.Before = before;
					}
				}
			}

			if (std::string(desc.Name) == "MeshComponent")
				DrawMaterial(*static_cast<MeshComponent*>(component));

			if (std::string(desc.Name) == "ManagedScriptComponent")
				DrawManagedScript(*static_cast<ManagedScriptComponent*>(component));

			if (std::string(desc.Name) == "NativeScriptComponent")
			{
				auto* script = static_cast<NativeScriptComponent*>(component);
				if (!script->ScriptName.empty() && !ScriptRegistry::IsRegistered(script->ScriptName))
				{
					// A scene can outlive the script it names. Saying so beats
					// an entity that silently does nothing.
					ImGui::TextColored(EditorTheme::Color::AccentHover,
									   "'%s' is not registered", script->ScriptName.c_str());
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("The scene refers to a script this build does not "
										  "contain. The entity will run nothing.");
				}
				ImGui::TextDisabled("Runs on the fixed step, only while playing.");
			}

			// One place where derived state is refreshed, so a field added
			// later cannot forget to do it.
			if (changed && desc.OnChanged)
				desc.OnChanged(component);

			ImGui::Spacing();
			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	// Applied after the loop: removing a component invalidates the pointer the
	// iteration is holding.
	if (pendingRemove)
	{
		auto command = std::make_unique<RemoveComponentCommand>(m_SceneRef, entity.GetUUID(),
															   pendingRemove->Name);
		if (m_Commands) m_Commands->Push(std::move(command));
		else            command->Execute();
	}

	CommitPendingEdit();
}

// A continuous edit becomes one undo step. The pre-edit value is whatever the
// field held at the start of the frame in which it first changed -- ImGui
// applies the change during the draw call, so reading before drawing gives the
// value from before the drag began.
void RageV::SceneHierarchyPanel::CommitPendingEdit()
{
	if (!m_PendingEdit.Active || ImGui::IsAnyItemActive())
		return;

	m_PendingEdit.Active = false;

	if (!m_Commands || !m_SceneRef)
		return;

	const ComponentDesc* desc = ComponentRegistry::Find(m_PendingEdit.Component);
	Entity entity = m_SceneRef->GetEntityByUUID(m_PendingEdit.Entity);
	if (!desc || !entity)
		return;

	void* component = desc->TryGet(entity);
	if (!component)
		return;

	for (const FieldDesc& field : desc->Fields)
	{
		if (m_PendingEdit.Field != field.Name)
			continue;

		const FieldValue after = ReadFieldValue(field, component);
		if (after == m_PendingEdit.Before)
			return;   // dragged and returned to where it started

		// Already applied by the widget, so it is recorded rather than run.
		m_Commands->PushApplied(std::make_unique<FieldEditCommand>(
			m_SceneRef, m_PendingEdit.Entity, m_PendingEdit.Component,
			m_PendingEdit.Field, m_PendingEdit.Before, after));
		return;
	}
}

// The C# script component: which type, and its fields.
//
// The fields come from reflecting the *type*, not from anything stored -- so a
// script that gains a field shows it immediately, and one that loses a field
// stops showing it without the scene needing a migration. What is stored is
// only the values somebody actually changed, which is why a default edited in
// code reaches every entity that never overrode it.
void RageV::SceneHierarchyPanel::DrawManagedScript(ManagedScriptComponent& script)
{
	if (!Managed::Interop::IsReady())
	{
		ImGui::TextColored(EditorTheme::Color::AccentHover, "C# scripting is not running");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("The .NET runtime did not start, or no script assembly\n"
							  "has been built. File > Build Scripts.");
		}
		return;
	}

	// The types actually loaded, rather than a free-text field: a typo would
	// otherwise produce an entity that silently does nothing.
	std::string listing(4096, '\0');
	const int32_t needed = Managed::Interop::Managed().ListScriptTypes
		? Managed::Interop::Managed().ListScriptTypes(listing.data(), (int32_t)listing.size())
		: 0;

	std::vector<std::string> types;
	if (needed > 0)
	{
		std::istringstream stream(std::string(listing.c_str()));
		std::string name;
		while (std::getline(stream, name))
		{
			if (!name.empty())
				types.push_back(name);
		}
	}

	const std::string current = script.ScriptName.empty() ? "(none)" : script.ScriptName;

	if (ImGui::BeginCombo("Script", current.c_str()))
	{
		if (ImGui::Selectable("(none)", script.ScriptName.empty()))
		{
			script.ScriptName.clear();
			script.Fields.clear();
		}

		for (const std::string& name : types)
		{
			if (ImGui::Selectable(name.c_str(), name == script.ScriptName))
			{
				// Overrides belong to the script that had them. Keeping them
				// across a change of type would apply values from one script to
				// another script's identically named field, which is worse than
				// losing them.
				if (name != script.ScriptName)
					script.Fields.clear();
				script.ScriptName = name;
			}
		}

		ImGui::EndCombo();
	}

	if (script.ScriptName.empty())
	{
		ImGui::TextDisabled("Runs on the fixed step, only while playing.");
		return;
	}

	const std::vector<Managed::ScriptFieldDesc> fields =
		Managed::Interop::DescribeFields(script.ScriptName);

	if (types.empty() || std::find(types.begin(), types.end(), script.ScriptName) == types.end())
	{
		// A scene can outlive the script it names, or name one from an assembly
		// that has not been built yet. Saying so beats an entity that silently
		// does nothing.
		ImGui::TextColored(EditorTheme::Color::AccentHover,
						   "'%s' is not in any loaded assembly", script.ScriptName.c_str());
		return;
	}

	if (fields.empty())
	{
		ImGui::TextDisabled("No editable fields.");
		return;
	}

	ImGui::Separator();

	for (const Managed::ScriptFieldDesc& field : fields)
	{
		const std::string* stored = script.Find(field.Name);
		const std::string value = stored ? *stored : field.Default;

		ImGui::PushID(field.Name.c_str());

		switch (field.Type)
		{
			case Managed::ScriptFieldType::Bool:
			{
				bool flag = (value == "true" || value == "1");
				if (ImGui::Checkbox(field.Name.c_str(), &flag))
					script.Set(field.Name, flag ? "true" : "false");
				break;
			}
			case Managed::ScriptFieldType::Int:
			{
				int number = std::atoi(value.c_str());
				if (ImGui::DragInt(field.Name.c_str(), &number))
					script.Set(field.Name, std::to_string(number));
				break;
			}
			case Managed::ScriptFieldType::Float:
			{
				float number = (float)std::atof(value.c_str());
				if (ImGui::DragFloat(field.Name.c_str(), &number, 0.01f))
					script.Set(field.Name, FormatFloat(number));
				break;
			}
			case Managed::ScriptFieldType::Vector3:
			{
				float parts[3] = { 0.0f, 0.0f, 0.0f };
				std::istringstream stream(value);
				stream >> parts[0] >> parts[1] >> parts[2];

				if (ImGui::DragFloat3(field.Name.c_str(), parts, 0.01f))
				{
					script.Set(field.Name, FormatFloat(parts[0]) + " " +
											FormatFloat(parts[1]) + " " +
											FormatFloat(parts[2]));
				}
				break;
			}
			case Managed::ScriptFieldType::String:
			{
				char buffer[256]{};
				std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
				if (ImGui::InputText(field.Name.c_str(), buffer, sizeof(buffer)))
					script.Set(field.Name, buffer);
				break;
			}
			default:
				break;
		}

		// Only overridden fields get a reset, because only they have anything
		// to reset to -- and showing the control on every field would suggest
		// otherwise.
		if (stored)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Reset"))
				script.Clear(field.Name);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Back to the script's own default (%s)", field.Default.c_str());
		}

		ImGui::PopID();
	}
}

// Shortest round-trippable text for a float.
//
// std::to_string gives six decimals and turns 1.2 into "1.200000", which then
// appears in the scene file and in the inspector forever. This is what keeps a
// hand-edited scene readable.
std::string RageV::SceneHierarchyPanel::FormatFloat(float value)
{
	std::ostringstream out;
	out << std::setprecision(9) << std::defaultfloat << value;
	return out.str();
}
