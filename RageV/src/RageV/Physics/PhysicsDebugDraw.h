#pragma once
#include "RageV/Core/UUID.h"
#include <glm/glm.hpp>

namespace RageV
{
	class Scene;

	// How each kind of body is coloured.
	//
	// Deliberately outside the editor's red-on-black palette. That palette is a
	// rule about UI chrome -- red means "you can act on this" -- and a collider
	// is neither chrome nor actionable, it is data drawn over the scene. Green
	// for colliders is also what Unity, Godot and Unreal all use, and matching
	// three engines someone may already know beats internal consistency here.
	struct PhysicsDebugStyle
	{
		glm::vec4 Static{ 0.32f, 0.72f, 0.44f, 0.70f };
		glm::vec4 Kinematic{ 0.35f, 0.72f, 0.92f, 0.85f };
		glm::vec4 Dynamic{ 0.45f, 0.95f, 0.50f, 0.90f };

		// Amber, because a trigger is the one collider that does not stop
		// anything and reading it as solid is the mistake worth preventing.
		glm::vec4 Trigger{ 0.98f, 0.76f, 0.24f, 0.90f };

		// The selected entity, whatever kind it is.
		glm::vec4 Selected{ 1.00f, 1.00f, 1.00f, 0.95f };

		// Bodies the simulation has put to sleep are drawn dimmer. Sleep is
		// otherwise invisible, and it is what makes contacts get withdrawn --
		// see ENGINE-NOTES section 3a.
		bool DimSleeping = true;
		float SleepingDim = 0.45f;
	};

	// One wireframe per collider in the scene.
	//
	// Call between DebugRenderer::BeginScene and EndScene. Works in edit mode
	// too, where there is no simulation: the shapes come from the components,
	// which is the whole reason a collider can be set up before Play is
	// pressed and be seen to be right.
	void DrawPhysicsColliders(Scene& scene, UUID selected = UUID::Invalid(),
							  const PhysicsDebugStyle& style = {});
}
