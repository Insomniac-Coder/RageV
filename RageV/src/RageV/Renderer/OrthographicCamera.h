#pragma once
#include "Camera.h"
#include "glm/gtc/matrix_transform.hpp"

namespace RageV
{

	class OrthographicCamera : public Camera
	{
	public:
		OrthographicCamera(float left, float right, float bottom, float top);

		virtual const glm::mat4& GetViewMatrix() const override { return m_ViewMatrix; }
		virtual const glm::mat4& GetProjectionMatrix() const override { return m_ProjectionMatrix; };
		virtual const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProjectionMatrix; };

		void SetProjectionMatrix(float left, float right, float bottom, float top);

		virtual const glm::vec3& GetPosition() const { return m_Position; }
		virtual const float& GetRotation() const { return m_Rotation; }

		virtual void SetRotation(const float& rotation) { m_Rotation = rotation; RecalculateViewMatrix(); }
		virtual void SetPosition(const glm::vec3& position) override { m_Position = position; RecalculateViewMatrix(); }

	private:
		virtual void RecalculateViewMatrix() override;

		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ViewProjectionMatrix;
		glm::vec3 m_Position = {0.0f, 0.0f, 0.0f};
		float m_Rotation = 0.0f;
		ProjectionType m_ProjectionType;
	};

}