// Scripts that ship with the engine.
//
// Two reasons they exist rather than being sample code in the editor project.
// First, pressing Play has to do something observable out of the box, or the
// whole play/stop loop is unverifiable by eye. Second, they are the worked
// examples of the script API -- what a script is allowed to reach, and what
// reads well.

#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"
#include <glm/glm.hpp>

namespace RageV
{
	// Rotates at a constant rate. The smallest thing that proves a scene is
	// being stepped and that Stop puts it back.
	class Spinner : public ScriptableEntity
	{
	public:
		void OnUpdate(Timestep dt) override
		{
			// Radians per second, applied per fixed step. Multiplying by dt is
			// what keeps the rate the same at any simulation frequency.
			Rotate({ 0.0f, m_Speed * dt.GetSeconds(), 0.0f });
		}

	private:
		float m_Speed = 1.2f;
	};

	// Moves with the movement axes, relative to its own orientation.
	//
	// Reads through actions rather than keycodes, so it works with whatever the
	// input map is bound to and keeps working when the player rebinds.
	class Mover : public ScriptableEntity
	{
	public:
		void OnUpdate(Timestep dt) override
		{
			const glm::vec3 direction =
				GetForward() * GetAxis("MoveForward") +
				GetRight()   * GetAxis("MoveRight") +
				glm::vec3(0.0f, 1.0f, 0.0f) * GetAxis("MoveUp");

			// Normalising a zero vector produces NaNs, which then spread
			// through every transform derived from this one.
			if (glm::dot(direction, direction) > 0.0f)
			{
				const float speed = IsActionDown("Sprint") ? m_Speed * 3.0f : m_Speed;
				Translate(glm::normalize(direction) * speed * dt.GetSeconds());
			}
		}

	private:
		float m_Speed = 4.0f;
	};

	// Follows another entity by name, at an offset. Demonstrates reaching
	// outside the entity a script is attached to.
	class Follow : public ScriptableEntity
	{
	public:
		void OnCreate() override
		{
			m_Target = FindEntityByName(m_TargetName);
			if (!m_Target)
				RV_CORE_WARN("Follow: no entity named '{0}'", m_TargetName);
		}

		void OnUpdate(Timestep dt) override
		{
			if (!m_Target)
				return;

			const glm::vec3 goal =
				m_Target.GetComponent<TransformComponent>().Position + m_Offset;

			// Framerate-independent smoothing: a plain lerp by a constant
			// factor converges at a rate that depends on the step size.
			const float t = 1.0f - std::exp(-m_Sharpness * dt.GetSeconds());
			GetPosition() += (goal - GetPosition()) * t;
		}

	private:
		std::string m_TargetName = "Player";
		glm::vec3 m_Offset{ 0.0f, 3.0f, 8.0f };
		float m_Sharpness = 4.0f;
		Entity m_Target;
	};
	// Inside the namespace, so the registered names are "Spinner" and not
	// "RageV::Spinner" -- those strings end up in scene files.
	RV_REGISTER_SCRIPT(Spinner);
	RV_REGISTER_SCRIPT(Mover);
	RV_REGISTER_SCRIPT(Follow);
}
