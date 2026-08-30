#include <rvpch.h>
#include "ProjectTemplate.h"
#include "Project.h"

#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Core/Log.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/SceneSerializer.h"

namespace RageV
{
	namespace
	{
		// What a new project opens on.
		//
		// Deliberately not an empty scene. An engine that opens on nothing
		// makes the first five minutes an exercise in finding out which of the
		// six things you need is missing -- there is no light, so everything
		// is black, and that reads as a broken install rather than an empty
		// scene. A ground plane, a light and two objects means Play does
		// something immediately and every part of the pipeline has proved
		// itself before the user has touched anything.
		void PopulateStarterScene(Scene& scene)
		{
			// A scene with no camera renders nothing, so seed one.
			Entity camera = scene.CreateEntity("Scene Camera");
			auto& cameraComponent = camera.AddComponent<CameraComponent>();
			cameraComponent.Camera.Projection = SceneCamera::ProjectionType::Perspective;

			auto& cameraTransform = camera.GetComponent<TransformComponent>();
			cameraTransform.Position = { 0.0f, 2.0f, 6.0f };
			cameraTransform.Rotation = Math::Radians(Vec3(-12.0f, 0.0f, 0.0f));

			const auto place = [&](PrimitiveType primitive, const char* name,
								   const Vec3& position, const Vec3& scaling,
								   const Vec4& colour, float metallic, float roughness)
			{
				Entity entity = scene.CreateEntity(name);
				auto& mesh = entity.AddComponent<MeshComponent>(primitive);

				mesh.OverrideBaseColor = true;
				mesh.BaseColor = colour;
				mesh.OverrideMetallic = true;
				mesh.Metallic = metallic;
				mesh.OverrideRoughness = true;
				mesh.Roughness = roughness;

				auto& transform = entity.GetComponent<TransformComponent>();
				transform.Position = position;
				transform.Scale = scaling;
				return entity;
			};

			place(PrimitiveType::Plane, "Ground", { 0.0f, 0.0f, 0.0f }, { 12.0f, 1.0f, 12.0f },
				  { 0.16f, 0.16f, 0.18f, 1.0f }, 0.0f, 0.9f);
			place(PrimitiveType::Cube, "Cube", { -1.2f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f },
				  { 0.78f, 0.22f, 0.22f, 1.0f }, 0.0f, 0.45f);
			place(PrimitiveType::Sphere, "Sphere", { 1.2f, 0.6f, 0.0f }, { 1.2f, 1.2f, 1.2f },
				  { 0.85f, 0.85f, 0.88f, 1.0f }, 1.0f, 0.2f);

			// A directional light, angled rather than straight down: a light
			// pointing along an axis produces flat shading and a shadow
			// directly underneath, which makes it hard to tell whether shadows
			// work at all.
			Entity sun = scene.CreateEntity("Sun");
			auto& light = sun.AddComponent<LightComponent>();
			light.Light.Type = Light::LightType::Directional;
			light.Light.Intensity = 3.0f;
			sun.GetComponent<TransformComponent>().Rotation =
				Math::Radians(Vec3(-55.0f, -35.0f, 0.0f));
		}
	}

	bool ProjectTemplate::Create(const std::filesystem::path& directory,
								 const std::string& name,
								 Boot::Progress& progress,
								 std::filesystem::path& sceneOut)
	{
		sceneOut.clear();

		if (name.empty())
		{
			RV_CORE_ERROR("A project needs a name");
			return false;
		}

		// The weights are the same kind of informed guess the loading screen's
		// are: writing the files is instant, re-rooting the registry is not,
		// and cooking the first scene is most of it.
		progress.BeginPhase("Creating " + name, 0.0f, 0.2f);
		progress.SetDetail(directory.string());

		if (!Project::Create(directory, name))
			return false;

		progress.Advance(1.0f);

		progress.BeginPhase("Creating project files", 0.2f, 0.45f);

		// The folders a project is expected to have. Created here rather than
		// left to whatever first writes into one, so the content browser opens
		// on a shape somebody recognises instead of on a single `scenes`
		// folder that appeared because a scene was saved.
		std::error_code error;
		for (const char* folder : { "scenes", "materials", "models", "textures",
									"audio", "fonts", "post" })
		{
			std::filesystem::create_directories(Project::AssetRoot() / folder, error);
			progress.SetDetail(std::string("assets/") + folder);
		}

		progress.Advance(1.0f);

		// **The registry is re-rooted before anything is written.** A scene
		// serialized while the registry still points at the previous project
		// mints handles that mean nothing here, which is a project that opens
		// to a scene full of missing assets and no explanation.
		progress.BeginPhase("Setting up assets", 0.45f, 0.75f);
		progress.SetDetail(Project::AssetRoot().string());

		Assets::Manager::ClearCache();
		Assets::Registry::Init(Project::AssetRoot());

		progress.Advance(1.0f);

		progress.BeginPhase("Initializing project", 0.75f, 1.0f);
		progress.SetDetail(kFirstScene);

		auto scene = std::make_shared<Scene>();
		PopulateStarterScene(*scene);

		const std::filesystem::path scenePath = Project::AssetPath(kFirstScene);
		std::filesystem::create_directories(scenePath.parent_path(), error);

		SceneSerializer serializer(scene);
		if (!serializer.Serialize(scenePath.string()))
		{
			RV_CORE_ERROR("Created the project but could not write its first scene");
			return false;
		}

		Project::Config().StartScene = kFirstScene;
		if (!Project::Save())
		{
			RV_CORE_ERROR("Created the project but could not write {0}", name);
			return false;
		}

		progress.Advance(1.0f);

		sceneOut = scenePath;
		RV_CORE_INFO("Created project '{0}' at {1}", name, directory.string());
		return true;
	}
}
