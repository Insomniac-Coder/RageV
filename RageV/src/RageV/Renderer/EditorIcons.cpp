#include <rvpch.h>
#include "EditorIcons.h"
#include "RageV/Core/Log.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/TextureLoader.h"
#include "RageV/Renderer/UIRenderer.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Scene.h"

namespace RageV
{
	namespace
	{
		// Beside the shaders, and loaded the same way: engine assets, not
		// project content, because the editor's own marks are not something a
		// game ships or a project can be missing.
		constexpr const char* kAtlasPath = "assets/icons/gizmos.png";

		// Not cached here on purpose. TextureLoader already caches by path, so
		// this is a map lookup a frame; keeping a second reference would mean
		// the atlas outlives Assets::Manager::ClearCache and a project switch
		// would leave a texture nothing can free. The flag is only so a
		// missing file says so once instead of sixty times a second.
		RHI::Ref<RHI::RHITexture> Atlas()
		{
			static bool warned = false;

			RHI::Ref<RHI::RHITexture> atlas =
				TextureLoader::Load2D(Renderer::GetDevice(), kAtlasPath,
									  /*srgb*/ true, /*generateMips*/ true);

			if (!atlas && !warned)
			{
				RV_CORE_WARN("Editor icons: '{0}' is missing; lights, cameras, "
							 "probes and audio sources will have no viewport mark",
							 kAtlasPath);
				warned = true;
			}

			return atlas;
		}

		UIRect UvFor(EditorIconKind kind)
		{
			// A horizontal strip, one cell per kind, in the enum's order.
			const float width = 1.0f / (float)EditorIconKind::Count;
			return { (float)kind * width, 0.0f, width, 1.0f };
		}
	}

	float EditorIcons::Radius(const Vec3& position, const Vec3& cameraPosition, float scale)
	{
		// Clamped away from zero so a mark the camera is sitting inside stays
		// a quad with an area rather than a degenerate one.
		const float distance = Math::Max(Math::Length(position - cameraPosition), 0.01f);
		return kAngularRadius * scale * distance;
	}

	std::vector<EditorIcon> EditorIcons::Collect(Scene& scene)
	{
		std::vector<EditorIcon> icons;
		ECS::Registry& registry = scene.GetRegistry();

		auto view = registry.GetView<TransformComponent>();
		for (auto handle : view)
		{
			EditorIconKind kind;

			// First match wins; see the enum's note.
			if (registry.AllOf<LightComponent>(handle))
				kind = EditorIconKind::Light;
			else if (registry.AllOf<CameraComponent>(handle))
				kind = EditorIconKind::Camera;
			else if (registry.AllOf<ReflectionProbeComponent>(handle))
				kind = EditorIconKind::Probe;
			else if (registry.AllOf<AudioSourceComponent>(handle))
				kind = EditorIconKind::Audio;
			else
				continue;

			EditorIcon icon;
			icon.Entity = Entity{ handle, &scene }.GetUUID();
			icon.Kind = kind;
			icon.Position = Vec3(view.Get<TransformComponent>(handle).World[3]);

			icons.push_back(icon);
		}

		return icons;
	}

	void EditorIcons::Draw(Scene& scene, const Mat4& viewProjection,
						   const Mat4& cameraTransform, const EditorIconSettings& settings)
	{
		if (!UIRenderer::IsReady() || !Renderer::HasDevice())
			return;

		const std::vector<EditorIcon> icons = Collect(scene);
		if (icons.empty())
			return;

		RHI::Ref<RHI::RHITexture> atlas = Atlas();
		if (!atlas)
			return;

		// The camera's own axes, which is the whole of the billboarding: a
		// quad spanned by them faces the viewer by construction, with no
		// per-mark rotation to build and nothing to get wrong at the poles.
		const Vec3 cameraPosition = Vec3(cameraTransform[3]);
		const Vec3 right = Math::Normalize(Vec3(cameraTransform[0]));
		const Vec3 up = Math::Normalize(Vec3(cameraTransform[1]));

		UIRenderer::BeginWorld(viewProjection);

		for (const EditorIcon& icon : icons)
		{
			const float radius = Radius(icon.Position, cameraPosition, settings.Scale);
			const Vec4 tint = icon.Entity == settings.Selected ? settings.SelectedTint
															  : settings.Tint;

			UIRenderer::DrawWorldSprite(icon.Position, right * radius, up * radius,
										atlas, tint, UvFor(icon.Kind));
		}

		UIRenderer::End();
	}
}
