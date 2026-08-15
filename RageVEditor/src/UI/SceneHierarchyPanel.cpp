#include "SceneHierarchyPanel.h"
#include "FieldEditor.h"
#include "CurveEditor.h"
#include "RageV/Asset/CurveSerializer.h"
#include "imgui.h"
#include "RageV/Project/Project.h"
#include <cctype>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "RageV/Managed/Interop.h"
#include "imgui_internal.h"
#include "RageV/Math/Math.h"
#include "EditorTheme.h"
#include "Widgets.h"
#include "AssetIcons.h"
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

// The inspector with nothing to inspect.
//
// "Nothing selected." was accurate and useless: an empty panel is where a tool
// has the reader's whole attention and the least to lose by spending three
// lines telling them what to do with it. This is the only place in the editor
// that explains selection at all.
static void DrawEmptySelection()
{
	using namespace RageV;

	ImGui::Dummy({ 0.0f, EditorTheme::Space::Wide });

	const float size = ImGui::GetTextLineHeight() * 2.4f;
	const float centre = (ImGui::GetContentRegionAvail().x - size) * 0.5f;
	if (centre > 0.0f)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centre);

	UI::DrawIcon(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), size,
				 UI::IconKind::Entity,
				 ImGui::GetColorU32(EditorTheme::Colors().TextDisabled));
	ImGui::Dummy({ size, size });

	ImGui::Dummy({ 0.0f, EditorTheme::Space::Base });

	ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextSecondary);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted("Nothing selected.");
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();

	ImGui::Dummy({ 0.0f, EditorTheme::Space::Tight });

	UI::PushTextScale(EditorTheme::Type::Caption);
	ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextDisabled);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted("Pick an entity in the Scene Hierarchy, or click one in "
						   "the viewport, and its components appear here.");
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
	UI::PopTextScale();
}

// Selection is also a request to *show* the thing selected.
//
// A viewport click already set this; what it could not do was make the row
// visible, because a child of a collapsed parent is never drawn. The flag is
// consumed by the next draw, which is the only place ImGui can open a node.
void RageV::SceneHierarchyPanel::SetSelectedEntity(Entity entity)
{
	// The asset goes even when the entity does not change: clicking an entity
	// that is already selected still means "show me this", and leaving the
	// panel on a file would make that click do nothing visible.
	if (entity)
		m_InspectedAsset = AssetHandle::Invalid();

	if (entity == m_Selected)
		return;

	m_Selected = entity;
	m_RevealSelection = (bool)entity;
}

void RageV::SceneHierarchyPanel::OnImGuiRender(bool* showHierarchy, bool* showProperties)
{
	if (showHierarchy && !*showHierarchy)
	{
		if (showProperties && *showProperties)
		{
			ImGui::Begin("Properties", showProperties);
			DrawProperties();
			ImGui::End();
		}
		return;
	}

	ImGui::Begin("Scene Hierarchy", showHierarchy);

	m_PendingDelete = {};
	m_PendingReparent = false;
	m_PendingCreateChild = false;

	// The chain from the selection up to its root, so the walk below knows
	// which nodes to open on the way down. Rebuilt rather than cached: a
	// reparent between two selections would invalidate it, and it is a handful
	// of lookups.
	m_RevealPath.clear();
	if (m_RevealSelection && m_Selected)
	{
		for (Entity parent = m_SceneRef->GetParent(m_Selected); parent;
			 parent = m_SceneRef->GetParent(parent))
		{
			m_RevealPath.push_back(parent.GetUUID());
		}
	}

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
		DrawProperties();
		ImGui::End();
	}
}

// What the Properties panel is showing: an entity, or an asset.
//
// Two sources, one panel, and the last thing clicked wins -- the same rule
// every editor with a project browser follows. Clicking an asset does not
// clear the entity selection, because the gizmo and the viewport outline hang
// off that and losing them to a glance at a file would be worse than the
// panel changing; clicking an entity clears the asset, because that is the
// gesture that means "show me this instead". ENGINE-NOTES 7s.
void RageV::SceneHierarchyPanel::DrawProperties()
{
	if (m_InspectedAsset.IsValid())
	{
		DrawAssetProperties();
		return;
	}

	if (m_Selected)
		ShowProperties(m_Selected);
	else
		DrawEmptySelection();
}

void RageV::SceneHierarchyPanel::DrawAssetProperties()
{
	const AssetMetadata& metadata = Assets::Registry::GetMetadata(m_InspectedAsset);

	if (!metadata.IsValid())
	{
		// The file was deleted, or the registry was rescanned and lost it.
		// Said rather than shown blank, and the selection dropped so the
		// panel does not sit on a dead handle for the rest of the session.
		ImGui::TextDisabled("That asset is no longer in the registry.");
		m_InspectedAsset = AssetHandle::Invalid();
		return;
	}

	UI::PushTextScale(1.15f);
	ImGui::TextUnformatted(std::filesystem::path(metadata.Path).filename().string().c_str());
	UI::PopTextScale();
	UI::TextCaption("%s -- %s", AssetTypeName(metadata.Type), metadata.Path.c_str());

	ImGui::Separator();

	switch (metadata.Type)
	{
		case AssetType::PostProfile:
			// The same drawer the camera's inspector uses, so a profile edited
			// from the content browser and one edited from the camera that
			// names it are the same rows in the same order with the same
			// tooltips. Two implementations of this would have diverged the
			// first time either was improved.
			UI::DrawPostProfile(m_InspectedAsset, m_SceneRef.get());
			break;

		case AssetType::ColorLut:
			// Likewise, and the drawer decides what a LUT has to show: a
			// `.rvlut` its knobs, a `.cube` a sentence explaining that a baked
			// table has none. ENGINE-NOTES 7v.
			UI::DrawColorLut(m_InspectedAsset);
			break;

		default:
			// Everything else is a reference to something with its own editor
			// or no editor at all. Saying so is better than an empty panel
			// that looks broken.
			ImGui::TextDisabled("No inspector for this asset type yet.");
			break;
	}
}

// What an entity *is*, for the row icon.
//
// Judged by components, most specific first: an entity carrying both a light
// and a mesh is a light with a bulb model on it, and calling it a mesh would
// bury the thing that makes it interesting. Order is the whole design here.
static RageV::UI::IconKind EntityIconKind(RageV::Entity entity)
{
	using namespace RageV;

	if (entity.HasComponent<CameraComponent>())          return UI::IconKind::Camera;
	if (entity.HasComponent<LightComponent>())           return UI::IconKind::Light;
	if (entity.HasComponent<ParticleEmitterComponent>()) return UI::IconKind::ParticleEmitter;
	if (entity.HasComponent<AudioSourceComponent>())     return UI::IconKind::AudioSource;
	if (entity.HasComponent<MeshComponent>())            return UI::IconKind::Mesh;

	return UI::IconKind::Entity;
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
		ImGui::PushStyleColor(ImGuiCol_Header, EditorTheme::Colors().AccentMuted);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorTheme::Colors().AccentMuted);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorTheme::Colors().Accent);
	}

	// Keyed by UUID rather than by handle: EnTT recycles handles, so a deleted
	// entity could hand its expansion state to an unrelated one.
	const uint64_t id = entity.GetUUID();

	// The node is drawn with an empty label and the row painted onto it, so an
	// icon can sit between the arrow and the name. A hierarchy of names in one
	// weight is a hierarchy you read line by line; the icon answers "what is
	// this" before the name is read at all.
	//
	// **Painted, not placed.** The icon and the name go straight into the draw
	// list rather than being ImGui items, because an item here would become
	// "the last item" -- and the drag source, the drop target and the context
	// menu below all attach to whatever that is. Adding them as items made
	// ImGui assert on `id != 0`, which is it refusing to make a text label
	// draggable.
	// An ancestor of a pending reveal is forced open, so a nested selection
	// arrives visible rather than hidden inside a collapsed parent.
	if (m_RevealSelection
		&& std::find(m_RevealPath.begin(), m_RevealPath.end(), id) != m_RevealPath.end())
	{
		ImGui::SetNextItemOpen(true);
	}

	const ImVec2 rowStart = ImGui::GetCursorScreenPos();
	const bool opened = ImGui::TreeNodeEx((void*)id, flags, "%s", "");

	// Reached the entity itself: bring it into view and consider the request
	// served. Cleared here rather than at the end of the frame so a second
	// entity with the same id further down cannot steal the scroll.
	if (m_RevealSelection && entity == m_Selected)
	{
		ImGui::SetScrollHereY(0.5f);
		m_RevealSelection = false;
	}

	if (selected)
		ImGui::PopStyleColor(3);

	{
		const auto& colors = EditorTheme::Colors();
		const float iconSize = ImGui::GetTextLineHeight();
		const float labelX = rowStart.x + ImGui::GetTreeNodeToLabelSpacing();
		const ImU32 tint = ImGui::GetColorU32(selected ? colors.TextPrimary
													   : colors.TextSecondary);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		UI::DrawIcon(draw, { labelX, rowStart.y }, iconSize, EntityIconKind(entity), tint);

		// Ellipsised by hand, because a draw-list AddText does not clip the way
		// an ImGui item does -- it would simply run off the edge of the panel
		// mid-letter. A name that had to be shortened says so, and gives the
		// whole thing back on hover.
		const float nameX = labelX + iconSize + EditorTheme::Space::Snug;
		const float room = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - nameX;

		const std::string& fullName = entity.GetName();
		std::string shown = fullName;
		if (room > 0.0f && ImGui::CalcTextSize(shown.c_str()).x > room)
		{
			while (shown.size() > 1
				   && ImGui::CalcTextSize((shown + "...").c_str()).x > room)
				shown.pop_back();
			shown += "...";
		}

		draw->AddText({ nameX, rowStart.y }, ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());

		if (shown != fullName && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", fullName.c_str());
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		m_Selected = entity;
		m_InspectedAsset = AssetHandle::Invalid();
	}

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
	using namespace RageV::UI;


	// What is left of the hand-written material section.
	//
	// Everything it used to draw -- base colour, metallic, roughness, occlusion,
	// emissive -- is now a reflected field on MeshComponent and comes out of the
	// generic field loop, tooltips and sliders included. Its own comment said
	// "phase 1 makes them assets and this goes away", and this is that.
	//
	// The advice does not survive reflection, so it stays here: a warning that
	// looks at a value rather than editing one.
	void DrawMaterialAdvice(MeshComponent& mesh)
	{
		// Only when this entity is actually deciding its own metallic value. If
		// the number came from the material asset, the place to fix it is the
		// asset, and the warning would follow every entity that uses it around.
		if (!mesh.OverrideMetallic)
			return;

		// Metals have no diffuse response, so a half-metallic surface is not
		// physical -- it is almost always an authoring mistake.
		if (mesh.Metallic > 0.05f && mesh.Metallic < 0.95f)
		{
			ImGui::TextColored(EditorTheme::Colors().Warning, "Partially metallic");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Real materials are either metal or not. Intermediate "
								  "values only make sense where a texture blends between "
								  "the two across a surface.");
		}
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

			// One Script entry, and only while the entity has no script in
			// either language. The C# component is not in the menu at all --
			// Language converts between them -- so without this an entity
			// already running a C# script would still be offered "Script" and
			// end up with both.
			//
			// strcmp, not ==: Name is a const char*, so == compares the two
			// pointers. The literals live in different translation units, and
			// whether they fold into one is up to the linker -- which is a test
			// that passes in one configuration and not the next.
			if (std::strcmp(desc.Name, "NativeScriptComponent") == 0 &&
				entity.HasComponent<ManagedScriptComponent>())
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
			// A vertical ellipsis, not an asterisk.
			//
			// The asterisk this replaces meant "component options", and an
			// asterisk means *modified* everywhere else in software -- the
			// menu bar three lines from here uses a mark in that exact sense
			// for unsaved changes. One glyph, two opposite meanings, in one
			// window. It also had no tooltip, so the only way to learn what it
			// did was to press it and find out.
			ImGui::SameLine(available.x - lineHeight * 0.5f);
			if (UI::IconButton("##options", UI::IconKind::More, "Component options"))
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

				if (UI::DrawField(field, component, m_SceneRef.get(), &desc, entity))
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
				DrawMaterialAdvice(*static_cast<MeshComponent*>(component));

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

		// The label does carry across, unlike the name and the overrides: it
		// belongs to the component rather than to either script, and losing what
		// you called this thing because you changed language would be a nuisance
		// with no reason behind it.
		if (toManaged && entity.HasComponent<NativeScriptComponent>())
		{
			const std::string label = entity.GetComponent<NativeScriptComponent>().Label;
			entity.RemoveComponent<NativeScriptComponent>();
			entity.AddComponent<ManagedScriptComponent>().Label = label;
		}
		else if (!toManaged && entity.HasComponent<ManagedScriptComponent>())
		{
			const std::string label = entity.GetComponent<ManagedScriptComponent>().Label;
			entity.RemoveComponent<ManagedScriptComponent>();
			entity.AddComponent<NativeScriptComponent>().Label = label;
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
	DrawScriptNameRow(script.Label);
	DrawScriptLanguageRow(true);

	if (!Managed::Interop::IsReady())
	{
		ImGui::TextColored(EditorTheme::Colors().Warning, "C# scripting is not running");
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
		// A scene can outlive the script it names, or name one that has been
		// written and not compiled. The second is one keystroke from fixed and
		// worth saying separately.
		std::error_code ec;
		const bool written = Project::GetActive()
			&& std::filesystem::exists(Project::Root() / "Scripts" /
									   (script.ScriptName + ".cs"), ec);

		if (written)
		{
			ImGui::TextColored(EditorTheme::Colors().Warning,
							   "'%s' is written but not built", script.ScriptName.c_str());
			ImGui::TextWrapped("File > Build Scripts, and it will run.");
		}
		else
		{
			ImGui::TextColored(EditorTheme::Colors().Danger,
							   "'%s' is not in any loaded assembly", script.ScriptName.c_str());
		}
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

// The component's label. A tag, not the script's identity.
//
// It names this component, the way the Tag row names the entity, and nothing
// looks it up or validates it. It was briefly bound to the script name instead,
// which meant typing in it renamed the script the entity was asking for and
// turned the row red the moment the name stopped matching something built.
// Which script runs is the row below's job, and that has to be a name the build
// actually has.
void RageV::SceneHierarchyPanel::DrawScriptNameRow(std::string& label)
{
	UI::BeginField("Name", "A label for this component. Free text -- it names the\n"
					   "component, not the script, which is chosen below.");

	// Zero-initialised, and copy() is given one byte less than the buffer, so
	// the terminator survives a label longer than the box.
	char buffer[128] = {};
	label.copy(buffer, sizeof(buffer) - 1);

	if (ImGui::InputText("##scriptlabel", buffer, sizeof(buffer)))
		label = buffer;

	UI::EndField();
}

// Whether a source file registers a script under its own file name.
//
// Without this every .cpp beside the scripts -- the builtins, this build's
// template probe -- would be offered as a script that does not exist. A file
// that does not name itself to RV_REGISTER_SCRIPT will not produce a script
// called after the file, whatever else it contains.
bool RageV::SceneHierarchyPanel::FileRegistersScript(const std::filesystem::path& file,
													 const std::string& name)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
		return false;

	std::ostringstream text;
	text << in.rdbuf();
	return text.str().find("RV_REGISTER_SCRIPT(" + name + ")") != std::string::npos;
}

// Scripts that exist as a file but are not in this build yet.
//
// This is why the dropdown looked broken after New Script: a C++ script is
// compiled into the engine and a C# one into the project assembly, so neither
// is in the list the moment it is written. Leaving them out made the file you
// had just created unreachable -- there is no other way to name a script now
// that Name is a label. Listing them says what is true: it exists, it is not
// built yet.
std::vector<std::string> RageV::SceneHierarchyPanel::ScanUnbuiltScripts(
	const std::vector<std::string>& built, bool managed)
{
	std::vector<std::string> pending;

	// Both languages live in the project now: C# in Scripts/, C++ in Source/,
	// where the game module builds from. The engine's own scripts folder is
	// not scanned -- its scripts are compiled in and always in `built`.
	if (!Project::GetActive())
		return pending;

	const std::filesystem::path dir =
		Project::Root() / (managed ? "Scripts" : "Source");

	std::error_code ec;
	if (dir.empty() || !std::filesystem::is_directory(dir, ec))
		return pending;

	const std::filesystem::path extension = managed ? ".cs" : ".cpp";

	for (const std::filesystem::directory_entry& entry :
		 std::filesystem::directory_iterator(dir, ec))
	{
		if (!entry.is_regular_file(ec) || entry.path().extension() != extension)
			continue;

		const std::string name = entry.path().stem().string();
		if (std::find(built.begin(), built.end(), name) != built.end())
			continue;

		if (!managed && !FileRegistersScript(entry.path(), name))
			continue;

		pending.push_back(name);
	}

	std::sort(pending.begin(), pending.end());
	return pending;
}

// The language row, shared by both script components.
//
// Changing it converts the entity from one script component to the other. That
// cannot be done here -- the caller is iterating the component list and adding
// or removing one invalidates it -- so the choice is queued and applied after
// the loop, exactly as component removal already is.
void RageV::SceneHierarchyPanel::DrawScriptLanguageRow(bool managed)
{
	UI::BeginField("Language",
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

	UI::EndField();
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

	UI::BeginField("Script", "The name written into the scene file. Renaming a script\n"
						 "breaks every scene that used it.");

	const std::string current = scriptName.empty() ? "(none)" : scriptName;

	if (ImGui::BeginCombo("##script", current.c_str()))
	{
		// Scanned when the list opens rather than every frame it is open: it
		// touches the disk, and the answer cannot change while it is on screen.
		if (ImGui::IsWindowAppearing())
			m_UnbuiltScripts = ScanUnbuiltScripts(available, managed);

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

		// Selectable, not greyed out: writing a script and attaching it before
		// the build catches up is the normal order of doing this. The scene
		// stores the name either way, and the entity starts working the moment
		// the build has it.
		if (!m_UnbuiltScripts.empty())
		{
			ImGui::Separator();
			for (const std::string& name : m_UnbuiltScripts)
			{
				// The same suffix for both languages now: either way the fix is
				// Build Scripts, not an engine rebuild.
				const std::string entry = name + "   (not built)";

				if (ImGui::Selectable(entry.c_str(), name == scriptName) && name != scriptName)
				{
					scriptName = name;
					changed = true;
				}
			}
		}

		ImGui::Separator();

		// The popup cannot be opened from inside the combo -- the combo closes
		// and takes the popup with it. Flagged here, opened after EndCombo.
		if (ImGui::Selectable("New Script..."))
			m_OpenNewScript = true;

		ImGui::EndCombo();
	}

	UI::EndField();

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

	// Both languages need a project: the script goes into its Scripts/ or
	// Source/, and there is nowhere else it could belong.
	if (!Project::GetActive())
	{
		ImGui::TextWrapped("Open a project first. A script belongs to one.");
		ImGui::Separator();
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return false;
	}

	if (!managed)
	{
		// Into the project's Source/, which the game module builds from. It
		// used to go into the engine's own scripts folder -- which meant a
		// packaged editor had nowhere to put one, and every new script cost an
		// engine rebuild. Both problems were the same problem, and the game
		// module is the fix for both.
		const std::filesystem::path scripts = Project::Root() / "Source";

		ImGui::Text("Class name");
		ImGui::SetNextItemWidth(280.0f);
		ImGui::InputText("##cppname", m_NewScriptName, sizeof(m_NewScriptName));

		const std::string name(m_NewScriptName);
		const bool valid = IsIdentifier(name);

		std::error_code ec;
		const std::filesystem::path file = scripts / (name + ".cpp");
		const bool exists = valid && std::filesystem::exists(file, ec);

		if (!name.empty() && !valid)
			ImGui::TextColored(EditorTheme::Colors().Danger, "Not a valid C++ class name");
		else if (exists)
			ImGui::TextColored(EditorTheme::Colors().Danger, "Source/%s.cpp already exists", name.c_str());
		else if (valid)
			ImGui::TextDisabled("Source/%s.cpp", name.c_str());
		else
			ImGui::TextDisabled(" ");

		ImGui::Spacing();
		ImGui::TextWrapped("File > Build Scripts compiles it.");

		ImGui::Separator();

		const bool ok = valid && !exists;
		ImGui::BeginDisabled(!ok);
		if (ImGui::Button("Create") && ok)
		{
			std::filesystem::create_directories(scripts, ec);
			if (WriteNewNativeScript(file, name))
			{
				chosenName = name;
				created = true;
				RV_INFO("Created Source/{0}.cpp -- File > Build Scripts to compile it", name);
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
		ImGui::TextColored(EditorTheme::Colors().Danger, "Not a valid C# class name");
	else if (exists)
		ImGui::TextColored(EditorTheme::Colors().Danger, "Scripts/%s.cs already exists", name.c_str());
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
// look like, and a template that already overrides both rates puts the choice
// between them in the one place somebody is certain to read.
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
	out << "\t//\n";
	out << "\t// Gameplay goes here: anything the physics, another player or a\n";
	out << "\t// replay has to agree with.\n";
	out << "\tpublic override void OnTick(float deltaTime)\n";
	out << "\t{\n";
	out << "\t\tTranslate(new Vector3(0.0f, m_Speed * deltaTime, 0.0f));\n";
	out << "\t}\n\n";
	out << "\t// Once per frame, with the real elapsed time, which varies. For things\n";
	out << "\t// nothing else has to agree about: a camera, a fade, a number counting\n";
	out << "\t// up on the screen. Delete it if this script has none.\n";
	out << "\tpublic override void OnFrame(float deltaTime)\n";
	out << "\t{\n";
	out << "\t}\n";
	out << "}\n";

	return true;
}

// The C++ script component.
void RageV::SceneHierarchyPanel::DrawNativeScript(NativeScriptComponent& script)
{
	DrawScriptNameRow(script.Label);
	DrawScriptLanguageRow(false);

	// Overrides belong to the script that had them. Carrying them across a
	// change of script would apply one script's values to another's identically
	// named field, which is worse than losing them.
	if (DrawScriptPicker(ScriptRegistry::GetNames(), script.ScriptName, false))
		script.Fields.Values.clear();

	// Creating one selects it, same as the C# side: the next step is Build
	// Scripts, not hunting for the new name in a dropdown.
	std::string created;
	if (DrawNewScriptPopup(false, created))
	{
		script.ScriptName = created;
		script.Fields.Values.clear();
	}

	if (script.ScriptName.empty())
	{
		ImGui::TextDisabled("Runs on the fixed step, only while playing.");
		return;
	}

	if (!ScriptRegistry::IsRegistered(script.ScriptName))
	{
		// Two different situations, and telling them apart is the whole value of
		// saying anything: a script whose source is sitting right there is one
		// build away, while one with no source behind it is a scene naming
		// something that no longer exists.
		std::error_code ec;
		const bool written = Project::GetActive()
			&& std::filesystem::exists(Project::Root() / "Source" /
									   (script.ScriptName + ".cpp"), ec);

		if (written)
		{
			ImGui::TextColored(EditorTheme::Colors().Warning,
							   "'%s' is written but not built", script.ScriptName.c_str());
			ImGui::TextWrapped("File > Build Scripts, and it will run.");
		}
		else
		{
			ImGui::TextColored(EditorTheme::Colors().Danger,
							   "'%s' is not registered", script.ScriptName.c_str());
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The scene refers to a script this build does not contain.\n"
								  "The entity will run nothing.");
			}
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

	UI::BeginField(name.c_str(), stored ? nullptr : "Unchanged, so the script's own default applies.");
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

	UI::EndField();
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
// A working script with one editable field -- the same shape the built-in
// scripts have, so the generated file and the worked examples teach the same
// thing. A blank file would only move the "what does a script look like"
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
	out << "\t// The class's name is what scene files store, so renaming it breaks\n";
	out << "\t// every scene that used it.\n";
	out << "\tclass " << name << " : public ScriptableEntity\n";
	out << "\t{\n";
	out << "\tpublic:\n";
	out << "\t\t// In the inspector and stored in the scene -- the marker is the\n";
	out << "\t\t// whole registration; the build generates the rest. Public,\n";
	out << "\t\t// because the generated code names it from outside the class.\n";
	out << "\t\tRVShowInEditor\n";
	out << "\t\tfloat Speed = 1.0f;\n\n";
	out << "\t\tvoid OnCreate() override\n";
	out << "\t\t{\n";
	out << "\t\t}\n\n";
	out << "\t\t// Every fixed simulation step, not every frame. A frame may run zero\n";
	out << "\t\t// steps, one, or several -- so multiply rates by dt and the behaviour\n";
	out << "\t\t// stays the same at any simulation frequency.\n";
	out << "\t\t//\n";
	out << "\t\t// Gameplay goes here: anything the physics, another player or a\n";
	out << "\t\t// replay has to agree with.\n";
	out << "\t\tvoid OnTick(Timestep dt) override\n";
	out << "\t\t{\n";
	out << "\t\t\tTranslate({ 0.0f, Speed * dt.GetSeconds(), 0.0f });\n";
	out << "\t\t}\n\n";
	out << "\t\t// Every frame, with the real elapsed time, which varies. For things\n";
	out << "\t\t// nothing else has to agree about: a camera, a fade, a number\n";
	out << "\t\t// counting up on the screen. Delete it if this script has none.\n";
	out << "\t\tvoid OnFrame(Timestep dt) override\n";
	out << "\t\t{\n";
	out << "\t\t}\n";
	out << "\t};\n";
	out << "}\n";

	// No registration block: RVShowInEditor above the field is the whole
	// registration -- rvgen generates the rest when the module builds. The
	// file lands in the project's Source/, which builds into the game module,
	// a DLL linked from its object files, so the generated registrar is never
	// discarded the way it would be from a static library. TemplateProbe.cpp
	// in the engine proves this shape compiles untouched; scenetest's rvgen
	// fixture proves the same shape actually registers.
	return true;
}
