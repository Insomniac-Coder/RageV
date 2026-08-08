#pragma once
#include "RageV.h"

namespace RageV
{

	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const std::shared_ptr<Scene>& sceneref);
		void SetSceneRef(const std::shared_ptr<Scene>& sceneref);
		Entity GetSelectedEntity() const { return m_Selected; }
		void SetSelectedEntity(Entity entity) { m_Selected = entity; }
		// Both panels are drawn here because they share the selection; the
		// Window menu owns their visibility.
		void OnImGuiRender(bool* showHierarchy, bool* showProperties);

		void ShowProperties(Entity entity);

	private:
		std::shared_ptr<Scene> m_SceneRef;
		Entity m_Selected;
	};

}