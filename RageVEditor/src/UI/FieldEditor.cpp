#include "FieldEditor.h"
#include "CurveEditor.h"
#include "EditorTheme.h"
#include "Widgets.h"
#include "AssetIcons.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Asset/CurveSerializer.h"
#include "RageV/Asset/PostProfileSerializer.h"
#include "RageV/Asset/LutRecipe.h"
#include "RageV/Core/Log.h"
#include "RageV/Utils/PlatformUtils.h"
#include "RageV/Managed/Interop.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/ScriptRegistry.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

// One reflected field, drawn.
//
// Lifted out of SceneHierarchyPanel.cpp, where it was an anonymous-namespace
// helper serving exactly one caller: the entity inspector. Then settings blocks
// stopped being one struct and became three (ENGINE-NOTES 7s), and the same
// drawing had to happen in three more places -- the project's render settings,
// a post profile under the camera that names it, and that same profile again
// when the asset itself is selected in the content browser.
//
// **Moved rather than copied.** A second implementation of "draw a FieldDesc"
// would render the same field two ways the first time somebody improved one of
// them, and the post profile is deliberately drawn in two places at once: a
// grade that looks different depending on which panel you opened it from is
// worse than no inline editing at all.
//
// The scene, the component and the owning entity are pointers now, because a
// settings block has none of them. Every branch that needs one checks first;
// what those branches offer -- an entity picker, a method binding, an animation
// clip list -- has no meaning for a block of numbers in a file.

namespace RageV::UI
{

	// One property row: label left, control right, both sized against the panel
	// rather than against a number somebody typed once.
	//
	// A one-row table per field rather than one table around the whole
	// inspector, because the fields are drawn from a dozen call sites that do
	// not know about each other. Several one-row tables with the same
	// proportions line up with each other, which is the property that matters;
	// what is given up is dragging the divider, so they are not resizable
	// (a drag that moved one row and not the rest would read as a bug).
	// BeginTable answers false when the whole table is culled -- scrolled out
	// of view, or in a panel collapsed to nothing -- and EndTable must not be
	// called then. Rather than make thirteen call sites branch, the miss falls
	// back to a plain label-then-control row: it does not align, but nothing
	// nothing is visible to align with, and the pairing stays unconditional.
	std::vector<bool> g_FieldUsedTable;

	// `labelFraction` exists for one row type: three axis fields and their
	// badges need more of the width than a single control does, and giving
	// every row the vector's proportions would waste half the panel on the
	// rest.
	void BeginField(const char* label, const char* tooltip, float labelFraction)
	{
		ImGui::PushID(label);

		const bool table = UI::BeginProperties("##field", labelFraction, false);
		g_FieldUsedTable.push_back(table);

		if (table)
		{
			UI::PropertyRow(label, tooltip);
			return;
		}

		ImGui::TextUnformatted(label);
		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	void EndField()
	{
		if (!g_FieldUsedTable.empty())
		{
			if (g_FieldUsedTable.back())
				UI::EndProperties();
			g_FieldUsedTable.pop_back();
		}
		ImGui::PopID();
	}

	bool DrawVec3(const char* label, Vec3& values, float resetValue)
	{
		// The same 42% as every other row, unconditionally.
		//
		// Two failed attempts before this one, both of them me tuning a number
		// instead of thinking. A flat 30% fixed the Z field clipping on a small
		// window and gave the inspector two left edges -- a vector row starting
		// its controls further left than the plain row above it, which reads as
		// wrong long before anyone works out why. Then an adaptive fraction
		// that widened when there was room, which misses the point entirely:
		// **alignment requires the same fraction, so anything adaptive is
		// misaligned by construction.**
		//
		// So it is binary, and alignment wins. At 42% the three fields get
		// ~28px each in a 1600-wide window, which shows "0.75" fine; on a
		// genuinely small window they hit DragVec3's floor and the row
		// overflows its cell, where the table clips it. A clipped digit is a
		// smaller problem than a column that does not line up, and unlike a
		// negative width it is not an assertion.
		BeginField(label, nullptr);
		const bool changed = UI::DragVec3("##vec", values, resetValue);
		EndField();
		return changed;
	}

	// For the clip search below. ASCII only and deliberately so: it compares a
	// name against what somebody typed, and both come from the same keyboard.
	std::string ToLower(std::string value)
	{
		for (char& c : value)
		{
			if (c >= 'A' && c <= 'Z')
				c = (char)(c - 'A' + 'a');
		}
		return value;
	}

	// An animation clip, chosen by name from the model on this entity.
	//
	// Returns whether the value changed. -1 is the bind pose and is always
	// offered: it is a legitimate state -- what a character that has never
	// been told to move looks like -- and not an error.
	//
	// The search box is not decoration. A rig can carry dozens of clips with
	// names that share a prefix ("run_forward", "run_left", "run_stop"), and a
	// list that has to be scrolled past its own naming convention is a list
	// that gets the wrong entry picked.
	bool DrawClipField(int* value, Entity owner)
	{
		std::vector<std::string> names;
		if (owner && owner.HasComponent<MeshComponent>())
		{
			const AssetHandle mesh = owner.GetComponent<MeshComponent>().Mesh;
			if (const std::vector<Anim::Clip>* clips = Assets::Manager::GetClips(mesh))
			{
				for (size_t i = 0; i < clips->size(); i++)
				{
					const std::string& name = (*clips)[i].Name;
					// glTF does not require an animation to be named, and an
					// unnamed one still has to be selectable.
					names.push_back(name.empty() ? "Clip " + std::to_string(i) : name);
				}
			}
		}

		const int current = *value;
		const bool inRange = current >= 0 && current < (int)names.size();

		std::string preview;
		if (current < 0)
			preview = "(bind pose)";
		else if (inRange)
			preview = names[current];
		else
			preview = "Clip " + std::to_string(current) + "  (missing)";

		bool changed = false;

		// One buffer: only one combo can be open at a time, and it is cleared
		// every time one opens.
		static char filter[64] = {};

		if (ImGui::BeginCombo("##value", preview.c_str()))
		{
			if (ImGui::IsWindowAppearing())
			{
				filter[0] = '\0';
				ImGui::SetKeyboardFocusHere();
			}

			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##search", "Search", filter, sizeof(filter));
			ImGui::Separator();

			const std::string needle = ToLower(filter);
			const auto matches = [&](const std::string& text)
			{
				return needle.empty() || ToLower(text).find(needle) != std::string::npos;
			};

			if (matches("bind pose") && ImGui::Selectable("(bind pose)", current < 0))
			{
				*value = -1;
				changed = true;
			}

			for (size_t i = 0; i < names.size(); i++)
			{
				if (!matches(names[i]))
					continue;

				if (ImGui::Selectable(names[i].c_str(), current == (int)i))
				{
					*value = (int)i;
					changed = true;
				}
			}

			// An index the model no longer has still has to be visible, or
			// opening the dropdown to look would be a way to lose it silently
			// -- the same rule the method binding follows.
			if (current >= 0 && !inRange)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().Danger);
				ImGui::Selectable(preview.c_str(), true);
				ImGui::PopStyleColor();
			}

			ImGui::EndCombo();
		}

		if (ImGui::IsItemHovered() && names.empty())
		{
			ImGui::SetTooltip("This entity's mesh has no animations.\n\n"
							  "Only a model imported with a skeleton carries any.");
		}

		return changed;
	}

	// Every method an entity's scripts declare, in both languages, merged.
	//
	// Answered from the script *names* rather than from instances, because this
	// runs while the scene is stopped -- which is when somebody is authoring a
	// button, and when nothing has been instantiated anywhere.
	std::vector<std::string> BindableMethods(Entity entity)
	{
		std::vector<std::string> names;
		if (!entity)
			return names;

		if (entity.HasComponent<NativeScriptComponent>())
		{
			const std::string& script = entity.GetComponent<NativeScriptComponent>().ScriptName;
			for (const ScriptMethod& method : ScriptRegistry::MethodsOf(script))
				names.push_back(method.Name);
		}

		if (entity.HasComponent<ManagedScriptComponent>() && Managed::Interop::IsReady()
			&& Managed::Interop::Managed().ListMethods)
		{
			const std::string& script = entity.GetComponent<ManagedScriptComponent>().ScriptName;

			// Asked for the length first, as every other buffer call here
			// does: a script with many methods must not come back clipped.
			const int32_t needed =
				Managed::Interop::Managed().ListMethods(script.c_str(), nullptr, 0);

			if (needed > 0)
			{
				std::string listing((size_t)needed + 1, '\0');
				Managed::Interop::Managed().ListMethods(script.c_str(), listing.data(),
														(int32_t)listing.size());

				std::stringstream stream(listing.c_str());
				std::string line;
				while (std::getline(stream, line))
				{
					if (!line.empty())
						names.push_back(line);
				}
			}
		}

		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		return names;
	}

	// A name for a new asset that no file in the folder already has.
	//
	// Numbered rather than uniquified with a suffix, because the sequence is
	// what a person expects: Untitled, Untitled 1, Untitled 2. The registry is
	// asked rather than the filesystem so that a handle minted this session
	// counts even if the scan has not run again.
	std::string UnusedAssetPath(const std::string& folder, const std::string& stem,
								const char* extension)
	{
		for (int index = 0; index < 1000; index++)
		{
			std::string name = folder + "/" + stem;
			if (index > 0)
				name += " " + std::to_string(index);
			name += extension;

			if (!Assets::Registry::GetHandle(name).IsValid())
				return name;
		}

		return {};
	}

	// Takes the scene because one field type needs it: an entity reference is
	// stored as a UUID and has to be shown as a *name*, which only the scene
	// can answer -- and has to keep saying something useful when the answer is
	// "that entity is gone".
	//
	// Takes the ComponentDesc and the owning entity for one *hint*: a
	// method-name field says which sibling field holds the entity whose methods
	// to offer, so resolving it means reading the component's other fields --
	// and an unset one falls back to the entity the component is on, which a
	// type-erased `void*` into a pool cannot answer for itself.
	bool DrawField(const FieldDesc& field, void* component, Scene* scene,
				   const ComponentDesc* desc, Entity owner)
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

				// An animation clip: offered by name, from the model on this
				// entity. An index is what the component stores and is the
				// wrong thing to *ask* for -- "2" says nothing about which
				// animation it is, and the answer changes when the model is
				// re-exported with a clip inserted.
				if (hint.ClipList)
					changed = DrawClipField((int*)value, owner);
				else
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
			case FieldType::Vec2:
			{
				// Two plain boxes rather than the coloured X/Y/Z badges the
				// transform uses. Those badges say "this is a direction in the
				// world", and a UI anchor is a fraction of a rectangle -- the
				// same decoration would be claiming something untrue.
				BeginField(field.DisplayName.c_str(), hint.Tooltip);
				changed = ImGui::DragFloat2("##value", Math::ValuePtr(*(Vec2*)value),
											hint.Speed, hint.Min, hint.Max);
				EndField();
				break;
			}
			case FieldType::Vec3:
			{
				if (hint.Kind == FieldHint::Widget::Color)
				{
					BeginField(field.DisplayName.c_str(), hint.Tooltip);
					// NoInputs, like every other colour in the editor. The
					// three numeric boxes ImGui shows by default eat the whole
					// row to express something nobody reads as numbers, and
					// left this path looking unlike the material editor beside
					// it. The picker still has them, one click away.
					changed = ImGui::ColorEdit3("##value", Math::ValuePtr(*(Vec3*)value),
												ImGuiColorEditFlags_NoInputs);
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
					changed = ImGui::ColorEdit4("##value", Math::ValuePtr(*(Vec4*)value),
												ImGuiColorEditFlags_NoInputs
												| ImGuiColorEditFlags_AlphaPreviewHalf);
				else
					changed = ImGui::DragFloat4("##value", Math::ValuePtr(*(Vec4*)value), hint.Speed);
				EndField();
				break;
			}
			case FieldType::Entity:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);

				EntityRef& reference = *(EntityRef*)value;
				Entity target = scene ? scene->GetEntityByUUID(reference.Value) : Entity();

				// Three states, and the third is the one worth drawing
				// carefully. An empty slot is ordinary. A resolved one shows a
				// name. A slot that *names* something the scene does not have
				// is a broken reference -- deleting the target leaves exactly
				// this -- and if it drew as "None" nobody would ever find out
				// why the button stopped working.
				const bool missing = reference.IsValid() && !target;

				std::string label;
				if (!reference.IsValid())
					label = "None";
				else if (target)
					label = target.GetName();
				else
					label = "Missing entity";

				if (missing)
					ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().Danger);

				ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));

				if (missing)
					ImGui::PopStyleColor();

				// The hierarchy is already a drag source and has been since
				// reparenting was built, so this costs one target and no new
				// payload type.
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RAGEV_ENTITY"))
					{
						Entity dropped{ *(const entt::entity*)payload->Data, scene };
						if (dropped)
						{
							reference = EntityRef(dropped.GetUUID());
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}

				// Clearing needs to be possible and must not be the same
				// gesture as opening something, so it is the context menu
				// rather than a click.
				if (ImGui::BeginPopupContextItem("##entity-ref"))
				{
					if (ImGui::MenuItem("Clear", nullptr, false, reference.IsValid()))
					{
						reference = EntityRef();
						changed = true;
					}
					ImGui::EndPopup();
				}

				if (ImGui::IsItemHovered())
				{
					if (missing)
					{
						ImGui::SetTooltip("This slot names entity %llu, which is not in the "
										  "scene.\nIt was probably deleted. Drag a "
										  "replacement in, or right-click to clear.",
										  (unsigned long long)(uint64_t)reference);
					}
					else
					{
						ImGui::SetTooltip("Drag an entity from the Hierarchy to set this.\n"
										  "Right-click to clear.");
					}
				}

				EndField();
				break;
			}
			case FieldType::Asset:
			{
				BeginField(field.DisplayName.c_str(), hint.Tooltip);

				AssetHandle& handle = *(AssetHandle*)value;
				changed = DrawAssetPicker("##value", handle, hint.Accepts);

				EndField();

				// A post profile is edited where it is used, for the reason the
				// curve below it is: the alternative is a round trip to the
				// content browser to change an exposure. Unlike a curve it is
				// *also* editable from the browser, because a grade is shared
				// between cameras and wanting to open the asset itself is
				// ordinary. One drawer serves both, so the two cannot disagree.
				if (hint.Accepts == AssetType::PostProfile && handle.IsValid())
					DrawPostProfile(handle, scene);

				// And a LUT under its own slot, for the same reason: the
				// moment you want to change a grade is the moment you are
				// looking at the profile that uses it. One drawer, shared with
				// the content browser, so the two cannot disagree.
				if (hint.Accepts == AssetType::ColorLut && handle.IsValid())
					DrawColorLut(handle);

				// A curve is edited where it is used. Everything else here is a
				// reference to something with its own home -- a mesh, a texture
				// -- but a ramp only means anything beside the emitter it
				// shapes, so it gets drawn under its own field rather than in a
				// window you have to go and find.
				if (hint.Accepts == AssetType::Curve && handle.IsValid())
				{
					if (const Curve* loaded = Assets::Manager::GetCurve(handle))
					{
						// A copy to edit: the cached one is what the renderer is
						// sampling this frame, and editing it in place would
						// change the picture halfway through drawing it.
						Curve editable = *loaded;

						if (UI::CurveEditor::Draw(field.Name, editable, handle))
						{
							const std::filesystem::path path =
								Assets::Registry::GetAbsolutePath(handle);

							// Written through, then the cache dropped. The
							// alternative -- updating the cache and saving on
							// some later event -- is how an edit survives on
							// screen and not on disk.
							if (!path.empty() && Assets::CurveSerializer::Save(editable, path))
								Assets::Manager::ReloadCurve(handle);
						}
					}
				}
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

				// A method binding: offered from what the target's script
				// actually declares, for the same reason scripts are picked
				// rather than typed. This is the field the whole "a name in a
				// scene file has no compiler behind it" problem lives in, and a
				// dropdown is what keeps a *new* binding from being wrong --
				// nothing here can stop an *old* one going stale when the
				// method is renamed later, which is why the click reports it.
				if (hint.MethodsOn)
				{
					std::string& current = *(std::string*)value;

					// The sibling that says whose methods to offer, and the
					// same empty-means-this-entity rule the dispatch uses.
					Entity target = owner;
					if (desc && scene)
					{
						for (const FieldDesc& sibling : desc->Fields)
						{
							if (sibling.Name && std::string(sibling.Name) == hint.MethodsOn
								&& sibling.Type == FieldType::Entity)
							{
								const EntityRef& reference = *(EntityRef*)sibling.Access(component);
								if (reference.IsValid())
									target = scene->GetEntityByUUID(reference.Value);
								break;
							}
						}
					}

					const std::vector<std::string> methods = BindableMethods(target);

					if (ImGui::BeginCombo("##value", current.empty() ? "(none)" : current.c_str()))
					{
						if (ImGui::Selectable("(none)", current.empty()))
						{
							current.clear();
							changed = true;
						}

						for (const std::string& name : methods)
						{
							if (ImGui::Selectable(name.c_str(), name == current))
							{
								current = name;
								changed = true;
							}
						}

						// A binding whose method the target no longer declares
						// still has to be selectable, or opening the dropdown
						// to look would silently be a way to lose it.
						if (!current.empty()
							&& std::find(methods.begin(), methods.end(), current) == methods.end())
						{
							ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().Danger);
							ImGui::Selectable((current + "  (not found)").c_str(), true);
							ImGui::PopStyleColor();
						}

						ImGui::EndCombo();
					}

					if (ImGui::IsItemHovered() && methods.empty())
					{
						ImGui::SetTooltip(
							"That entity's script declares no bindable methods.\n\n"
							"C++: register one with .Method<&Type::Name>(\"Name\").\n"
							"C#: make it public, no arguments, returning void.");
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
}

namespace RageV::UI
{
	bool DrawAssetPicker(const char* id, AssetHandle& handle, AssetType accepts)
	{
		bool changed = false;

		ImGui::PushID(id);

		// The slot itself: the current name, and the drop target. A button
		// rather than a label, because a target you cannot see is one nobody
		// finds.
		//
		// The dropdown arrow is on the same control rather than beside it: two
		// controls in a property row halve the width available to the name,
		// and the name is the part that has to be readable.
		const std::string name = Assets::Manager::GetDisplayName(handle);
		const bool opened = ImGui::BeginCombo("##pick", name.c_str());

		// Before the popup body, so a drop lands on the closed control.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RAGEV_ASSET"))
			{
				const AssetHandle dropped = *(const AssetHandle*)payload->Data;
				const AssetMetadata& metadata = Assets::Registry::GetMetadata(dropped);

				// Refused rather than stored: a handle of the wrong type
				// resolves to nothing, and silently accepting it would present
				// as the field simply not working.
				if (accepts == AssetType::None || metadata.Type == accepts)
				{
					handle = dropped;
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (!opened && ImGui::IsItemHovered() && handle.IsValid())
			ImGui::SetTooltip("%s\n\nPick from the list, or drop an asset from the Content browser.",
							  Assets::Registry::GetMetadata(handle).Path.c_str());

		if (opened)
		{
			// Filtered as you type. A project has hundreds of textures and
			// scrolling a list of hundreds is not choosing, it is hunting.
			static char s_Filter[64] = {};
			if (ImGui::IsWindowAppearing())
			{
				s_Filter[0] = '\0';
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", "Search", s_Filter, sizeof(s_Filter));

			const std::string needle = ToLower(s_Filter);

			if (ImGui::Selectable("None", !handle.IsValid()))
			{
				handle = AssetHandle::Invalid();
				changed = true;
			}

			// Creating one from here rather than from the content browser
			// only: the moment you want a profile is the moment you are
			// looking at the camera that needs one, and sending somebody to
			// another panel to make an empty file first is the kind of step
			// that makes a feature go unused.
			if (accepts == AssetType::PostProfile && Assets::Registry::IsInitialised())
			{
				ImGui::Separator();
				if (ImGui::Selectable("New post profile..."))
				{
					const std::string path =
						UnusedAssetPath("post", "Untitled", ".rvpostprofile");

					// Written with the defaults, which is the neutral grade --
					// so a camera that has just been given a profile looks
					// exactly as it did a moment ago, and every change from
					// here is a change somebody made.
					const AssetHandle created =
						Assets::Manager::CreatePostProfile(PostSettings{}, path);

					if (created.IsValid())
					{
						handle = created;
						changed = true;
					}
					else
					{
						RV_ERROR("Could not create a post profile at {0}", path);
					}
				}
				ImGui::Separator();
			}

			// The same door for LUTs, and the only way to author a look
			// without leaving the engine. A `.cube` cannot be created here
			// because a `.cube` is somebody else's baked output; what this
			// makes is a `.rvlut` recipe. ENGINE-NOTES 7v.
			if (accepts == AssetType::ColorLut && Assets::Registry::IsInitialised())
			{
				ImGui::Separator();
				if (ImGui::Selectable("New colour LUT..."))
				{
					const std::string path =
						UnusedAssetPath("luts", "Untitled", ".rvlut");

					// Defaults, which bake the identity table exactly -- so a
					// profile that has just been given a LUT looks precisely
					// as it did a moment ago, and every change from here is a
					// change somebody made.
					const AssetHandle created =
						Assets::Manager::CreateLutRecipe(Assets::LutRecipe{}, path);

					if (created.IsValid())
					{
						handle = created;
						changed = true;
					}
					else
					{
						RV_ERROR("Could not create a LUT recipe at {0}", path);
					}
				}
				ImGui::Separator();
			}

			for (const auto& [path, metadata] : Assets::Registry::All())
			{
				if (accepts != AssetType::None && metadata.Type != accepts)
					continue;
				if (!needle.empty() && ToLower(path).find(needle) == std::string::npos)
					continue;

				ImGui::PushID((int)(uint64_t)metadata.Handle);
				if (ImGui::Selectable(path.c_str(), metadata.Handle == handle))
				{
					handle = metadata.Handle;
					changed = true;
				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		ImGui::PopID();
		return changed;
	}

	void DrawPostProfile(AssetHandle handle, Scene* scene)
	{
		PostSettings* profile = Assets::Manager::GetPostProfile(handle);
		if (!profile)
			return;

		ImGui::Indent();

		// Who is looking through it. Nothing in this panel reaches the picture
		// unless a camera names this profile, and a scene's camera names none
		// until somebody picks one -- so a fresh project plus a freshly graded
		// profile is a screenful of controls with no effect and no complaint.
		if (scene)
		{
			int users = 0;
			auto view = scene->GetRegistry().view<CameraComponent>();
			for (auto entity : view)
			{
				if (view.get<CameraComponent>(entity).PostProfile == handle)
					users++;
			}

			if (users == 0)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().Warning);
				ImGui::TextWrapped("No camera in this scene uses this profile, so nothing "
								   "below changes what you see. Pick it on a camera's "
								   "Post profile row first.");
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}
		}

		// Which file is being written, said plainly. A profile is an asset and
		// an asset is shared: editing one from a camera's inspector looks
		// exactly like editing that camera, and it is not.
		const std::string& path = Assets::Registry::GetMetadata(handle).Path;
		UI::TextCaption("Editing %s -- shared by every camera using it",
						path.empty() ? "this profile" : path.c_str());

		// Edited in place rather than through a copy, unlike the curve below.
		// The two want opposite things: a curve is *sampled* by the simulation
		// while the editor draws, so editing the cached one changes the
		// picture halfway through a frame; a post profile is read once when
		// the frame graph is built, before any of this runs, so writing
		// through is what makes the preview live.
		if (DrawFields(PostSettingsRegistry::Fields(), profile))
		{
			const std::filesystem::path file = Assets::Registry::GetAbsolutePath(handle);
			if (!file.empty())
				Assets::PostProfileSerializer::Save(*profile, file);
		}

		ImGui::Unindent();
	}

	void DrawColorLut(AssetHandle handle)
	{
		if (!handle.IsValid())
			return;

		const std::filesystem::path file = Assets::Registry::GetAbsolutePath(handle);
		const std::string& path = Assets::Registry::GetMetadata(handle).Path;

		ImGui::Indent();

		Assets::LutRecipe* recipe = Assets::Manager::GetLutRecipe(handle);

		if (!recipe)
		{
			// A `.cube`. Said plainly rather than shown as a row of greyed-out
			// knobs, because the knobs do not exist: the transform that made
			// this table left when the table was baked.
			UI::TextCaption("%s -- a baked table, imported",
							path.empty() ? "This LUT" : path.c_str());
			UI::TextCaption("Its entries are data. The grade that produced them is not "
							"in the file, so there is nothing here to edit.\n"
							"New colour LUT... makes a .rvlut, which is a look you "
							"can author.");
			ImGui::Unindent();
			return;
		}

		UI::TextCaption("Editing %s -- shared by every profile using it",
						path.empty() ? "this LUT" : path.c_str());

		// Edited in place and written through, exactly as a post profile is,
		// and for the same reason: the frame graph reads the baked texture
		// when it builds, so writing through is what makes the preview live.
		if (DrawFields(LutRecipeRegistry::Fields(), recipe))
		{
			if (!file.empty())
				Assets::LutRecipeSerializer::Save(*recipe, file);

			// The texture is stale the instant a knob moves. Dropped rather
			// than rebaked here: the next frame that wants it rebakes, which
			// keeps one path from the file to the sampler.
			Assets::Manager::ReloadColorLut(handle);
		}

		ImGui::Spacing();

		// The way out. A recipe that could not leave the engine would make
		// this a worse place to author a look than the tool it replaced.
		if (ImGui::Button("Export .cube..."))
		{
			const std::string target = FileDialogs::SaveFile("Cube LUT (*.cube)\0*.cube\0");
			if (!target.empty())
			{
				if (Assets::ExportCube(Assets::BakeRecipe(*recipe), target))
					RV_INFO("Exported {0}", target);
				else
					RV_ERROR("Could not export {0}", target);
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Bakes the table these knobs describe and writes it as a\n"
							  ".cube, which Resolve, Photoshop and every other grading\n"
							  "tool reads.");
		}

		ImGui::Unindent();
	}

	bool DrawFields(const std::vector<FieldDesc>& fields, void* block)
	{
		bool changed = false;

		for (const FieldDesc& field : fields)
		{
			// The same rule the entity inspector follows: a hidden field is
			// hidden, never dropped. Nothing here writes to disk, so hiding a
			// row cannot lose a value -- and showing MSAA's sample count while
			// FXAA is selected is a control that does nothing, which is worse
			// than an absent one.
			if (field.Hint.VisibleIf && !field.Hint.VisibleIf(block))
				continue;

			changed |= DrawField(field, block);
		}

		return changed;
	}
}
