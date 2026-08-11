#include "ContentBrowserPanel.h"
#include "EditorTheme.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace RageV
{
	namespace
	{
		// A glyph per type. Real thumbnails need a render-to-texture pass per
		// asset and a cache to hold them, which is worth doing once there is
		// something more visually distinguishable than four file types.
		const char* TypeGlyph(AssetType type)
		{
			switch (type)
			{
				case AssetType::Mesh:     return "[M]";
				case AssetType::Texture:  return "[T]";
				case AssetType::Material: return "[m]";
				case AssetType::Prefab:   return "[P]";
				case AssetType::Scene:    return "[S]";
				case AssetType::Audio:    return "[A]";
				default:                  return " ? ";
			}
		}

		// Below this many columns the grid stops reading as a grid, so cells
		// shrink instead. Below kMinCell they stop being clickable.
		constexpr int kMinColumns = 3;
		constexpr float kMinCell = 34.0f;

		ImVec4 TypeColor(AssetType type)
		{
			// Only the two types that can be dragged somewhere useful are
			// coloured. Everything else stays grey, so the accent still means
			// "you can act on this" rather than decorating the panel.
			switch (type)
			{
				case AssetType::Mesh:
				case AssetType::Prefab:
					return EditorTheme::Colors().Accent;
				default:
					return ImVec4(0.55f, 0.55f, 0.58f, 1.0f);
			}
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
		int columns = std::max(1, (int)(width / cellSize));

		if (columns < kMinColumns && width > 0.0f)
		{
			columns = std::max(1, std::min(kMinColumns, (int)(width / (kMinCell + m_Padding))));
			cellSize = std::max(kMinCell, width / (float)columns - m_Padding);
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

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text,
							  isDirectory ? ImVec4(0.85f, 0.85f, 0.88f, 1.0f) : TypeColor(type));

		ImGui::Button(isDirectory ? "[/]" : TypeGlyph(type), ImVec2(cellSize, cellSize));

		ImGui::PopStyleColor(2);

		// --- drag source ----------------------------------------------------
		// Only assets the registry knows: dragging a file with no handle would
		// drop something that resolves to nothing.
		if (handle.IsValid() && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("RAGEV_ASSET", &handle, sizeof(handle));
			ImGui::Text("%s %s", TypeGlyph(type), filename.c_str());
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
				ImGui::SetTooltip("%s\n%s\n\nDrag onto a field in the Inspector, or double-click.",
								  filename.c_str(), AssetTypeName(type));

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

		ImGui::TextUnformatted(label.c_str());
		if (ImGui::IsItemHovered() && label != filename)
			ImGui::SetTooltip("%s", filename.c_str());

		ImGui::PopID();
	}
}
