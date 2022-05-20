#pragma once
#include "RageV/Renderer/Cameranew.h"

namespace RageV
{
	class SceneCamera : public Cameranew
	{
	public:
		SceneCamera() = default;
		virtual ~SceneCamera() {}

		void SetOrthgraphicSize(float size);
		void SetViewport(float width, float height);
	private:
		void Recalculate();
	private:
		unsigned int m_Size = 10.0f;
		float m_Near = -1.0f, m_Far = 1.0f;
		float m_AspectRatio = 0.0f;
	};
}