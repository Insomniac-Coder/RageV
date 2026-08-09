#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include "RageV/Project/Project.h"
#include <cctype>
#include <fstream>
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
				DrawNativeScript(*static_cast<NativeScriptComponent*>(component));

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

	// The language row queued a conversion. Applied here for the same reason
	// removal is: adding or removing a component while the component list is
	// being walked invalidates the iteration.
	//
	// The script name and its field overrides are deliberately *not* carried
	// across. A C++ script and a C# script are different scripts even when they
	// share a name, and moving one's tuning values onto the other would be
	// applying numbers to fields that only coincidentally match.
	if (m_PendingScriptSwap != PendingScriptSwap::None)
	{
		const bool toManaged = (m_PendingScriptSwap == PendingScriptSwap::ToCSharp);
		m_PendingScriptSwap = PendingScriptSwap::None;

		if (toManaged && entity.HasComponent<NativeScriptComponent>())
		{
			entity.RemoveComponent<NativeScriptComponent>();
			entity.AddComponent<ManagedScriptComponent>();
		}
		else if (!toManaged && entity.HasComponent<ManagedScriptComponent>())
		{
			entity.RemoveComponent<ManagedScriptComponent>();
			entity.AddComponent<NativeScriptComponent>();
		}
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
	if (DrawScriptNameRow(script.ScriptName))
		script.Fields.Values.clear();

	DrawScriptLanguageRow(true);

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

	if (DrawScriptPicker(types, script.ScriptName, true))
		script.Fields.Values.clear();

	// Creating one selects it, so the next step is Build Scripts rather than
	// hunting for the new name in a dropdown.
	std::string created;
	if (DrawNewScriptPopup(true, created))
	{
		script.ScriptName = created;
		script.Fields.Values.clear();
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

	// The same rows the C++ component draws. Two languages describe their
	// fields differently -- C++ at registration, C# by reflection -- and
	// converge here, so a field cannot behave differently depending on which
	// language declared it.
	for (const Managed::ScriptFieldDesc& field : fields)
		DrawScriptField(field.Name, (int)field.Type, field.Default, script.Fields);
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

// The script's name, typed rather than picked.
//
// Redundant with the dropdown for a script that already exists -- and not
// redundant at all for one that does not. A C++ script only joins the dropdown
// once the engine has been rebuilt, so without a text field there is no way to
// point an entity at a script you have just written.
//
// The name is stored on every keystroke, so the box keeps what you type, but
// overrides are dropped only once editing finishes and the name really did
// change: clearing per keystroke would delete them on the way to retyping the
// same name.
bool RageV::SceneHierarchyPanel::DrawScriptNameRow(std::string& scriptName)
{
	BeginField("Name", "The class name written into the scene file.\n"
					   "Type it to name a script that is not built yet -- a C++\n"
					   "script only joins the dropdown after an engine rebuild.");

	// Zero-initialised, and copy() is given one byte less than the buffer, so
	// the terminator survives a name longer than the box.
	char buffer[128] = {};
	scriptName.copy(buffer, sizeof(buffer) - 1);

	if (ImGui::InputText("##scriptname", buffer, sizeof(buffer)))
		scriptName = buffer;

	if (ImGui::IsItemActivated())
		m_ScriptNameBeforeEdit = scriptName;

	const bool committed = ImGui::IsItemDeactivatedAfterEdit()
						&& scriptName != m_ScriptNameBeforeEdit;

	EndField();
	return committed;
}

// The language row, shared by both script components.
//
// Changing it converts the entity from one script component to the other. That
// cannot be done here -- the caller is iterating the component list and adding
// or removing one invalidates it -- so the choice is queued and applied after
// the loop, exactly as component removal already is.
void RageV::SceneHierarchyPanel::DrawScriptLanguageRow(bool managed)
{
	BeginField("Language",
			   "C++ scripts are compiled into the engine and need a rebuild.\n"
			   "C# scripts live in the project and rebuild from File > Build Scripts.");

	const char* const kLanguages[] = { "C++", "C#" };
	int current = managed ? 1 : 0;

	if (ImGui::Combo("##language", &current, kLanguages, 2))
	{
		if ((current == 1) != managed)
			m_PendingScriptSwap = (current == 1) ? PendingScriptSwap::ToCSharp
												 : PendingScriptSwap::ToCpp;
	}

	EndField();
}

// The script row: everything available in this language, and a way to make one
// more.
//
// "New Script..." is the last entry in the dropdown rather than a button beside
// it. Choosing a script and making a new one are the same decision -- "which
// script runs here" -- and splitting them across two controls made the row read
// as two unrelated things.
//
// Returns true when the selection changed.
bool RageV::SceneHierarchyPanel::DrawScriptPicker(const std::vector<std::string>& available,
												  std::string& scriptName, bool managed)
{
	bool changed = false;

	BeginField("Script", "The name written into the scene file. Renaming a script\n"
						 "breaks every scene that used it.");

	const std::string current = scriptName.empty() ? "(none)" : scriptName;

	if (ImGui::BeginCombo("##script", current.c_str()))
	{
		if (ImGui::Selectable("(none)", scriptName.empty()))
		{
			scriptName.clear();
			changed = true;
		}

		for (const std::string& name : available)
		{
			if (ImGui::Selectable(name.c_str(), name == scriptName) && name != scriptName)
			{
				scriptName = name;
				changed = true;
			}
		}

		ImGui::Separator();

		// The popup cannot be opened from inside the combo -- the combo closes
		// and takes the popup with it. Flagged here, opened after EndCombo.
		if (ImGui::Selectable("New Script..."))
			m_OpenNewScript = true;

		ImGui::EndCombo();
	}

	EndField();

	if (m_OpenNewScript)
	{
		m_OpenNewScript = false;
		m_NewScriptName[0] = '\0';
		ImGui::OpenPopup("New Script");
	}

	(void)managed;
	return changed;
}

// "New Script..." -- writes a working template, never a blank file.
//
// Opened from the last entry of the script dropdown rather than a button beside
// it: choosing a script and making a new one are the same decision, and
// splitting them across two controls made the row read as two unrelated things.
//
// Returns the new script's type name once the file is written, so the caller can
// select it immediately. Creating a script and then having to find it in a
// dropdown is a step nobody wants.
bool RageV::SceneHierarchyPanel::DrawNewScriptPopup(bool managed, std::string& chosenName)
{
	const char* const kPopup = "New Script";
	bool created = false;

	if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return false;

	if (!managed)
	{
		const std::filesystem::path scripts = EngineScriptsDir();

		if (scripts.empty())
		{
			// A packaged editor has no engine source to add a file to. That is
			// the honest reason C++ scripting is not a per-project feature, and
			// saying it beats writing a file nothing builds.
			ImGui::TextWrapped("C++ scripts are compiled into the engine, and this build has no "
							   "engine source beside it.");
			ImGui::Spacing();
			ImGui::TextWrapped("Add a class deriving from ScriptableEntity to the engine or game "
							   "target and register it:");
			ImGui::Spacing();
			ImGui::TextDisabled("RV_REGISTER_SCRIPT(Spinner).Field<&Spinner::Speed>(\"Speed\");");
			ImGui::Spacing();
			ImGui::TextWrapped("Switch Language to C# for a script this project can build itself.");

			ImGui::Separator();
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();

			ImGui::EndPopup();
			return false;
		}

		ImGui::Text("Class name");
		ImGui::SetNextItemWidth(280.0f);
		ImGui::InputText("##cppname", m_NewScriptName, sizeof(m_NewScriptName));

		const std::string name(m_NewScriptName);
		const bool valid = IsIdentifier(name);

		std::error_code ec;
		const std::filesystem::path file = scripts / (name + ".cpp");
		const bool exists = valid && std::filesystem::exists(file, ec);

		if (!name.empty() && !valid)
			ImGui::TextColored(EditorTheme::Color::AccentHover, "Not a valid C++ class name");
		else if (exists)
			ImGui::TextColored(EditorTheme::Color::AccentHover, "%s.cpp already exists", name.c_str());
		else if (valid)
			ImGui::TextDisabled("%s", file.string().c_str());
		else
			ImGui::TextDisabled(" ");

		ImGui::Spacing();
		ImGui::TextWrapped("A C++ script is compiled into the engine, so it appears in the dropdown "
						   "after a rebuild -- not immediately.");

		ImGui::Separator();

		const bool ok = valid && !exists;
		ImGui::BeginDisabled(!ok);
		if (ImGui::Button("Create") && ok)
		{
			if (WriteNewNativeScript(file, name))
				RV_INFO("Created {0} -- rebuild the engine to use it", file.string());
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
		return false;
	}

	if (!Project::GetActive())
	{
		ImGui::TextWrapped("Open a project first. A script belongs to one.");
		ImGui::Separator();
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return false;
	}

	ImGui::Text("Class name");
	ImGui::SetNextItemWidth(280.0f);
	const bool submitted = ImGui::InputText("##csname", m_NewScriptName, sizeof(m_NewScriptName),
											ImGuiInputTextFlags_EnterReturnsTrue);

	const std::string name(m_NewScriptName);
	const bool valid = IsIdentifier(name);

	std::error_code ec;
	const std::filesystem::path file = Project::Root() / "Scripts" / (name + ".cs");
	const bool exists = valid && std::filesystem::exists(file, ec);

	if (!name.empty() && !valid)
		ImGui::TextColored(EditorTheme::Color::AccentHover, "Not a valid C# class name");
	else if (exists)
		ImGui::TextColored(EditorTheme::Color::AccentHover, "Scripts/%s.cs already exists", name.c_str());
	else if (valid)
		ImGui::TextDisabled("Scripts/%s.cs", name.c_str());
	else
		ImGui::TextDisabled(" ");

	ImGui::Spacing();
	ImGui::TextWrapped("File > Build Scripts compiles it.");

	ImGui::Separator();

	const bool ok = valid && !exists;
	ImGui::BeginDisabled(!ok);
	if ((ImGui::Button("Create") || (submitted && ok)) && ok)
	{
		if (WriteNewScript(file, name))
		{
			chosenName = name;
			created = true;
			RV_INFO("Created Scripts/{0}.cs -- File > Build Scripts to compile it", name);
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
	return created;
}

// The file a new C# script starts as.
//
// Not empty. The same reasoning as the starter scene and the generated project:
// the first five minutes should not be spent working out what a script has to
// look like, and a template that already overrides OnUpdate puts the fixed-step
// contract in the one place somebody is certain to read.
bool RageV::SceneHierarchyPanel::WriteNewScript(const std::filesystem::path& file,
											    const std::string& name)
{
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);

	std::ofstream out(file);
	if (!out)
	{
		RV_ERROR("Could not write {0}", file.string());
		return false;
	}

	out << "using RageV;\n\n";
	out << "public class " << name << " : Script\n";
	out << "{\n";
	out << "\t// Public or private, either shows up in the inspector. Only what you\n";
	out << "\t// change there is stored, so editing this default reaches every entity\n";
	out << "\t// that never overrode it.\n";
	out << "\tprivate float m_Speed = 1.0f;\n\n";
	out << "\tpublic override void OnCreate()\n";
	out << "\t{\n";
	out << "\t\tLog.Info($\"{Entity.Name} is ready\");\n";
	out << "\t}\n\n";
	out << "\t// Once per fixed simulation step, not once per frame. A frame may run\n";
	out << "\t// zero steps, one, or several -- so multiply rates by deltaTime and the\n";
	out << "\t// behaviour stays the same at any simulation frequency.\n";
	out << "\tpublic override void OnUpdate(float deltaTime)\n";
	out << "\t{\n";
	out << "\t\tTranslate(new Vector3(0.0f, m_Speed * deltaTime, 0.0f));\n";
	out << "\t}\n";
	out << "}\n";

	return true;
}

// The C++ script component.
void RageV::SceneHierarchyPanel::DrawNativeScript(NativeScriptComponent& script)
{
	// Overrides belong to the script that had them. Carrying them across a
	// change of script would apply one script's values to another's identically
	// named field, which is worse than losing them. Both ways of changing the
	// script -- typing the name and picking it -- drop them.
	if (DrawScriptNameRow(script.ScriptName))
		script.Fields.Values.clear();

	DrawScriptLanguageRow(false);

	if (DrawScriptPicker(ScriptRegistry::GetNames(), script.ScriptName, false))
		script.Fields.Values.clear();

	std::string created;
	DrawNewScriptPopup(false, created);

	if (script.ScriptName.empty())
	{
		ImGui::TextDisabled("Runs on the fixed step, only while playing.");
		return;
	}

	if (!ScriptRegistry::IsRegistered(script.ScriptName))
	{
		// A scene can outlive the script it names. Saying so beats an entity
		// that silently does nothing.
		ImGui::TextColored(EditorTheme::Color::AccentHover,
						   "'%s' is not registered", script.ScriptName.c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("The scene refers to a script this build does not contain.\n"
							  "The entity will run nothing.");
		}
		return;
	}

	const std::vector<ScriptField>& fields = ScriptRegistry::FieldsOf(script.ScriptName);
	if (fields.empty())
	{
		// How to add one belongs in a tooltip, not in the panel. It is the same
		// three lines under every fieldless script, and a wall of text that
		// never changes stops being read after the second time.
		ImGui::TextDisabled("No editable fields.");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Declare them where the script is registered:\n"
							  "RV_REGISTER_SCRIPT(%s).Field<&%s::Member>(\"Name\");",
							  script.ScriptName.c_str(), script.ScriptName.c_str());
		}
		return;
	}

	ImGui::Separator();

	for (const ScriptField& field : fields)
		DrawScriptField(field.Name, ScriptKindToWidget(field.Kind), field.Default, script.Fields);
}

// One field row, whichever language declared it.
//
// The two languages describe their fields differently -- C++ at registration,
// C# by reflection -- and converge here, so the widgets, the reset button and
// the stored text are the same in both. A field that behaves differently
// depending on the language it came from would be a bug nobody would think to
// look for.
void RageV::SceneHierarchyPanel::DrawScriptField(const std::string& name, int kind,
												 const std::string& defaultValue,
												 ScriptFieldOverrides& overrides)
{
	const std::string* stored = overrides.Find(name);
	const std::string value = stored ? *stored : defaultValue;

	// Only an overridden field gets a reset, and it takes width from the widget
	// rather than sitting under it -- so a column of fields stays a column.
	const float resetWidth = stored ? 52.0f : 0.0f;

	BeginField(name.c_str(), stored ? nullptr : "Unchanged, so the script's own default applies.");
	if (resetWidth > 0.0f)
		ImGui::PushItemWidth(-resetWidth);

	switch (kind)
	{
		case 0:   // bool
		{
			bool flag = (value == "true" || value == "1");
			if (ImGui::Checkbox("##value", &flag))
				overrides.Set(name, flag ? "true" : "false");
			break;
		}
		case 1:   // int
		{
			int number = std::atoi(value.c_str());
			if (ImGui::DragInt("##value", &number))
				overrides.Set(name, std::to_string(number));
			break;
		}
		case 2:   // float
		{
			float number = (float)std::atof(value.c_str());
			if (ImGui::DragFloat("##value", &number, 0.01f))
				overrides.Set(name, FormatFloat(number));
			break;
		}
		case 4:   // Vec3
		{
			float parts[3] = { 0.0f, 0.0f, 0.0f };
			std::istringstream stream(value);
			stream >> parts[0] >> parts[1] >> parts[2];

			if (ImGui::DragFloat3("##value", parts, 0.01f))
			{
				overrides.Set(name, FormatFloat(parts[0]) + " " +
									 FormatFloat(parts[1]) + " " +
									 FormatFloat(parts[2]));
			}
			break;
		}
		case 3:   // string
		{
			char buffer[256]{};
			std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
			if (ImGui::InputText("##value", buffer, sizeof(buffer)))
				overrides.Set(name, buffer);
			break;
		}
		default:
			break;
	}

	if (resetWidth > 0.0f)
	{
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset"))
			overrides.Clear(name);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Back to the script's own default (%s)", defaultValue.c_str());
	}

	EndField();
}

// The native kinds and the managed ones are separate enums that happen to line
// up. Mapped explicitly rather than cast, so that adding a kind to one of them
// is a compile error here instead of a widget silently drawing the wrong thing.
int RageV::SceneHierarchyPanel::ScriptKindToWidget(ScriptFieldKind kind)
{
	switch (kind)
	{
		case ScriptFieldKind::Bool:   return 0;
		case ScriptFieldKind::Int:    return 1;
		case ScriptFieldKind::Float:  return 2;
		case ScriptFieldKind::String: return 3;
		case ScriptFieldKind::Vec3:   return 4;
	}
	return -1;
}

// Where a generated C++ script goes, or empty when there is no engine source.
//
// Baked in by CMake, so it points at the tree this editor was built from. An
// installed editor has none, and the dialog says so rather than writing a file
// into a folder nothing compiles.
std::filesystem::path RageV::SceneHierarchyPanel::EngineScriptsDir()
{
#ifdef RV_ENGINE_SCRIPTS_DIR
	std::error_code ec;
	const std::filesystem::path path = RV_ENGINE_SCRIPTS_DIR;
	if (std::filesystem::is_directory(path, ec))
		return path;
#endif
	return {};
}

// A valid identifier in either language. The two agree closely enough that one
// check covers both, and rejecting a bad name here beats a compiler error
// minutes later that names a file rather than the field that caused it.
bool RageV::SceneHierarchyPanel::IsIdentifier(const std::string& name)
{
	if (name.empty())
		return false;
	if (!std::isalpha((unsigned char)name[0]) && name[0] != '_')
		return false;

	return std::all_of(name.begin(), name.end(),
					   [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

// The file a new C++ script starts as.
//
// A working script with one editable field, registered -- the same shape the
// built-in scripts have, so the generated file and the worked examples teach the
// same thing. A blank file would only move the "what does a script look like"
// problem somewhere less convenient.
bool RageV::SceneHierarchyPanel::WriteNewNativeScript(const std::filesystem::path& file,
													  const std::string& name)
{
	std::ofstream out(file);
	if (!out)
	{
		RV_ERROR("Could not write {0}", file.string());
		return false;
	}

	out << "#include <rvpch.h>\n";
	out << "#include \"RageV/Scene/ScriptRegistry.h\"\n";
	out << "#include \"RageV/Scene/Components.h\"\n\n";
	out << "namespace RageV\n";
	out << "{\n";
	out << "\tclass " << name << " : public ScriptableEntity\n";
	out << "\t{\n";
	out << "\tpublic:\n";
	out << "\t\t// Public so the registration below can name it. C++ has no\n";
	out << "\t\t// reflection, so an editable field has to be declared explicitly --\n";
	out << "\t\t// unlike C#, where the inspector finds private fields on its own.\n";
	out << "\t\tfloat Speed = 1.0f;\n\n";
	out << "\t\tvoid OnCreate() override\n";
	out << "\t\t{\n";
	out << "\t\t}\n\n";
	out << "\t\t// Every fixed simulation step, not every frame. A frame may run zero\n";
	out << "\t\t// steps, one, or several -- so multiply rates by dt and the behaviour\n";
	out << "\t\t// stays the same at any simulation frequency.\n";
	out << "\t\tvoid OnUpdate(Timestep dt) override\n";
	out << "\t\t{\n";
	out << "\t\t\tTranslate({ 0.0f, Speed * dt.GetSeconds(), 0.0f });\n";
	out << "\t\t}\n";
	out << "\t};\n\n";
	out << "\t// The name here is what scene files store, so renaming it breaks every\n";
	out << "\t// scene that used it. Fields declared on the same line show up in the\n";
	out << "\t// inspector.\n";
	out << "\tRV_REGISTER_SCRIPT(" << name << ").Field<&" << name << "::Speed>(\"Speed\");\n";
	out << "}\n";

	// Nothing else is needed to make the registration survive, and an earlier
	// version of this generator wrote a `#pragma comment(linker, "/include:...")`
	// anchor that looked like it did. It cannot work: the directive lives in the
	// object file the linker is discarding, so it never arrives. RageV.lib is
	// linked whole instead -- see RageV/CMakeLists.txt, and the TemplateProbe
	// script that proves it still is.
	return true;
}
