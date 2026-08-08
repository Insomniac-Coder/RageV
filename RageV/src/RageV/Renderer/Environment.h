#pragma once
#include "glm/glm.hpp"

namespace RageV
{
	// Scene-wide lighting that is not attached to any entity.
	//
	// The PBR shader's ambient term used to be the literal constants 0.06 and
	// 0.02, unreachable from the editor. It is still an approximation of
	// image-based lighting -- one colour cannot vary with view angle or
	// roughness the way a real environment does -- but it is a value you can
	// set now, and it is what IBL will fall back to for scenes with no
	// environment map.
	//
	// It lives beside the renderer rather than in Scene so that Renderer3D does
	// not have to include the scene layer to be handed one.
	struct SceneEnvironment
	{
		glm::vec3 AmbientColor{ 0.42f, 0.47f, 0.58f };   // cool, sky-ish
		float AmbientIntensity = 0.12f;
	};
}
