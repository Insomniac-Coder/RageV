#pragma once
#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"

// Declared in the enclosing namespace on purpose. Inside
// `namespace RageV::Physics` these would declare new types that nothing
// ever defines, and the error would surface far from here.
namespace RageV
{
	class Scene;
}

namespace RageV::Physics
{

	// How each kind of body is coloured.
	//
	// Deliberately outside the editor's red-on-black palette. That palette is a
	// rule about UI chrome -- red means "you can act on this" -- and a collider
	// is neither chrome nor actionable, it is data drawn over the scene. Green
	// for colliders is also what Unity, Godot and Unreal all use, and matching
	// three engines someone may already know beats internal consistency here.
	struct DebugStyle
	{
		Vec4 Static{ 0.32f, 0.72f, 0.44f, 0.70f };
		Vec4 Kinematic{ 0.35f, 0.72f, 0.92f, 0.85f };
		Vec4 Dynamic{ 0.45f, 0.95f, 0.50f, 0.90f };

		// Amber, because a trigger is the one collider that does not stop
		// anything and reading it as solid is the mistake worth preventing.
		Vec4 Trigger{ 0.98f, 0.76f, 0.24f, 0.90f };

		// The selected entity, whatever kind it is.
		Vec4 Selected{ 1.00f, 1.00f, 1.00f, 0.95f };

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
	void DrawColliders(Scene& scene, UUID selected = UUID::Invalid(),
							  const DebugStyle& style = {});
}
