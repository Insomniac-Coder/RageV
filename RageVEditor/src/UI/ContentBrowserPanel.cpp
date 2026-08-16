#include "ContentBrowserPanel.h"
#include "EditorTheme.h"
#include "AssetIcons.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace RageV
{
	namespace
	{
		// Icons are vectors now, in UI/AssetIcons. Real *thumbnails* -- a
		// render of the actual mesh or the actual texture -- still need a
		// render-to-texture pass per asset and a cache to hold them; the icons
		// say what kind of thing it is, which is the question a browser is
		// asked far more often.

		// Below this many columns the grid stops reading as a grid, so cells
		// shrink instead. Below kMinCell they stop being clickable.
		constexpr int kMinColumns = 3;
		constexpr float kMinCell = 34.0f;

		// Icons are one colour, and it says state rather than type.
		//
		// The version this replaces tinted meshes and prefabs with the accent
		// because those are the two that can be dragged somewhere useful. That
		// was already stretching the rule -- red means "you can act on this" --
		// and it does not survive having real icons, where shape carries the
		// type and colouring by type would leave the panel with no way left to
		// show which cell the cursor is on.
		ImU32 IconColor(bool hovered)
		{
			const auto& colors = EditorTheme::Colors();
			return ImGui::GetColorU32(hovered ? colors.Accent : colors.TextSecondary);
		}
	}

	void ContentBrowserPanel::OnImGuiRender(bool* open)
	{
		if (!ImGui::Begin("Content", open))
		{
			ImGui::End();
			return;
		}

		if (!Assets::Registry::IsInitialised())
		{
			ImGui::TextDisabled("No asset registry.");
			ImGui::End();
			return;
		}

		if (m_Current.empty() || !std::filesystem::exists(m_Current))
			m_Current = Assets::Registry::Root();

		DrawBreadcrumbs();
		ImGui::Separator();

		const float width = ImGui::GetContentRegionAvail().x;

		// The cell shrinks to fit rather than the column count collapsing to
		// one. A fixed cell size means a narrow panel shows a single column of
		// enormous icons, which is the worst of both -- and the panel is narrow
		// exactly when space is scarce.
		float cellSize = m_Thumbnail + m_Padding;
		int columns = Math::Max(1, (int)(width / cellSize));

		if (columns < kMinColumns && width > 0.0f)
		{
			columns = Math::Max(1, Math::Min(kMinColumns, (int)(width / (kMinCell + m_Padding))));
			cellSize = Math::Max(kMinCell, width / (float)columns - m_Padding);
		}

		ImGui::BeginChild("##entries");

		if (ImGui::BeginTable("##grid", columns, ImGuiTableFlags_SizingFixedSame))
		{
			std::error_code error;

			// Directories first, then files, each alphabetically -- directory
			// iteration order is whatever the filesystem feels like.
			std::vector<std::filesystem::directory_entry> directories;
			std::vector<std::filesystem::directory_entry> files;

			for (const auto& entry : std::filesystem::directory_iterator(m_Current, error))
			{
				if (error)
					break;
				// Sidecars are the identity of the file beside them, not
				// separate assets, so they are not shown.
				if (entry.path().extension() == ".meta")
					continue;

				(entry.is_directory() ? directories : files).push_back(entry);
			}

			auto byName = [](const auto& a, const auto& b)
			{
				return a.path().filename().string() < b.path().filename().string();
			};
			std::sort(directories.begin(), directories.end(), byName);
			std::sort(files.begin(), files.end(), byName);

			for (const auto& entry : directories)
				DrawEntry(entry, cellSize);
			for (const auto& entry : files)
				DrawEntry(entry, cellSize);

			ImGui::EndTable();
		}

		ImGui::EndChild();
		ImGui::End();
	}

	std::string ContentBrowserPanel::GetCurrentFolder() const
	{
		if (m_Current.empty() || !Assets::Registry::IsInitialised())
			return {};

		std::error_code error;
		const std::filesystem::path relative =
			std::filesystem::relative(m_Current, Assets::Registry::Root(), error);
		if (error || relative.empty() || relative == ".")
			return {};

		return relative.generic_string();
	}

	void ContentBrowserPanel::SetCurrentFolder(const std::string& relative)
	{
		if (relative.empty() || !Assets::Registry::IsInitialised())
			return;

		// Verified rather than trusted: the folder may have been deleted since
		// it was written, and a browser that opens on a path that no longer
		// exists shows an empty grid with no way to tell why.
		const std::filesystem::path candidate = Assets::Registry::Root() / relative;
		std::error_code error;
		if (std::filesystem::is_directory(candidate, error) && !error)
			m_Current = candidate;
	}

	void ContentBrowserPanel::DrawBreadcrumbs()
	{
		const std::filesystem::path& root = Assets::Registry::Root();

		ImGui::BeginDisabled(m_Current == root);
		if (ImGui::Button("<"))
			m_Current = m_Current.parent_path();
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Refresh"))
		{
			// Files arrive from outside the editor -- an exporter writing into
			// the folder, a git pull -- so the registry is rescanned on demand
			// rather than only at startup.
			Assets::Registry::Refresh();
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Rescan the assets folder. New files get a handle; the sidecar\n"
							  "beside each one is what makes that handle survive a rename.");

		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::SliderFloat("##thumb", &m_Thumbnail, 34.0f, 128.0f, "%.0f px");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Icon size. Cells shrink below this on their own when the panel is narrow.");

		ImGui::SameLine();

		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(m_Current, root, error);
		const std::string label = (error || relative == ".")
								? "assets"
								: "assets/" + relative.generic_string();
		ImGui::TextDisabled("%s", label.c_str());
	}

	void ContentBrowserPanel::DrawEntry(const std::filesystem::directory_entry& entry, float cellSize)
	{
		const std::filesystem::path& path = entry.path();
		const std::string filename = path.filename().string();

		ImGui::TableNextColumn();
		ImGui::PushID(filename.c_str());

		const bool isDirectory = entry.is_directory();

		AssetHandle handle = AssetHandle::Invalid();
		AssetType type = AssetType::None;

		if (!isDirectory)
		{
			std::error_code error;
			const std::filesystem::path relative =
				std::filesystem::relative(path, Assets::Registry::Root(), error);
			if (!error)
			{
				handle = Assets::Registry::GetHandle(relative.generic_string());
				type = Assets::Registry::GetMetadata(handle).Type;
			}
		}

		// The button is the hit target and nothing else: transparent at rest so
		// the grid reads as a grid rather than as a wall of tiles, with the
		// hover fill as the only thing that moves.
		const auto& colors = EditorTheme::Colors();
		const bool selected = handle.IsValid() && handle == m_Selected;

		// Transparent at rest so the grid reads as a grid rather than a wall
		// of tiles, with the hover fill as the only thing that moves -- except
		// on the selected cell, which keeps its fill so that a panel scrolled
		// away from the cursor still says which file the inspector is showing.
		ImGui::PushStyleColor(ImGuiCol_Button,
							  selected ? colors.AccentMuted : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.AccentFaint);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.AccentMuted);

		const ImVec2 cellTopLeft = ImGui::GetCursorScreenPos();
		ImGui::Button("##cell", ImVec2(cellSize, cellSize));
		const bool hovered = ImGui::IsItemHovered();

		ImGui::PopStyleColor(3);

		// Drawn after the button so it lands on top of the hover fill, and
		// inset so the glyph is not flush against the cell's edge.
		const float inset = cellSize * 0.16f;
		const ImVec2 iconAt = { cellTopLeft.x + inset, cellTopLeft.y + inset };
		const float iconSize = cellSize - inset * 2.0f;
		ImDrawList* draw = ImGui::GetWindowDrawList();

		// The registry's type when it has one, the extension when it does not:
		// a project folder holds scripts, shaders and a readme as well as the
		// seven things the engine imports, and one glyph between all of them
		// is a browser navigated by reading filenames.
		const UI::IconKind kind = isDirectory ? UI::IconKind::Folder
											  : UI::KindForFile(path, type);
		UI::DrawIcon(draw, iconAt, iconSize, kind, IconColor(hovered));

		// --- drag source ----------------------------------------------------
		// Only assets the registry knows: dragging a file with no handle would
		// drop something that resolves to nothing.
		if (handle.IsValid() && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("RAGEV_ASSET", &handle, sizeof(handle));
			// The same icon at text size, so what is under the cursor while
			// dragging is recognisably the thing that was picked up.
			UI::DrawInlineIcon(UI::KindForFile(path, type),
							   ImGui::GetColorU32(EditorTheme::Colors().TextPrimary));
			ImGui::SameLine();
			ImGui::TextUnformatted(filename.c_str());
			ImGui::EndDragDropSource();
		}

		if (ImGui::IsItemHovered())
		{
			if (isDirectory)
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					m_Current = path;
			}
			else if (handle.IsValid())
			{
				ImGui::SetTooltip("%s\n%s\n\nClick to inspect, double-click to open,\n"
								  "or drag onto a field in the Inspector.",
								  filename.c_str(), AssetTypeName(type));

				// Selection first, so a double click inspects *and* opens
				// rather than opening something the panel never showed.
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					m_Selected = handle;
					if (m_OnSelect)
						m_OnSelect(handle, type);
				}

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_OnActivate)
					m_OnActivate(handle, type);
			}
			else
			{
				ImGui::SetTooltip("%s\n\nNot an asset type the engine imports, so it has no handle.",
								  filename.c_str());
			}
		}

		// Clipped to the cell rather than wrapped: a long filename in a narrow
		// cell wraps into a tower of single characters and pushes every row
		// below it out of alignment.
		const float available = ImGui::GetContentRegionAvail().x;
		std::string label = filename;
		if (ImGui::CalcTextSize(label.c_str()).x > available)
		{
			while (label.size() > 1 && ImGui::CalcTextSize((label + "...").c_str()).x > available)
				label.pop_back();
			label += "...";
		}

		// Centred under the icon rather than left-aligned in the cell. The icon
		// is centred in a cell `cellSize` wide and the label was not, so every
		// short name sat off to one side of the thing it names -- and the
		// shorter the name, the further off it looked.
		const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
		if (const float slack = cellSize - labelWidth; slack > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * 0.5f);

		ImGui::TextUnformatted(label.c_str());
		if (ImGui::IsItemHovered() && label != filename)
			ImGui::SetTooltip("%s", filename.c_str());

		ImGui::PopID();
	}
}
