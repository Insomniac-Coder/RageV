#pragma once
#include "RageV/Renderer/Cameranew.h"

namespace RageV
{
	class SceneCamera : public Cameranew
	{
	public:
		enum class ProjectionType
		{
			Perspective = 0,
			Orthographic = 1
		};

		SceneCamera() = default;
		virtual ~SceneCamera() {}

		float GetOrthographicSize() { return m_Size; }
		void SetOrthgraphicSize(float size);
		void SetViewport(float width, float height);

		void SetOrthoNearClip(float value) { m_OrthoNear = value; Recalculate(); }
		void SetOrthoFarClip(float value) { m_OrthoFar = value; Recalculate(); }
		float GetOrthoNearClip() { return m_OrthoNear; }
		float GetOrthoFarClip() { return m_OrthoFar; }

		void SetPerspectiveNearClip(float value) { m_PerspectiveNear = value; Recalculate(); }
		void SetPerspectiveFarClip(float value) { m_PerspectiveFar = value; Recalculate(); }
		void SetPerspectiveFOV(float value) { m_PerspectiveFOV = value; }
		float GetPerspectiveNearClip() { return m_PerspectiveNear; }
		float GetPerspectiveFarClip() { return m_PerspectiveFar; }
		float GetPerspectiveFOV() { return m_PerspectiveFOV; }

		ProjectionType GetProjectionType() const { return m_ProjectionType; }
		void SetProjectionType(ProjectionType type) { m_ProjectionType = type; }
	private:
		void Recalculate();
	private:
		float m_Size = 10.0f;
		float m_OrthoNear = -1.0f, m_OrthoFar = 1.0f;
		float m_PerspectiveFOV = 60.0f;
		float m_PerspectiveNear = 0.01f, m_PerspectiveFar = 1000.0f;
		float m_AspectRatio = 0.0f;
		ProjectionType m_ProjectionType = ProjectionType::Orthographic;
	};
}