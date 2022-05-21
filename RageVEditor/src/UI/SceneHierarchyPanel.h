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

		void OnImGuiRender();

	private:
		std::shared_ptr<Scene> m_SceneRef;
		Entity m_Selected	;
	};

}