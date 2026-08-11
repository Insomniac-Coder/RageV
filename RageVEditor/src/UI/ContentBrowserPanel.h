#pragma once
#include "RageV.h"
#include "RageV/Asset/AssetRegistry.h"
#include <filesystem>
#include <functional>

namespace RageV
{
	// Browses the project's assets and is the source of every asset drag.
	//
	// The inspector's asset fields have been drop targets since they existed;
	// this is what finally makes them reachable. A field you can only set from
	// code is a field nobody sets.
	class ContentBrowserPanel
	{
	public:
		ContentBrowserPanel() = default;

		// Called when an asset is activated -- double-clicked, or dropped into
		// the viewport. The editor owns what "open this" means for each type.
		void SetActivateCallback(std::function<void(AssetHandle, AssetType)> callback)
		{
			m_OnActivate = std::move(callback);
		}

		void OnImGuiRender(bool* open);

		// Which folder is open, so it can be remembered between sessions.
		// Stored relative to the asset root: an absolute path breaks the moment
		// the project moves, and the folder that was open is a property of the
		// project rather than of the machine.
		std::string GetCurrentFolder() const;
		void SetCurrentFolder(const std::string& relative);

	private:
		void DrawBreadcrumbs();
		void DrawEntry(const std::filesystem::directory_entry& entry, float cellSize);

	private:
		// Empty until the first draw, when it is set to the registry root.
		// Assets::Registry::Init runs after the panel is constructed.
		std::filesystem::path m_Current;
		std::function<void(AssetHandle, AssetType)> m_OnActivate;

		float m_Thumbnail = 72.0f;
		float m_Padding = 14.0f;
	};
}
