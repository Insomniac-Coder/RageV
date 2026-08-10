// Scene serialization round-trip test.
//
// The invariant: once a scene has been through one load, saving it again must
// be byte-identical forever after.
//
//     load(fixture) -> save(A) -> load(A) -> save(B)     assert A == B
//
// Comparing the *original* file against A would be wrong -- a hand-written or
// older-version fixture legitimately differs from what the current serializer
// emits. Idempotence after one round trip is the property that actually
// matters, and it is what non-destructive play mode will depend on: play mode
// is snapshot-then-restore, and it is only as correct as this is.
//
// A device is created because MeshComponent's material is a GPU resource; the
// window is hidden since nothing is drawn.
//
//     scenetest [--rhi=vulkan|opengl]
#include <rvpch.h>
#include "RageV/Core/Log.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/SceneSerializer.h"
#include "RageV/Scene/SceneCommands.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Core/FixedStep.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/KeyCodes.h"
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsDebugDraw.h"
#include "RageV/Physics/ColliderShapes.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Renderer/ParticleRenderer.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Renderer/RenderGraph.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Renderer/EditorCamera.h"
#include "RageV/Renderer/Skybox.h"
#include "RageV/Renderer/Cubemap.h"
#include "RageV/Renderer/ReflectionProbe.h"
#include "RageV/Renderer/ShadowMap.h"
#include "RageV/Renderer/Frustum.h"
#include "RageV/Renderer/LightGrid.h"
#include "RageV/Animation/Skeleton.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/EnvironmentIBL.h"
#include "RageV/Renderer/PostProcess.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/TextureLoader.h"
#include "RageV/Scene/ScenePicking.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/GltfImporter.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Project/Project.h"
#include "RageV/Project/ProjectPackager.h"
#include "RageV/Managed/DotNetHost.h"
#include "RageV/Managed/Interop.h"
#include "RageV/Managed/ScriptBuild.h"
#include "RageV/Project/ModuleBuild.h"
#include "RageV/Project/GameModule.h"
#include "RageV/Core/ChildProcess.h"
#include "RageV/Math/Math.h"
#include "GlmBridge.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include "RageV/Core/EngineConfig.h"

// No <GLFW/glfw3.h> here on purpose: this executable must not link its own copy
// of GLFW, or it gets a second set of GLFW's globals that the engine DLL cannot
// see. Windows come from Window::Create.
#include "RageV/Core/Window.h"
#include <fstream>
#include <functional>
#include <set>
#include <unordered_set>

using namespace RageV;
using namespace RageV::RHI;

// Defined below; used by fixtures declared before it.
static std::filesystem::path ScratchDir(const std::string& name);

namespace
{
	int g_Failures = 0;

	void Check(bool condition, const std::string& what)
	{
		if (condition)
		{
			RV_CORE_INFO("  pass  {0}", what);
		}
		else
		{
			RV_CORE_ERROR("  FAIL  {0}", what);
			g_Failures++;
		}
	}

	// Every component type, a three-level hierarchy, and values that are not
	// defaults -- a fixture built from defaults cannot tell "written correctly"
	// apart from "not written at all".
	std::shared_ptr<Scene> BuildFixture()
	{
		auto scene = std::make_shared<Scene>();

		// Non-default, so "written correctly" is distinguishable from "not
		// written at all".
		scene->GetEnvironment().AmbientColor = { 0.31f, 0.18f, 0.44f };
		scene->GetEnvironment().AmbientIntensity = 0.37f;
		scene->GetEnvironment().Sky = SkyType::Cubemap;
		scene->GetEnvironment().SkyHorizon = { 0.11f, 0.22f, 0.33f };
		scene->GetEnvironment().SkyZenith = { 0.44f, 0.55f, 0.66f };
		scene->GetEnvironment().SkyGround = { 0.77f, 0.12f, 0.09f };
		scene->GetEnvironment().SkyIntensity = 1.75f;
		scene->GetEnvironment().SkyRotation = 0.9f;
		scene->GetEnvironment().SkyTexture = UUID(0x5ceec0de1234ull);

		Entity camera = scene->CreateEntity("Main Camera");
		auto& cameraComponent = camera.AddComponent<CameraComponent>();
		cameraComponent.Camera.Projection = SceneCamera::ProjectionType::Perspective;
		cameraComponent.Camera.PerspectiveFOV = 53.5f;
		cameraComponent.Camera.PerspectiveNear = 0.05f;
		cameraComponent.Camera.PerspectiveFar = 750.0f;
		cameraComponent.fixedAspectRatio = true;
		camera.GetComponent<TransformComponent>().Position = { 1.5f, 2.5f, 9.0f };

		Entity root = scene->CreateEntity("Root");
		auto& rootTransform = root.GetComponent<TransformComponent>();
		rootTransform.Position = { 2.0f, 0.5f, -1.0f };
		rootTransform.Rotation = { 0.25f, 0.5f, -0.125f };
		rootTransform.Scale = { 1.5f, 1.5f, 1.5f };

		Entity child = scene->CreateEntity("Child");
		child.GetComponent<TransformComponent>().Position = { 0.0f, 2.0f, 0.0f };
		auto& mesh = child.AddComponent<MeshComponent>(PrimitiveType::Sphere);
		if (Renderer::HasDevice())
		{
			mesh.Material = std::make_shared<Material>(Renderer::GetDevice(), "Fixture");
			auto& params = mesh.Material->GetParams();
			params.BaseColor = { 0.85f, 0.17f, 0.19f, 1.0f };
			params.EmissiveColor = { 0.02f, 0.0f, 0.0f, 1.0f };
			params.Metallic = 0.75f;
			params.Roughness = 0.3f;
			params.Occlusion = 0.9f;
			mesh.Material->Invalidate();
		}
		scene->SetParent(child, root);

		Entity grandchild = scene->CreateEntity("Grandchild");
		grandchild.AddComponent<ColorComponent>(Vec4(0.2f, 0.4f, 0.9f, 0.75f));
		scene->SetParent(grandchild, child);

		Entity spot = scene->CreateEntity("Spot Light");
		auto& light = spot.AddComponent<LightComponent>().Light;
		light.Type = Light::LightType::Spot;
		light.Color = { 0.9f, 0.75f, 0.4f };
		light.Intensity = 85.0f;
		light.Range = 22.5f;
		light.InnerCone = 18.0f;
		light.OuterCone = 34.0f;
		// False, which is not the default: a field that happens to match its
		// default cannot tell a working round trip from a missing one.
		light.CastShadows = false;
		spot.GetComponent<TransformComponent>().Position = { -3.0f, 4.0f, 2.0f };

		return scene;
	}

	void CheckHierarchyPreserved(const std::shared_ptr<Scene>& scene)
	{
		Entity root, child, grandchild;
		auto view = scene->GetRegistry().view<TagComponent>();
		for (auto handle : view)
		{
			Entity entity{ handle, scene.get() };
			const std::string& name = entity.GetName();
			if (name == "Root")            root = entity;
			else if (name == "Child")      child = entity;
			else if (name == "Grandchild") grandchild = entity;
		}

		Check(root && child && grandchild, "all three hierarchy entities survived the round trip");
		if (!root || !child || !grandchild)
			return;

		Check(scene->GetParent(child) == root, "Child's parent is Root");
		Check(scene->GetParent(grandchild) == child, "Grandchild's parent is Child");
		Check(!scene->GetParent(root), "Root has no parent");
		Check(scene->IsDescendantOf(grandchild, root), "Grandchild is a descendant of Root");
		Check(scene->GetChildren(root).size() == 1, "Root has exactly one child");

		// The world transform must compose through both levels. A hierarchy
		// that serializes but does not compose is worse than none.
		scene->UpdateWorldTransforms();
		const Mat4 expected = scene->GetWorldTransform(child) *
								   grandchild.GetComponent<TransformComponent>().GetLocalTransform();
		const Mat4 actual = grandchild.GetComponent<TransformComponent>().World;

		bool equal = true;
		for (int column = 0; column < 4; column++)
			for (int row = 0; row < 4; row++)
				equal = equal && std::fabs(expected[column][row] - actual[column][row]) < 1e-4f;

		Check(equal, "Grandchild's world transform composes through two parents");
	}

	void CheckUniqueIDs(const std::shared_ptr<Scene>& scene)
	{
		std::set<uint64_t> seen;
		bool unique = true;
		bool allValid = true;

		auto view = scene->GetRegistry().view<IDComponent>();
		for (auto handle : view)
		{
			const UUID id = view.get<IDComponent>(handle).ID;
			allValid = allValid && id.IsValid();
			unique = unique && seen.insert((uint64_t)id).second;
		}

		Check(allValid, "every entity has a non-zero UUID");
		Check(unique, "every entity UUID is distinct");
	}

	// The fixture is a cube with per-face normals under a two-level node tree,
	// one material with non-default values, and a rotation stored as a
	// quaternion -- the shapes a real exporter emits.
	void CheckGltfImport()
	{
		const std::filesystem::path path = Project::AssetPath("models/testcube.gltf");

		Assets::ImportedModel model;
		if (!Assets::GltfImporter::Import(path, model))
		{
			Check(false, "testcube.gltf imports");
			return;
		}

		Check(true, "testcube.gltf imports");
		Check(model.Primitives.size() == 1, "one primitive");
		Check(model.Materials.size() == 1, "one material");
		Check(model.Nodes.size() == 2, "two nodes");

		if (model.Primitives.empty() || model.Materials.empty() || model.Nodes.size() < 2)
			return;

		const Assets::ImportedPrimitive& primitive = model.Primitives[0];
		Check(primitive.Vertices.size() == 24, "24 vertices (4 per face, so normals are not shared)");
		Check(primitive.Indices.size() == 36, "36 indices");
		Check(primitive.Material == 0, "the primitive references material 0");

		// A vertex whose normal is zero renders black under every light, which
		// is the single most common symptom of a broken accessor read.
		bool normalsUnit = true;
		bool positionsInRange = true;
		for (const MeshVertex& vertex : primitive.Vertices)
		{
			normalsUnit = normalsUnit && std::fabs(Math::Length(vertex.Normal) - 1.0f) < 1e-3f;
			positionsInRange = positionsInRange &&
							   std::fabs(vertex.Position.x) <= 1.001f &&
							   std::fabs(vertex.Position.y) <= 1.001f &&
							   std::fabs(vertex.Position.z) <= 1.001f;
		}
		Check(normalsUnit, "every normal is unit length");
		Check(positionsInRange, "positions decode to the cube's bounds");

		const MaterialParams& params = model.Materials[0].Params;
		Check(std::fabs(params.BaseColor.r - 0.85f) < 1e-3f &&
			  std::fabs(params.Metallic - 0.25f) < 1e-3f &&
			  std::fabs(params.Roughness - 0.65f) < 1e-3f,
			  "material factors decode");
		Check(model.Materials[0].Name == "TestRed", "material name survives");

		// Parents must precede children, or the scene cannot build the tree in
		// one pass.
		Check(model.Nodes[0].Parent == -1 && model.Nodes[1].Parent == 0,
			  "node parents resolve, parents first");
		Check(model.Nodes[0].Name == "Pivot" && model.Nodes[1].Name == "CubeNode",
			  "node names survive");

		Check(std::fabs(model.Nodes[0].Position.x - 2.0f) < 1e-4f &&
			  std::fabs(model.Nodes[0].Position.z + 1.0f) < 1e-4f,
			  "node translation decodes");

		// The fixture stores a 45-degree turn about Y as a quaternion. glTF is
		// xyzw and glm's constructor is wxyz; swapping them is silent and
		// produces a rotation that merely looks wrong.
		Check(std::fabs(model.Nodes[1].Rotation.y - Math::Radians(45.0f)) < 1e-3f,
			  "quaternion rotation converts to the right euler angles");
		Check(std::fabs(model.Nodes[1].Scale.x - 0.5f) < 1e-4f, "node scale decodes");
	}

	void CheckModelInstantiation()
	{
		const AssetHandle handle = Assets::Registry::GetHandle("models/testcube.gltf");
		Check(handle.IsValid(), "the registry minted a handle for the model");
		if (!handle.IsValid())
			return;

		Check(Assets::Registry::GetMetadata(handle).Type == AssetType::Mesh,
			  "the model is typed as a mesh");

		auto scene = std::make_shared<Scene>();
		Entity root = Assets::Manager::InstantiateModel(*scene, handle);
		Check((bool)root, "the model instantiates into the scene");
		if (!root)
			return;

		// One root, the glTF pivot beneath it, and the cube node beneath that.
		Check(scene->GetChildren(root).size() == 1, "the import has a single root");

		Entity pivot = scene->GetEntityByUUID(scene->GetChildren(root)[0]);
		Check(pivot && pivot.GetName() == "Pivot", "the pivot node is under the root");
		if (!pivot)
			return;

		Check(scene->GetChildren(pivot).size() == 1, "the cube node is under the pivot");
		Entity cube = scene->GetEntityByUUID(scene->GetChildren(pivot)[0]);
		Check(cube && cube.HasComponent<MeshComponent>(), "the cube node carries a mesh");

		if (cube && cube.HasComponent<MeshComponent>())
		{
			auto& mesh = cube.GetComponent<MeshComponent>();
			Check((bool)Assets::Manager::GetMesh(mesh.Mesh), "the mesh handle resolves to GPU geometry");
			Check((bool)mesh.Material, "the imported material is attached");
		}

		// The whole point of handles: a saved scene refers to the model, and
		// reloading resolves it again.
		SceneSerializer serializer(scene);
		const std::string saved = serializer.SerializeToString();

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		reader.DeserializeFromString(saved);

		bool resolved = false;
		for (auto entity : reloaded->GetRegistry().view<MeshComponent>())
			resolved = resolved || Assets::Manager::GetMesh(
				reloaded->GetRegistry().get<MeshComponent>(entity).Mesh) != nullptr;

		Check(resolved, "a saved scene's mesh handle still resolves after a reload");
	}

	void CheckPrimitiveHandles()
	{
		PrimitiveType type = PrimitiveType::Sphere;
		Check(Assets::Manager::IsPrimitive(PrimitiveHandle(PrimitiveType::Cylinder), type) &&
			  type == PrimitiveType::Cylinder,
			  "primitive handles round-trip through the builtin range");

		Check((bool)Assets::Manager::GetMesh(PrimitiveHandle(PrimitiveType::Sphere)),
			  "a primitive handle resolves to a mesh");

		// A random handle must not be mistaken for a builtin.
		Check(!Assets::Manager::IsPrimitive(AssetHandle(), type),
			  "a random handle is not treated as a primitive");
	}

	// A prefab is a scene file holding one tree. The property that matters is
	// that stamping it out twice produces two independent trees -- if the
	// copies shared ids, every reference to one would resolve to both, and the
	// hierarchy links would cross-connect them.
	// Anything that writes an asset does it in a throwaway project.
	//
	// Assets used to be staged beside each tool, so a test writing one left its
	// mess in a build directory nobody looks at. They live in a real project
	// folder now, and a test suite that leaves files in the user's project --
	// which is under version control -- is a test suite people stop running.
	//
	// Restores whatever project was open on destruction, including when a check
	// returns early.
	class ScratchProject
	{
	public:
		explicit ScratchProject(const char* name)
			: m_Previous(Project::File())
		{
			m_Root = ScratchDir(name);

			std::error_code error;
			std::filesystem::remove_all(m_Root, error);

			if (Project::Create(m_Root, name))
				Assets::Registry::Init(Project::AssetRoot());
		}

		~ScratchProject()
		{
			if (!m_Previous.empty() && Project::Load(m_Previous))
				Assets::Registry::Init(Project::AssetRoot());

			std::error_code error;
			std::filesystem::remove_all(m_Root, error);
		}

		ScratchProject(const ScratchProject&) = delete;
		ScratchProject& operator=(const ScratchProject&) = delete;

	private:
		std::filesystem::path m_Previous;
		std::filesystem::path m_Root;
	};

	void CheckPrefabs()
	{
		ScratchProject scratch("prefabtest");

		auto scene = std::make_shared<Scene>();

		Entity root = scene->CreateEntity("Turret");
		root.GetComponent<TransformComponent>().Position = { 3.0f, 1.0f, -2.0f };
		root.AddComponent<MeshComponent>(PrimitiveType::Cylinder);

		Entity barrel = scene->CreateEntity("Barrel");
		barrel.GetComponent<TransformComponent>().Position = { 0.0f, 1.5f, 0.0f };
		barrel.AddComponent<MeshComponent>(PrimitiveType::Cube);
		scene->SetParent(barrel, root);

		Entity light = scene->CreateEntity("Muzzle Light");
		light.AddComponent<LightComponent>().Light.Type = Light::LightType::Spot;
		scene->SetParent(light, barrel);

		const AssetHandle prefab = Assets::Manager::CreatePrefab(*scene, root, "prefabs/turret.rprefab");
		Check(prefab.IsValid(), "a prefab is written and gets a handle");
		if (!prefab.IsValid())
			return;

		Check(Assets::Registry::GetMetadata(prefab).Type == AssetType::Prefab,
			  "the prefab is typed as a prefab");
		Check(root.HasComponent<PrefabComponent>(),
			  "the source tree becomes an instance of the prefab it produced");

		auto target = std::make_shared<Scene>();
		Entity first = Assets::Manager::InstantiatePrefab(*target, prefab);
		Entity second = Assets::Manager::InstantiatePrefab(*target, prefab);

		Check(first && second, "a prefab instantiates twice");
		if (!first || !second)
			return;

		Check(first.GetUUID() != second.GetUUID(), "two instances have different ids");
		Check(first.GetName() == "Turret", "the instance keeps the prefab's root name");
		Check(target->GetRegistry().view<IDComponent>().size() == 6,
			  "three entities per instance, twice");

		// Each copy's hierarchy must point at its own entities. Sharing an id
		// here would silently parent one instance's barrel to the other's.
		Check(scene->GetChildren(root).size() == 1, "the source tree is unchanged");
		Check(target->GetChildren(first).size() == 1, "the first instance has its child");
		Check(target->GetChildren(second).size() == 1, "the second instance has its child");
		Check(target->GetChildren(first)[0] != target->GetChildren(second)[0],
			  "the two instances do not share a child");

		Entity firstBarrel = target->GetEntityByUUID(target->GetChildren(first)[0]);
		Check(firstBarrel && target->GetParent(firstBarrel) == first,
			  "the grandchild chain is rebuilt within the instance");
		Check(firstBarrel && target->GetChildren(firstBarrel).size() == 1,
			  "the third level survives");

		// A prefab's root has no parent inside its own file, so instantiating
		// must not leave it parented to whatever id happened to be recorded.
		Check(!target->GetParent(first), "an instance root has no parent");

		Check(first.HasComponent<PrefabComponent>() &&
			  first.GetComponent<PrefabComponent>().Source == prefab,
			  "an instance records which prefab it came from");

		// Local transforms come across verbatim rather than being re-derived.
		Check(std::fabs(first.GetComponent<TransformComponent>().Position.x - 3.0f) < 1e-4f,
			  "the instance keeps the prefab's transform");
	}

	// The loop that makes physics possible. Every failure mode here is
	// invisible at a glance and only shows up as "the simulation feels wrong on
	// this machine".
	void CheckFixedStep()
	{
		FixedStep step;
		step.Timestep = 1.0f / 60.0f;

		// Exactly one step's worth is one step, and lands back on zero rather
		// than leaving an alpha of 1.0 -- which would render a step that has
		// not been simulated.
		Check(step.Advance(step.Timestep) == 1, "one step's worth of time runs exactly one step");
		Check(step.Alpha < 1.0f && step.Alpha >= 0.0f, "the blend factor stays in [0, 1)");

		// Above the simulation rate, most frames run no steps at all. This is
		// the normal case at 144 Hz, and the whole reason rendering has to
		// interpolate rather than read the simulation directly.
		step.Reset();
		const int fastFrames = step.Advance(1.0f / 240.0f);
		Check(fastFrames == 0, "a frame shorter than the timestep runs no steps");
		Check(step.Alpha > 0.0f, "but it still advances the blend factor");

		// Below it, one frame runs several.
		step.Reset();
		Check(step.Advance(1.0f / 15.0f) == 4, "a slow frame runs several steps");

		// A stall must not queue a burst that can never be worked off.
		step.Reset();
		const int afterStall = step.Advance(10.0f);
		Check(afterStall == (int)(0.25f * 60.0f),
			  "a stall is clamped rather than queueing hundreds of steps");

		// Over a long run, simulated time tracks real time. Drift here would
		// mean the game slowly running fast or slow.
		step.Reset();
		int total = 0;
		constexpr int kFrames = 1000;
		constexpr float kFrameTime = 1.0f / 144.0f;
		for (int i = 0; i < kFrames; i++)
			total += step.Advance(kFrameTime);

		const float simulated = total * step.Timestep;
		const float real = kFrames * kFrameTime;
		Check(std::fabs(simulated - real) < step.Timestep,
			  "simulated time tracks real time over a long run");

		// Reset must not leave time behind, or restoring a minimised window
		// spends the whole gap at once.
		step.Reset();
		Check(step.Accumulator == 0.0f && step.Alpha == 0.0f, "reset discards pending time");
	}

	// Play mode is snapshot, run, restore. The property is that the restore is
	// exact -- not "close", not "mostly" -- because anything less means pressing
	// Play quietly damages the scene.
	void CheckPlayModeRestore()
	{
		auto scene = BuildFixture();
		SceneSerializer serializer(scene);

		const std::string snapshot = serializer.SerializeToString();

		// Everything a running game might do: move things, add and remove
		// components, delete an entity, spawn new ones, change the environment.
		for (auto handle : scene->GetRegistry().view<TransformComponent>())
		{
			auto& transform = scene->GetRegistry().get<TransformComponent>(handle);
			transform.Position += Vec3(11.0f, -4.0f, 7.5f);
			transform.Rotation.y += 1.25f;
		}

		Entity spawned = scene->CreateEntity("Spawned At Runtime");
		spawned.AddComponent<MeshComponent>(PrimitiveType::Sphere);

		for (auto handle : scene->GetRegistry().view<TagComponent>())
		{
			Entity entity{ handle, scene.get() };
			if (entity.GetName() == "Grandchild")
			{
				scene->DeleteEntity(entity);
				break;
			}
		}

		scene->GetEnvironment().AmbientIntensity = 3.0f;

		Check(serializer.SerializeToString() != snapshot, "running the scene changed it");

		// Stop.
		Check(serializer.DeserializeFromString(snapshot), "the snapshot restores");
		Check(serializer.SerializeToString() == snapshot,
			  "stopping restores the scene byte for byte");

		// And the restored scene is a working scene, not just matching bytes:
		// the hierarchy has to be rebuilt, not merely serialized.
		CheckHierarchyPreserved(scene);
	}

	// A script that records what it was given, so the API can be exercised
	// without a window or a keyboard.
	class ProbeScript : public ScriptableEntity
	{
	public:
		inline static int Created = 0;
		inline static int Updated = 0;
		inline static int Destroyed = 0;
		inline static float LastDelta = 0.0f;
		inline static std::string SeenName;
		inline static bool FoundOther = false;
		inline static bool SelfDestruct = false;
		inline static bool SpawnOnCreate = false;

		static void Reset()
		{
			Created = Updated = Destroyed = 0;
			LastDelta = 0.0f;
			SeenName.clear();
			FoundOther = false;
			SelfDestruct = false;
			SpawnOnCreate = false;
		}

		void OnCreate() override
		{
			Created++;
			SeenName = GetName();
			FoundOther = (bool)FindEntityByName("Target");

			if (SpawnOnCreate)
				Spawn("Spawned");
		}

		void OnUpdate(Timestep dt) override
		{
			Updated++;
			LastDelta = dt.GetSeconds();

			// Moving through the API rather than the component, so the helpers
			// are what is under test.
			Translate({ 1.0f, 0.0f, 0.0f });

			if (SelfDestruct)
				Destroy();
		}

		void OnDestroy() override { Destroyed++; }
	};

	void CheckScriptApi()
	{
		// The engine's own scripts live in a translation unit whose only
		// contents are static registrar objects. A linker is free to drop an
		// object file from a static library when nothing references a symbol in
		// it -- and then the registrations never run.
		Check(ScriptRegistry::IsRegistered("Spinner"), "the built-in Spinner script is registered");
		Check(ScriptRegistry::IsRegistered("Mover"), "the built-in Mover script is registered");

		ScriptRegistry::Register("ProbeScript", []() -> ScriptableEntity* { return new ProbeScript(); });
		Check(ScriptRegistry::IsRegistered("ProbeScript"), "a script registers by name");
		Check(ScriptRegistry::Create("NoSuchScript") == nullptr,
			  "an unknown script name yields nothing rather than crashing");

		ProbeScript::Reset();

		auto scene = std::make_shared<Scene>();
		Entity target = scene->CreateEntity("Target");

		Entity actor = scene->CreateEntity("Actor");
		actor.AddComponent<NativeScriptComponent>("ProbeScript");

		// Nothing runs until the scene is stepped -- editing a scene must not
		// run it.
		scene->OnUpdateEditor(1.0f / 60.0f);
		Check(ProbeScript::Created == 0, "scripts do not run in the editor");

		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 1, "OnCreate runs once on the first step");
		Check(ProbeScript::Updated == 1, "OnUpdate runs on the same step as OnCreate");
		Check(std::fabs(ProbeScript::LastDelta - 1.0f / 60.0f) < 1e-6f,
			  "the script is handed the fixed timestep");
		Check(ProbeScript::SeenName == "Actor", "a script can read its own entity");
		Check(ProbeScript::FoundOther, "a script can find another entity by name");

		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 1, "OnCreate does not run again");
		Check(ProbeScript::Updated == 2, "OnUpdate runs every step");

		Check(std::fabs(actor.GetComponent<TransformComponent>().Position.x - 2.0f) < 1e-4f,
			  "transform helpers write through to the component");

		// Destroying the entity has to run OnDestroy exactly once.
		scene->DeleteEntity(actor);
		Check(ProbeScript::Destroyed == 1, "OnDestroy runs when the entity is destroyed");

		// A script that spawns during OnCreate restructures the very pool the
		// script pass is walking.
		ProbeScript::Reset();
		ProbeScript::SpawnOnCreate = true;
		{
			auto spawner = std::make_shared<Scene>();
			Entity entity = spawner->CreateEntity("Spawner");
			entity.AddComponent<NativeScriptComponent>("ProbeScript");

			spawner->OnFixedUpdateRuntime(1.0f / 60.0f);
			Check(spawner->GetRegistry().view<IDComponent>().size() == 2,
				  "a script can spawn an entity mid-step without corrupting the pass");
		}

		// And one that destroys itself while executing inside itself.
		ProbeScript::Reset();
		ProbeScript::SelfDestruct = true;
		{
			auto suicide = std::make_shared<Scene>();
			Entity entity = suicide->CreateEntity("Doomed");
			entity.AddComponent<NativeScriptComponent>("ProbeScript");

			suicide->OnFixedUpdateRuntime(1.0f / 60.0f);
			Check(suicide->GetRegistry().view<IDComponent>().size() == 0,
				  "a script can destroy itself; the delete lands after the pass");
			Check(ProbeScript::Destroyed == 1, "self-destruction still runs OnDestroy once");
		}
		ProbeScript::Reset();

		// The script name is the durable reference, so it has to survive a save.
		{
			auto saved = std::make_shared<Scene>();
			Entity entity = saved->CreateEntity("Scripted");
			entity.AddComponent<NativeScriptComponent>("ProbeScript");

			SceneSerializer serializer(saved);
			const std::string yaml = serializer.SerializeToString();

			auto reloaded = std::make_shared<Scene>();
			SceneSerializer reader(reloaded);
			reader.DeserializeFromString(yaml);

			bool found = false;
			for (auto handle : reloaded->GetRegistry().view<NativeScriptComponent>())
			{
				found = found || reloaded->GetRegistry()
									.get<NativeScriptComponent>(handle).ScriptName == "ProbeScript";
			}
			Check(found, "a script assignment survives save and load");
		}
	}

	// Attaching or swapping a script while the scene is already running.
	void CheckLiveScriptChanges()
	{
		ProbeScript::Reset();
		ScriptRegistry::Register("SecondProbe", []() -> ScriptableEntity* { return new ProbeScript(); });

		auto scene = std::make_shared<Scene>();
		Entity entity = scene->CreateEntity("Live");

		// Already running, with nothing scripted.
		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 0, "a scene with no scripts steps without doing anything");

		// A bare component with no script chosen yet -- what the inspector
		// produces the instant Add Component is clicked.
		auto& component = entity.AddComponent<NativeScriptComponent>();
		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 0, "an unassigned script component does nothing");

		// Then a script is picked from the dropdown, mid-run.
		component.ScriptName = "ProbeScript";
		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 1, "a script attached mid-run starts on the next step");
		Check(ProbeScript::Updated == 1, "and updates on that same step");

		// Swapping the choice has to take effect, not keep running the old one.
		entity.GetComponent<NativeScriptComponent>().ScriptName = "SecondProbe";
		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Destroyed == 1, "swapping the script destroys the old instance");
		Check(ProbeScript::Created == 2, "and creates the new one");

		// Clearing it back to none stops the script and cleans up.
		entity.GetComponent<NativeScriptComponent>().ScriptName.clear();
		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Destroyed == 2, "clearing the choice destroys the instance");

		ProbeScript::Reset();
	}

	// Physics, checked by simulating rather than by asserting on plumbing.
	// "A body was created" says nothing about whether it falls.
	void CheckPhysics()
	{
		auto scene = std::make_shared<Scene>();

		// A floor.
		Entity ground = scene->CreateEntity("Ground");
		ground.GetComponent<TransformComponent>().Position = { 0.0f, -1.0f, 0.0f };
		ground.AddComponent<RigidBodyComponent>(BodyType::Static);
		auto& groundCollider = ground.AddComponent<ColliderComponent>();
		groundCollider.HalfExtents = { 25.0f, 1.0f, 25.0f };

		// A box above it.
		Entity box = scene->CreateEntity("Box");
		box.GetComponent<TransformComponent>().Position = { 0.0f, 8.0f, 0.0f };
		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>();

		// And a static one that must never move.
		Entity pillar = scene->CreateEntity("Pillar");
		pillar.GetComponent<TransformComponent>().Position = { 10.0f, 0.0f, 0.0f };
		pillar.AddComponent<RigidBodyComponent>(BodyType::Static);
		pillar.AddComponent<ColliderComponent>();

		scene->OnRuntimeStart();
		Physics::World* physics = scene->GetPhysics();
		Check(physics != nullptr, "play creates a physics world");
		if (!physics)
			return;

		Check(physics->GetBodyCount() == 3, "one body per entity with a rigid body and a collider");

		const float startY = box.GetComponent<TransformComponent>().Position.y;

		// Half a second of simulation. Gravity alone should move it several
		// units; a body that is in the world but not simulated does not.
		constexpr float dt = 1.0f / 60.0f;
		for (int i = 0; i < 30; i++)
			scene->OnFixedUpdateRuntime(dt);
		scene->OnUpdateRuntime(dt);

		const float afterFalling = box.GetComponent<TransformComponent>().Position.y;
		Check(afterFalling < startY - 0.5f, "a dynamic body falls under gravity");
		Check(pillar.GetComponent<TransformComponent>().Position.x == 10.0f &&
			  pillar.GetComponent<TransformComponent>().Position.y == 0.0f,
			  "a static body does not move");

		// Long enough to land and settle. The interesting property is that it
		// stops on the floor rather than passing through it -- tunnelling is
		// what a degenerate broad phase produces.
		for (int i = 0; i < 300; i++)
			scene->OnFixedUpdateRuntime(dt);
		scene->OnUpdateRuntime(dt);

		const float resting = box.GetComponent<TransformComponent>().Position.y;
		// Ground top is at y = 0, the box is a unit cube with half-extent 0.5.
		Check(resting > -0.2f && resting < 0.8f, "it comes to rest on the floor rather than through it");

		const Vec3 velocity = physics->GetLinearVelocity(box.GetUUID());
		Check(Math::Length(velocity) < 0.5f, "and settles rather than jittering forever");

		// A ray straight down from above the box must hit it.
		const RayHit hit = physics->CastRay({ 0.0f, 20.0f, 0.0f }, { 0.0f, -40.0f, 0.0f });
		Check(hit.Hit, "a ray finds a body");
		Check(hit.Entity == box.GetUUID(), "and reports which entity it belongs to");
		Check(hit.Normal.y > 0.5f, "with a surface normal pointing back at the ray");

		const RayHit miss = physics->CastRay({ 500.0f, 20.0f, 500.0f }, { 0.0f, -40.0f, 0.0f });
		Check(!miss.Hit, "a ray through empty space finds nothing");

		// Runtime control.
		physics->SetLinearVelocity(box.GetUUID(), { 5.0f, 0.0f, 0.0f });
		Check(std::fabs(physics->GetLinearVelocity(box.GetUUID()).x - 5.0f) < 0.01f,
			  "velocity can be set from code");

		// A body removed mid-run must stop being simulated.
		scene->DestroyDeferred(box);
		scene->OnFixedUpdateRuntime(dt);
		Check(physics->GetBodyCount() == 2, "destroying an entity removes its body");

		// An entity that gains its body *during* play -- a spawned prefab, a
		// script-assembled projectile -- must join the simulation. Nothing
		// ever called AddBody until the first real game fired a ball and
		// watched it hang in the air exactly where it appeared.
		Entity spawned = scene->CreateEntity("Spawned");
		spawned.GetComponent<TransformComponent>().Position = { 3.0f, 6.0f, 0.0f };
		spawned.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		spawned.AddComponent<ColliderComponent>();

		for (int i = 0; i < 30; i++)
			scene->OnFixedUpdateRuntime(dt);
		scene->OnUpdateRuntime(dt);

		Check(physics->GetBodyCount() == 3, "an entity spawned during play gains a body");
		Check(spawned.GetComponent<TransformComponent>().Position.y < 5.5f,
			  "and it simulates -- a spawned crate falls");

		scene->OnRuntimeStop();
		Check(scene->GetPhysics() == nullptr, "stop tears the physics world down");
	}

	// Particles, checked by counting rather than by looking. The simulation
	// is deterministic -- xorshift, fixed dt -- so exact numbers are
	// assertable; what they look like is the screenshot's job.
	void CheckParticles()
	{
		auto scene = std::make_shared<Scene>();
		Entity host = scene->CreateEntity("Emitter");
		auto& emitter = host.AddComponent<ParticleEmitterComponent>();
		emitter.Rate = 60.0f;
		emitter.Lifetime = 10.0f;
		emitter.LifetimeJitter = 0.0f;

		constexpr float dt = 1.0f / 60.0f;

		// Sixty a second at sixty frames a second is one per frame, and the
		// carry accumulator means exactly that -- not zero from truncation
		// and not two from rounding.
		for (int i = 0; i < 30; i++)
			scene->OnUpdateRuntime(dt);
		Check(emitter.Pool.size() == 30, "an emitter at 60/s has born one per frame");

		Check(Particles::System::Count(*scene) == 30,
			  "and the scene-wide count agrees");

		// Motion: everything was fired up a 25-degree cone, so everything
		// that has lived a frame has climbed. The one born this frame has
		// not been integrated yet and sits exactly at the origin -- which is
		// correct, not lazy.
		bool coneHolds = !emitter.Pool.empty();
		int climbed = 0;
		for (const Particle& particle : emitter.Pool)
		{
			coneHolds = coneHolds && particle.Position.y >= 0.0f;
			climbed += particle.Position.y > 0.0f ? 1 : 0;
		}
		Check(coneHolds && climbed >= (int)emitter.Pool.size() - 1,
			  "particles move the way the cone points");

		// Death: stop emitting, outlive the longest particle, and the pool
		// must be empty -- a particle that never dies is a leak with a size.
		emitter.Emit = false;
		emitter.Lifetime = 0.2f;   // affects new spawns only; the old keep theirs
		for (int i = 0; i < 11 * 60; i++)
			scene->OnUpdateRuntime(dt);
		Check(emitter.Pool.empty(), "every particle dies on schedule");

		// A burst is an order, not a rate: consumed in one step, capped by
		// the pool like everything else.
		emitter.MaxParticles = 10;
		emitter.Burst = 100;
		scene->OnUpdateRuntime(dt);
		Check(emitter.Pool.size() == 10, "a burst fires at once and respects the cap");
		Check(emitter.Burst == 0, "and is consumed rather than repeated");

		// The pool belongs to the run: a copied component -- which is what
		// pressing Play does to every component -- starts empty.
		ParticleEmitterComponent copied = emitter;
		Check(copied.Pool.empty() && copied.MaxParticles == 10,
			  "copying an emitter copies the settings and not the particles");

		// The GPU flag parks the CPU simulation entirely.
		emitter.Pool.clear();
		emitter.SimulateOnGpu = true;
		emitter.Emit = true;
		emitter.Burst = 5;
		scene->OnUpdateRuntime(dt);
		Check(emitter.Pool.empty() && emitter.Burst == 5,
			  "a GPU emitter is not simulated on the CPU, burst included");
		emitter.SimulateOnGpu = false;

		// The component round-trips through the scene file with its enums by
		// name, exactly as the inspector wrote them.
		emitter.Blend = ParticleBlend::Additive;
		emitter.Facing = ParticleFacing::Flat;
		emitter.Rate = 42.5f;

		SceneSerializer writer(scene);
		const std::string text = writer.SerializeToString();
		Check(text.find("ParticleEmitterComponent") != std::string::npos
			  && text.find("Additive") != std::string::npos
			  && text.find("Flat") != std::string::npos,
			  "the emitter serializes with its enums by name");

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		Check(reader.DeserializeFromString(text), "and the scene loads again");

		Entity restored = reloaded->FindEntityByName("Emitter");
		Check(restored && restored.HasComponent<ParticleEmitterComponent>(),
			  "with the emitter still on the entity");
		if (restored && restored.HasComponent<ParticleEmitterComponent>())
		{
			const auto& back = restored.GetComponent<ParticleEmitterComponent>();
			Check(back.Blend == ParticleBlend::Additive
				  && back.Facing == ParticleFacing::Flat
				  && std::fabs(back.Rate - 42.5f) < 1e-6f,
				  "and every authored value it carried");
			Check(back.Pool.empty(), "and no particles, which belong to a run");
		}

		if (Renderer::HasDevice())
			Check(ParticleRenderer::IsReady(),
				  "the particle renderer compiled its shader");
	}

	// Records every contact it is told about, so the routing can be checked
	// without anything being on screen.
	class ContactProbe : public ScriptableEntity
	{
	public:
		struct Log
		{
			int Enter = 0, Stay = 0, Exit = 0;
			int TriggerEnter = 0, TriggerStay = 0, TriggerExit = 0;
			std::string LastOther;
			Vec3 LastNormal{ 0.0f };
			float LastImpact = 0.0f;
			bool SawInvalidOther = false;
		};

		// Keyed by the entity's name, so one probe class can be attached to
		// both sides of a collision and each side checked separately.
		inline static std::map<std::string, Log> Logs;

		static void Reset() { Logs.clear(); }
		static const Log& For(const std::string& name) { return Logs[name]; }

		void OnCollisionEnter(const Collision& collision) override
		{
			Log& log = Record(collision);
			log.Enter++;
		}

		void OnCollisionStay(const Collision& collision) override { Record(collision).Stay++; }
		void OnCollisionExit(const Collision& collision) override  { Record(collision).Exit++; }

		void OnTriggerEnter(const Collision& collision) override { Record(collision).TriggerEnter++; }
		void OnTriggerStay(const Collision& collision) override  { Record(collision).TriggerStay++; }
		void OnTriggerExit(const Collision& collision) override   { Record(collision).TriggerExit++; }

	private:
		Log& Record(const Collision& collision)
		{
			Log& log = Logs[GetName()];

			if (collision.Other)
				log.LastOther = collision.Other.GetName();
			else
				log.SawInvalidOther = true;

			log.LastNormal = collision.Normal;
			log.LastImpact = Math::Max(log.LastImpact, collision.ImpactSpeed);
			return log;
		}
	};

	// Contacts routed into scripts.
	//
	// Simulated rather than asserted on: the plumbing being present says
	// nothing about whether a box landing on a floor produces exactly one
	// Enter, on both entities, with a normal that points the right way.
	void CheckContactCallbacks()
	{
		ContactProbe::Reset();
		ScriptRegistry::Register("ContactProbe", []() -> ScriptableEntity* { return new ContactProbe(); });

		auto scene = std::make_shared<Scene>();

		Entity ground = scene->CreateEntity("Ground");
		ground.GetComponent<TransformComponent>().Position = { 0.0f, -1.0f, 0.0f };
		ground.AddComponent<RigidBodyComponent>(BodyType::Static);
		ground.AddComponent<ColliderComponent>().HalfExtents = { 25.0f, 1.0f, 25.0f };
		ground.AddComponent<NativeScriptComponent>("ContactProbe");

		Entity box = scene->CreateEntity("Box");
		box.GetComponent<TransformComponent>().Position = { 0.0f, 5.0f, 0.0f };
		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>();
		box.AddComponent<NativeScriptComponent>("ContactProbe");

		// Off to one side and never touched, so "a callback fired" can be told
		// apart from "callbacks fire for everything".
		Entity lonely = scene->CreateEntity("Lonely");
		lonely.GetComponent<TransformComponent>().Position = { 40.0f, 5.0f, 0.0f };
		lonely.AddComponent<RigidBodyComponent>(BodyType::Static);
		lonely.AddComponent<ColliderComponent>();
		lonely.AddComponent<NativeScriptComponent>("ContactProbe");

		constexpr float dt = 1.0f / 60.0f;
		scene->OnRuntimeStart();

		// Falling, not yet landed.
		for (int i = 0; i < 20; i++)
			scene->OnFixedUpdateRuntime(dt);

		Check(ContactProbe::For("Box").Enter == 0, "nothing is reported while a body is in mid-air");

		// Long enough to land, but well short of falling asleep.
		for (int i = 0; i < 40; i++)
			scene->OnFixedUpdateRuntime(dt);

		const ContactProbe::Log& onBox = ContactProbe::For("Box");
		Check(onBox.Enter == 1, "landing reports exactly one collision enter");
		Check(onBox.LastOther == "Ground", "and names the entity that was hit");
		Check(onBox.Stay > 0, "and keeps reporting while they stay in contact");
		Check(onBox.Exit == 0, "with no exit while they are still touching");
		// The ground is below the box, so the normal it hands the box points up.
		Check(onBox.LastNormal.y > 0.9f, "the normal points from the other body back at this one");
		Check(onBox.LastImpact > 1.0f, "the impact carries the speed the bodies met at");

		const ContactProbe::Log& onGround = ContactProbe::For("Ground");
		Check(onGround.Enter == 1, "the other entity is told about the same collision");
		Check(onGround.LastOther == "Box", "from its own point of view");
		Check(onGround.LastNormal.y < -0.9f, "with the normal reversed for it");

		Check(ContactProbe::For("Lonely").Enter == 0, "an untouched body hears nothing");

		// Jolt withdraws the contacts of a body that falls asleep, which would
		// read as the box leaving the floor about a second after it landed.
		const int stayBeforeSleep = onBox.Stay;
		for (int i = 0; i < 240; i++)
			scene->OnFixedUpdateRuntime(dt);

		Check(onBox.Exit == 0, "a body settling and falling asleep is not reported as leaving");
		Check(onBox.Enter == 1, "and waking up again is not reported as a new collision");
		Check(onBox.Stay > stayBeforeSleep, "resting contact keeps being reported");

		// Destroying one side has to tell the other, and not leave it believing
		// it is still being stood on.
		scene->DestroyDeferred(box);
		scene->OnFixedUpdateRuntime(dt);

		Check(onGround.Exit == 1, "destroying one body reports an exit to the other");

		scene->OnRuntimeStop();
	}

	// Triggers report overlaps and resist nothing, which is a different code
	// path through the same listener.
	void CheckTriggerCallbacks()
	{
		ContactProbe::Reset();

		auto scene = std::make_shared<Scene>();

		// A slab of air the box falls through.
		Entity zone = scene->CreateEntity("Zone");
		zone.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
		zone.AddComponent<RigidBodyComponent>(BodyType::Static);
		auto& trigger = zone.AddComponent<ColliderComponent>();
		trigger.HalfExtents = { 4.0f, 0.5f, 4.0f };
		trigger.IsTrigger = true;
		zone.AddComponent<NativeScriptComponent>("ContactProbe");

		Entity box = scene->CreateEntity("Box");
		box.GetComponent<TransformComponent>().Position = { 0.0f, 6.0f, 0.0f };
		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>();
		box.AddComponent<NativeScriptComponent>("ContactProbe");

		constexpr float dt = 1.0f / 60.0f;
		scene->OnRuntimeStart();

		// Long enough to fall in and out the other side.
		for (int i = 0; i < 120; i++)
			scene->OnFixedUpdateRuntime(dt);

		// Simulated positions reach the transforms on the frame, not on the
		// step -- that is where the interpolation between the last two steps is
		// applied. Without this the transform still reads where it started.
		scene->OnUpdateRuntime(dt);

		const ContactProbe::Log& onBox = ContactProbe::For("Box");
		const ContactProbe::Log& onZone = ContactProbe::For("Zone");

		Check(onBox.TriggerEnter == 1, "entering a trigger reports once");
		Check(onBox.TriggerExit == 1, "and leaving it reports once");
		Check(onBox.TriggerStay > 0, "with the time between reported as well");
		Check(onBox.Enter == 0 && onBox.Exit == 0,
			  "a trigger does not also report as a solid collision");
		Check(onZone.TriggerEnter == 1, "the trigger itself is told what entered it");
		Check(onZone.LastOther == "Box", "and which entity it was");

		// The whole point of a trigger: it reports without resisting. A solid
		// body here would have stopped the box at the slab.
		Check(box.GetComponent<TransformComponent>().Position.y < -2.0f,
			  "and does not stop what passes through it");

		scene->OnRuntimeStop();
	}

	// The project concept.
	//
	// Everything here is about paths surviving a trip through a file, because
	// the failure this exists to prevent is a project that only opens on the
	// machine that made it.
	// The game module, end to end: a script the engine has never contained,
	// loaded from the sample project's DLL, attached, and stepped. This is the
	// bar the whole feature was built against -- anything less passes with the
	// script compiled into the engine, which is exactly the state replaced.
	//
	// Conditional on the module having been built, because the suite must not
	// require a prior Build Scripts to run at all. Skipping is loud.
	void CheckGameModule()
	{
		if (!Project::GetActive())
			return;

		std::error_code ec;
		if (!std::filesystem::exists(
				ModuleBuild::ModuleFor(Project::Root(), Project::Config().Name), ec))
		{
			RV_CORE_WARN("No built game module beside the sample project; the module "
						 "load path is not exercised here. Build Scripts in the editor "
						 "and rerun for full coverage.");
			return;
		}

		// Loaded by Project::Load at startup, not by anything this test did:
		// the module is part of opening a project.
		Check(GameModule::IsLoaded(), "opening a project loads its game module");
		Check(ScriptRegistry::IsRegistered("Rotator"),
			  "and a script the engine has never contained is registered");

		// The proof is behaviour, not presence: Rotator turns Speed rad/s
		// about Y, and this instance runs from code in the module's DLL.
		{
			auto scene = std::make_shared<Scene>();
			Entity host = scene->CreateEntity("FromModule");
			auto& script = host.AddComponent<NativeScriptComponent>("Rotator");
			script.Set("Speed", "2.0");

			scene->OnRuntimeStart();
			host.GetComponent<TransformComponent>().Rotation = Vec3(0.0f);

			for (int step = 0; step < 30; step++)
				scene->OnFixedUpdateRuntime(1.0f / 60.0f);

			const float turned = host.GetComponent<TransformComponent>().Rotation.y;
			Check(std::fabs(turned - (2.0f * 30.0f / 60.0f)) < 1e-4f,
				  "and an entity runs it, at the overridden speed");

			// Before the unload below: an instance outliving its module is the
			// use-after-free this whole design exists to prevent.
			scene->OnRuntimeStop();
		}

		// Unload takes the module's scripts with it and nothing else.
		GameModule::Unload();
		Check(!ScriptRegistry::IsRegistered("Rotator"),
			  "unloading the module unregisters its scripts");
		Check(ScriptRegistry::IsRegistered("Spinner"),
			  "and the engine's own survive");

		// And back, because the tests after this expect the world unchanged.
		Check(GameModule::Load(Project::Root(), Project::Config().Name),
			  "and the module loads again");
		Check(ScriptRegistry::IsRegistered("Rotator"), "with its scripts back");
	}

	void CheckProject()
	{
		// Kept, because the checks below open other projects and everything
		// after this expects the sample back.
		const std::filesystem::path original = Project::File();

		const std::filesystem::path root =
			ScratchDir("project-test");

		std::error_code error;
		std::filesystem::remove_all(root, error);

		Check(Project::Create(root, "Probe"), "a project can be created");
		Check(std::filesystem::exists(root / "Probe.rvproject"),
			  "which writes a .rvproject");
		Check(std::filesystem::is_directory(root / "assets"),
			  "and an assets folder beside it");

		// The skeleton is a contract, not a suggestion. A path like
		// `models/rock.gltf` only means the same thing across projects if every
		// project has the same folders, and the only way that stays true is if
		// something checks.
		for (const char* folder : { "scenes", "models", "textures", "audio", "prefabs" })
		{
			Check(std::filesystem::is_directory(root / "assets" / folder),
				  std::string("and assets/") + folder);
		}

		Check(std::filesystem::is_directory(root / "bin"),
			  "and a bin/ for builds, before the first build rather than after it");
		Check(Project::BinaryRoot() == root / "bin",
			  "which is where BinaryRoot points");

		// bin/ holds the runtime and a copy of every asset. Nobody wants that in
		// a diff, and a generated project is exactly the kind that gets
		// committed without anyone reading it first.
		Check(std::filesystem::exists(root / ".gitignore"),
			  "and a .gitignore, because bin/ must not be committed");

		// --- the C++ game module scaffold --------------------------------------
		//
		// Content checks, not just existence: an empty CMakeLists.txt would pass
		// an exists() and configure nothing. What is checked is what the scaffold
		// promises -- the engine arrives via RAGEV_ENGINE at configure time, the
		// module links the exported target, and the starter script registers
		// itself under its own name.
		{
			Check(std::filesystem::is_directory(root / "Source"),
				  "a project gets a Source/ for its C++ game module");

			std::ifstream lists(root / "Source" / "CMakeLists.txt");
			std::stringstream cmake;
			cmake << lists.rdbuf();
			const std::string build = cmake.str();

			Check(build.find("RAGEV_ENGINE") != std::string::npos,
				  "whose build takes the engine location at configure time");
			Check(build.find("RageV::RageV") != std::string::npos,
				  "and links the exported engine target");
			Check(build.find("add_library(Probe SHARED") != std::string::npos,
				  "as a DLL named after the project");
			Check(build.find("/utf-8") != std::string::npos,
				  "with /utf-8, which spdlog's fmt refuses to build without");

			std::ifstream script(root / "Source" / "Rotator.cpp");
			std::stringstream starter;
			starter << script.rdbuf();

			Check(starter.str().find("RV_REGISTER_SCRIPT(Rotator)") != std::string::npos,
				  "and a starter script that registers itself");
		}

		// Refused rather than overwritten: a project file is the only thing
		// that says where a game's assets are.
		Check(!Project::Create(root, "Probe"), "creating over an existing project is refused");

		Project::Config().StartScene = "scenes/level.rage";
		Project::Config().FixedHz = 120;
		Check(Project::Save(), "a project saves");

		Project::Close();
		Check(Project::GetActive() == nullptr, "and can be closed");

		Check(Project::Load(root / "Probe.rvproject"), "and loaded back");
		Check(Project::Config().Name == "Probe", "the name survives");
		Check(Project::Config().StartScene == "scenes/level.rage", "as does the start scene");
		Check(Project::Config().FixedHz == 120, "and the simulation rate");
		Check(Project::AssetRoot() == root / "assets",
			  "the asset root is derived from the project folder");

		// A folder, not a file: how a packaged game finds its own project.
		Check(Project::FindIn(root) == root / "Probe.rvproject",
			  "a project is found by looking in its folder");
		Check(Project::FindIn(root / "assets").empty(), "and not found where there is none");

		// --- relative paths ---------------------------------------------------
		Check(Project::MakeRelative(root / "assets" / "models" / "x.gltf") == "models/x.gltf",
			  "a path inside the project becomes relative, with forward slashes");
		Check(Project::MakeRelative(root / "elsewhere" / "x.gltf").empty(),
			  "and one outside it is refused rather than stored");

		// A failed load must leave the previous project intact rather than
		// half-replacing it.
		Check(!Project::Load(root / "nope.rvproject"), "loading a missing project fails");
		Check(Project::GetActive() != nullptr && Project::Config().Name == "Probe",
			  "and leaves the open one alone");

		std::filesystem::remove_all(root, error);

		// Back to the sample, which everything after this depends on.
		Check(Project::Load(original), "the sample project reopens");
		Assets::Registry::Init(Project::AssetRoot());
	}

	// The render graph.
	//
	// The interesting part is not that a valid frame runs -- it is that an
	// invalid one is refused, with a message, before anything is allocated.
	// A pass reading a target nothing produced is a black screen otherwise,
	// and a black screen is the single most expensive thing to diagnose in a
	// renderer.
	void CheckRenderGraph()
	{
		if (!Renderer::HasDevice())
			return;

		RenderGraph graph(Renderer::GetDevice());

		// --- a frame that describes nothing -----------------------------------
		graph.Begin(1280, 720);
		Check(!graph.Compile(), "an empty frame is refused");

		// --- a pass that draws nowhere ----------------------------------------
		graph.Begin(1280, 720);
		graph.AddPass("Nowhere", [](RGPassBuilder&) {}, [](RGPassContext&) {});
		Check(!graph.Compile(), "a pass that writes no target is refused");

		// --- reading something nobody wrote -----------------------------------
		graph.Begin(1280, 720);
		{
			RGTargetDesc desc;
			desc.Name = "Unwritten";
			const RGResource unwritten = graph.CreateTarget(desc);

			graph.AddPass("Reader",
				[&](RGPassBuilder& builder)
				{
					builder.Write(graph.Backbuffer());
					builder.Sample(unwritten);
				},
				[](RGPassContext&) {});
		}
		Check(!graph.Compile(), "a pass sampling a target nothing wrote is refused");
		Check(!graph.Errors().empty() &&
			  graph.Errors()[0].find("Unwritten") != std::string::npos,
			  "and the error names the target");

		// --- a target nothing draws into --------------------------------------
		graph.Begin(1280, 720);
		{
			RGTargetDesc desc;
			desc.Name = "Orphan";
			graph.CreateTarget(desc);
			graph.AddPass("Main", [&](RGPassBuilder& b) { b.Write(graph.Backbuffer()); },
						  [](RGPassContext&) {});
		}
		Check(!graph.Compile(), "a target created and never written is refused");

		// --- the shape a real frame has ---------------------------------------
		graph.Begin(1280, 720);
		{
			RGTargetDesc hdr;
			hdr.Name = "HDR";
			hdr.Color = RHI::Format::R16G16B16A16_SFLOAT;
			const RGResource scene = graph.CreateTarget(hdr);

			graph.AddPass("Scene",
				[&](RGPassBuilder& builder) { builder.Write(scene); },
				[](RGPassContext&) {});

			graph.AddPass("Tonemap",
				[&](RGPassBuilder& builder)
				{
					builder.Write(graph.Backbuffer());
					builder.Sample(scene);
				},
				[](RGPassContext&) {});
		}
		Check(graph.Compile(), "a pass reading an earlier pass's output compiles");
		Check(graph.GetPassCount() == 2, "with both passes");
		Check(graph.GetPassName(0) == "Scene" && graph.GetPassName(1) == "Tonemap",
			  "in the order they were declared");

		// --- targets are pooled, not reallocated -------------------------------
		const size_t afterFirst = graph.GetPooledTargetCount();

		for (int i = 0; i < 5; i++)
		{
			graph.Begin(1280, 720);
			RGTargetDesc hdr;
			hdr.Name = "HDR";
			const RGResource scene = graph.CreateTarget(hdr);
			graph.AddPass("Scene", [&](RGPassBuilder& b) { b.Write(scene); },
						  [](RGPassContext&) {});
			graph.AddPass("Tonemap",
				[&](RGPassBuilder& b) { b.Write(graph.Backbuffer()); b.Sample(scene); },
				[](RGPassContext&) {});
			graph.Compile();
		}

		Check(graph.GetPooledTargetCount() == afterFirst,
			  "a stable frame allocates nothing after the first");

		// A second target of the same shape in one frame is a second target,
		// not the same one handed out twice -- two passes each wanting "a
		// half-res buffer" want two of them.
		graph.Begin(1280, 720);
		{
			RGTargetDesc desc;
			desc.Name = "Ping";
			const RGResource ping = graph.CreateTarget(desc);
			desc.Name = "Pong";
			const RGResource pong = graph.CreateTarget(desc);

			graph.AddPass("A", [&](RGPassBuilder& b) { b.Write(ping); }, [](RGPassContext&) {});
			graph.AddPass("B", [&](RGPassBuilder& b) { b.Write(pong); b.Sample(ping); },
						  [](RGPassContext&) {});
			graph.AddPass("C", [&](RGPassBuilder& b) { b.Write(graph.Backbuffer()); b.Sample(pong); },
						  [](RGPassContext&) {});
		}
		Check(graph.Compile(), "a three-pass chain compiles");
		Check(graph.GetPooledTargetCount() >= afterFirst + 1,
			  "two targets of one shape in a frame are two targets");

		// --- scaled targets ----------------------------------------------------
		graph.Begin(1000, 500);
		{
			RGTargetDesc half;
			half.Name = "HalfRes";
			half.Scale = 0.5f;
			const RGResource small = graph.CreateTarget(half);
			graph.AddPass("Half", [&](RGPassBuilder& b) { b.Write(small); },
						  [](RGPassContext&) {});
			graph.AddPass("Out", [&](RGPassBuilder& b) { b.Write(graph.Backbuffer()); b.Sample(small); },
						  [](RGPassContext&) {});

			Check(graph.Compile(), "a half-resolution target compiles");

			uint32_t width = 0, height = 0;
			graph.GetTargetSize(small, width, height);
			Check(width == 500 && height == 250, "and is sized against the frame, not the window");
		}
	}

	// The standard frame: scene, bloom, tone mapping, anti-aliasing.
	//
	// Checked through the graph it produces rather than by looking at pixels.
	// What can go wrong here is structural -- a pass reading a level that was
	// never written, a chain that keeps going after the levels stop being worth
	// filtering, a setting that changes nothing -- and all of that is visible
	// in the pass list.
	// Primitive winding.
	//
	// The pipeline calls counter-clockwise front-facing and culls the rest, so
	// a primitive wound the other way draws the inside of its far half. The
	// silhouette is identical and, until something reads the normal, so is
	// most of the image -- which is how the sphere shipped inside out through
	// four roadmap phases and was only caught when surfaces started reflecting
	// the sky and reflected the ground instead.
	//
	// Checked against the centroid rather than against stored normals, because
	// the normals are what a wrong winding agrees with: these are all convex,
	// so a face must point away from the middle.
	void CheckPrimitiveWinding()
	{
		if (!Renderer::HasDevice())
			return;

		struct Solid { PrimitiveType Type; const char* Name; };
		const Solid solids[] = {
			{ PrimitiveType::Cube,     "cube"     },
			{ PrimitiveType::Sphere,   "sphere"   },
			{ PrimitiveType::Cylinder, "cylinder" },
		};

		for (const Solid& solid : solids)
		{
			RHI::Ref<Mesh> mesh = Mesh::GetPrimitive(Renderer::GetDevice(), solid.Type);
			if (!mesh)
			{
				Check(false, std::string("the ") + solid.Name + " primitive exists");
				continue;
			}

			const std::vector<Vec3>& positions = mesh->GetPositions();
			const std::vector<uint32_t>& indices = mesh->GetIndices();

			Vec3 centroid(0.0f);
			for (const Vec3& position : positions)
				centroid += position;
			centroid /= (float)std::max<size_t>(positions.size(), 1);

			size_t inward = 0;
			size_t degenerate = 0;

			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				const Vec3& a = positions[indices[i]];
				const Vec3& b = positions[indices[i + 1]];
				const Vec3& c = positions[indices[i + 2]];

				const Vec3 face = Math::Cross(b - a, c - a);

				// The poles of a UV sphere collapse to triangles of no area.
				// They render nothing and have no winding to be wrong about --
				// and their orientation is noise, since sin(pi) in float is
				// -8.7e-8 rather than zero, which is enough to flip one. These
				// primitives are unit sized, so the smallest triangle that
				// covers anything is many orders of magnitude above this.
				if (Math::Length(face) < 1e-6f)
				{
					degenerate++;
					continue;
				}

				if (Math::Dot(face, (a + b + c) / 3.0f - centroid) <= 0.0f)
					inward++;
			}

			Check(inward == 0, std::string("every ") + solid.Name +
								" triangle faces outwards (" + std::to_string(inward) +
								" of " + std::to_string(indices.size() / 3) + " do not)");
			Check(degenerate * 4 < indices.size() / 3,
				  std::string("and the ") + solid.Name + " is not mostly degenerate");
		}
	}

	// Cube maps.
	//
	// The conversion is a function from pixels to pixels, so it is checked
	// exactly rather than by looking at a picture -- which matters, because
	// every way of getting this wrong produces a sky that is *there*. A
	// mirrored one, an upside-down one and a correct one are all skies, and
	// only one of them is right.
	void CheckCubemap()
	{
		// --- the face table -------------------------------------------------
		const Vec3 axes[CubeFaces::kFaceCount] =
		{
			{  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
			{  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
			{  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
		};

		bool centresCorrect = true;
		bool allUnit = true;
		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
		{
			const Vec3 centre = CubeFaceDirection(face, 0.5f, 0.5f);
			centresCorrect = centresCorrect && Math::Length(centre - axes[face]) < 1e-5f;

			for (float v = 0.05f; v < 1.0f; v += 0.3f)
			{
				for (float u = 0.05f; u < 1.0f; u += 0.3f)
					allUnit = allUnit && std::fabs(Math::Length(CubeFaceDirection(face, u, v)) - 1.0f) < 1e-5f;
			}
		}

		Check(centresCorrect, "each cube face looks down its own axis");
		Check(allUnit, "and every direction it gives is a unit vector");

		// Faces meet: the left edge of -Z and the right edge of +X are the same
		// place in the world -- the direction (1, 0, -1) -- so they must look
		// the same way. This is the check that catches a face rotated or
		// mirrored, which the centre test above cannot see, since a rotated
		// face still looks down its own axis.
		{
			const Vec3 fromNegZ = CubeFaceDirection(5, 0.0f, 0.5f);
			const Vec3 fromPosX = CubeFaceDirection(0, 1.0f, 0.5f);
			Check(Math::Length(fromNegZ - fromPosX) < 1e-5f,
				  "adjacent faces agree along the edge they share");

			// And the other axis, which a vertical mirror would break while
			// leaving the horizontal seam intact.
			const Vec3 topOfPosZ = CubeFaceDirection(4, 0.5f, 0.0f);
			const Vec3 nearPosY = CubeFaceDirection(2, 0.5f, 1.0f);
			Check(Math::Length(topOfPosZ - nearPosY) < 1e-5f,
				  "and along the edge they share with the one above");
		}

		// --- the panorama mapping --------------------------------------------
		// A test image that encodes its own coordinates: red is the column and
		// green is the row. Sampling it at a known direction has one correct
		// answer, and it is arithmetic rather than judgement.
		constexpr uint32_t kWidth = 256;
		constexpr uint32_t kHeight = 128;
		std::vector<float> panorama((size_t)kWidth * kHeight * 4);

		for (uint32_t y = 0; y < kHeight; y++)
		{
			for (uint32_t x = 0; x < kWidth; x++)
			{
				float* texel = panorama.data() + ((size_t)y * kWidth + x) * 4;
				texel[0] = ((float)x + 0.5f) / (float)kWidth;
				texel[1] = ((float)y + 0.5f) / (float)kHeight;
				texel[2] = 0.0f;
				texel[3] = 1.0f;
			}
		}

		const CubeFaces faces = EquirectangularToCube(panorama.data(), kWidth, kHeight, 32);
		Check(faces.Valid() && faces.Size == 32, "a panorama converts to six complete faces");

		const uint32_t centre = faces.Size / 2;
		auto centreTexel = [&](uint32_t face) { return faces.Sample(face, centre, centre); };

		// Longitude. The centre column of the image is straight ahead of a
		// camera at rest, which means -Z, with +X a quarter turn to its right.
		// The +Z face is the seam and is left out on purpose: it is the one
		// place a bilinear filter reads across the join, and the ramp in this
		// test image is discontinuous there in a way a real sky is not.
		Check(std::fabs(centreTexel(5).r - 0.50f) < 0.02f, "the image centre lands straight ahead");
		Check(std::fabs(centreTexel(0).r - 0.75f) < 0.02f, "a quarter turn right of it lands at +X");
		Check(std::fabs(centreTexel(1).r - 0.25f) < 0.02f, "and a quarter turn left at -X");

		// Latitude. Row 0 is the top of the image and the top of the world.
		Check(centreTexel(2).g < 0.05f, "the top row of the image is straight up");
		Check(centreTexel(3).g > 0.95f, "and the bottom row straight down");
		Check(std::fabs(centreTexel(5).g - 0.50f) < 0.02f, "with the horizon halfway between");

		// A tilted read of the same image: halfway up the -Z face is halfway
		// between the horizon and the zenith, not a quarter of the way. The
		// difference is the cube's own projection, and getting it wrong bows
		// the horizon.
		{
			const Vec3 up = faces.Sample(5, centre, 0);
			Check(up.g > 0.05f && up.g < 0.30f, "a face's top edge is above the horizon but not at the pole");
		}

		// --- degenerate input -------------------------------------------------
		Check(!EquirectangularToCube(nullptr, kWidth, kHeight, 32).Valid(),
			  "no pixels converts to nothing rather than crashing");
		Check(!EquirectangularToCube(panorama.data(), kWidth, kHeight, 0).Valid(),
			  "and neither does a zero-sized face");

		// --- the GPU side -----------------------------------------------------
		if (!Renderer::HasDevice())
			return;

		auto& device = Renderer::GetDevice();

		auto cube = TextureLoader::CreateCube(device, faces, "scenetest.cube");
		Check(cube != nullptr, "the faces upload as a cube texture");

		if (cube)
		{
			Check(cube->GetDesc().Type == RHI::TextureType::TextureCube,
				  "which the RHI knows is a cube");
			Check(cube->GetDesc().Layers == 6, "with six layers");
			Check(cube->GetWidth() == 32 && cube->GetHeight() == 32, "at the size asked for");
			// Zero asked for the full chain, so the desc must have been filled
			// in -- a cube with one mip cannot serve a roughness lookup later.
			Check(cube->GetDesc().MipLevels > 1, "and a mip chain, which 3.4 will need");

			// Out of range is refused rather than corrupting a neighbour or
			// walking off the end of the image.
			const std::vector<uint16_t> junk((size_t)32 * 32 * 4, 0);
			cube->UploadLayer(junk.data(), junk.size() * sizeof(uint16_t), 9);
			Check(true, "uploading a layer that does not exist is survivable");
		}

		Check(TextureLoader::BlackCube(device) != nullptr,
			  "there is a neutral cube to bind when a scene has no sky");

		CubeFaces incomplete;
		incomplete.Size = 4;
		Check(TextureLoader::CreateCube(device, incomplete, "scenetest.broken") == nullptr,
			  "and an incomplete set of faces is refused");
	}

	// Every renderer subsystem actually came up.
	//
	// This exists because of a specific failure and the shape of it matters
	// more than the instance: EnvironmentIBL logged "ready" unconditionally, so
	// a shader that would not compile produced a feature that was present in
	// every sense except that it did nothing. Nothing failed, nothing was red,
	// and the only symptom was a picture that looked slightly wrong -- which is
	// indistinguishable from the feature simply not being very good.
	//
	// A log line cannot be tested. A flag can, and asking every subsystem for
	// its own turns a silent shader failure into a failing build.
	void CheckRenderersReady()
	{
		if (!Renderer::HasDevice())
			return;

		// More than one frame in flight, on both backends.
		//
		// OpenGL used to report 1, which is what every subsystem sizes its
		// per-frame buffers by -- so each kept a single copy and rewrote it
		// while the GPU was still reading the previous frame out of it. A
		// static scene then rendered differently from one frame to the next.
		// Checked here rather than left to a screenshot, because the symptom
		// was a shimmer that looked like a shading artefact.
		Check(Renderer::GetDevice().GetFramesInFlight() >= 2,
			  "the device has more than one frame in flight");
		Check(Renderer::GetDevice().GetFrameIndex() < Renderer::GetDevice().GetFramesInFlight(),
			  "and its frame index stays inside them");

		Check(Renderer2D::IsReady(), "Renderer2D came up");
		Check(Renderer3D::IsReady(), "Renderer3D came up, with both its shaders");
		Check(DebugRenderer::IsReady(), "DebugRenderer came up");
		Check(Skybox::IsReady(), "the sky came up");
		Check(ShadowMap::IsReady(), "the shadow maps came up");
		Check(PostProcess::IsReady(), "the post chain came up");
		Check(EnvironmentIBL::IsReady(), "image-based lighting came up");
		Check(EnvironmentIBL::GetBRDF() != nullptr, "with a BRDF table behind it");

		// Coming up is not the same as doing anything.
		//
		// The prefilter refused any source smaller than 64 pixels a face, and
		// the default sky's cube is 32 -- so on a scene with no environment map,
		// which is the common case, reflections fell back to the box-filtered
		// chain and IsReady() said yes the whole time. A subsystem reporting
		// itself healthy is worth exactly as much as the work it then does.
		Check(EnvironmentIBL::LevelsFor(32) >= 2,
			  "and the default sky's 32 px cube is large enough to prefilter");
		Check(EnvironmentIBL::LevelsFor(512) == EnvironmentIBL::kRoughnessLevels,
			  "with a full set of roughness levels once the source is large");
		Check(EnvironmentIBL::LevelsFor(8) >= 2 &&
			  EnvironmentIBL::LevelsFor(8) < EnvironmentIBL::kRoughnessLevels,
			  "a small source carries fewer levels rather than none");
		Check(EnvironmentIBL::LevelsFor(1) == 0 && EnvironmentIBL::LevelsFor(0) == 0,
			  "and one texel carries none at all");
	}

	// Inspector labels.
	//
	// The label and the serialized key are the same string in the source and
	// must not be the same string in use: the key is what every scene on disk
	// is written with, so prettifying it in place would silently orphan every
	// saved value. The check that matters is the second one.
	void CheckFieldLabels()
	{
		struct Case { const char* Name; const char* Reads; };
		const Case cases[] = {
			{ "CastShadows",          "Cast shadows" },
			{ "InnerCone",            "Inner cone" },
			{ "OrthographicNearClip", "Orthographic near clip" },
			{ "PerspectiveFOV",       "Perspective FOV" },   // an acronym keeps its case
			{ "PlayOnAwake",          "Play on awake" },
			{ "Mass",                 "Mass" },              // one word is left alone
			{ "MinDistance",          "Min distance" },
			{ "IsTrigger",            "Is trigger" },
		};

		bool correct = true;
		for (const Case& item : cases)
		{
			const std::string derived = HumanFieldName(item.Name);
			if (derived != item.Reads)
			{
				RV_CORE_ERROR("  '{0}' reads as '{1}', expected '{2}'",
							  item.Name, derived, item.Reads);
				correct = false;
			}
		}

		Check(correct, "field names read as sentences in the inspector");

		// An authored label wins over the derived one, and the serialized key
		// is untouched by it. Derivation cannot fix every case -- an acronym
		// that is not all-caps in the key, or a key kept for compatibility that
		// no longer describes the field -- and renaming the key to fix a label
		// would rewrite every scene file on disk.
		{
			bool authored = false;
			bool keyKept = false;
			for (const ComponentDesc& component : ComponentRegistry::All())
			{
				if (std::string(component.Name) != "ColorComponent")
					continue;

				for (const FieldDesc& field : component.Fields)
				{
					if (std::string(field.Name) != "ColorValue")
						continue;

					authored = field.DisplayName == "Color";
					keyKept = std::string(field.Name) == "ColorValue";
				}
			}

			Check(authored, "an authored label replaces the derived one");
			Check(keyKept, "and leaves the serialized key alone");
			Check(HumanFieldName("ColorValue") == "Color value",
				  "which the derivation on its own would not have produced");
		}
		Check(HumanFieldName(nullptr).empty() && HumanFieldName("").empty(),
			  "and nothing at all reads as nothing");

		// Every described field has a label, and none of them replaced the key.
		bool keysIntact = true;
		bool labelled = true;
		for (const ComponentDesc& component : ComponentRegistry::All())
		{
			for (const FieldDesc& field : component.Fields)
			{
				labelled = labelled && !field.DisplayName.empty();
				keysIntact = keysIntact && field.Name != nullptr &&
							 std::string(field.Name) == std::string(field.Name);
			}
		}

		Check(labelled, "every field has one");

		// Every field is exactly as wide as the type it is read and written
		// as. Reflection is type-erased: an Enum field is reached through an
		// `int*` by the serializer, the undo stack, the inspector and the C#
		// bridge alike, so an enum declared `: uint8_t` would compile, pass
		// review, and write three bytes past its end into whatever member
		// follows it.
		//
		// Field<> static_asserts the enum case, which is where it will
		// actually be caught. This is the same claim made about the registry
		// as it stands rather than about the template -- it covers a
		// descriptor assembled by hand, and it names the field when it fails
		// instead of pointing at a template instantiation.
		{
			const char* offender = nullptr;
			const char* offendingComponent = nullptr;

			for (const ComponentDesc& component : ComponentRegistry::All())
			{
				for (const FieldDesc& field : component.Fields)
				{
					// Zero means hand-built rather than through Field<>;
					// there is nothing to compare against.
					if (field.Size == 0)
						continue;

					size_t expected = 0;
					switch (field.Type)
					{
						case FieldType::Bool:   expected = sizeof(bool); break;
						case FieldType::Int:    expected = sizeof(int); break;
						case FieldType::Enum:   expected = sizeof(int); break;
						case FieldType::Float:  expected = sizeof(float); break;
						case FieldType::Vec3:   expected = sizeof(Vec3); break;
						case FieldType::Vec4:   expected = sizeof(Vec4); break;
						case FieldType::String: expected = sizeof(std::string); break;
						case FieldType::Asset:  expected = sizeof(AssetHandle); break;
					}

					if (field.Size != expected && !offender)
					{
						offender = field.Name;
						offendingComponent = component.Name;
					}
				}
			}

			if (offender)
			{
				RV_CORE_ERROR("{0}::{1} is not the width its FieldType is written as",
							  offendingComponent, offender);
			}

			Check(offender == nullptr,
				  "every registered field is as wide as the type reflection reads it as");
		}

		// Names are keys, and a duplicate key is a silent corruption of the
		// same family as the enum above: the serializer writes both fields
		// under one YAML key and the loader reads whichever it meets last
		// into only one of them, so the other silently stops persisting.
		// Nothing about that is visible in the inspector, which draws both.
		{
			bool duplicateField = false;
			bool duplicateComponent = false;
			std::unordered_set<std::string> componentNames;

			for (const ComponentDesc& component : ComponentRegistry::All())
			{
				if (component.Name && !componentNames.insert(component.Name).second)
				{
					duplicateComponent = true;
					RV_CORE_ERROR("Two components registered as '{0}'", component.Name);
				}

				std::unordered_set<std::string> fieldNames;
				for (const FieldDesc& field : component.Fields)
				{
					if (field.Name && !fieldNames.insert(field.Name).second)
					{
						duplicateField = true;
						RV_CORE_ERROR("{0} registers '{1}' twice",
									  component.Name, field.Name);
					}
				}
			}

			Check(!duplicateComponent, "no two components share a registry name");
			Check(!duplicateField, "and no component registers one field name twice");
		}

		// The property that actually protects saved scenes: the serializer
		// writes the key, not the label. A scene written before this change
		// has to keep loading, and one written after has to keep the same
		// spelling.
		auto scene = std::make_shared<Scene>();
		Entity light = scene->CreateEntity("Spot");
		light.AddComponent<LightComponent>().Light.Type = Light::LightType::Spot;

		SceneSerializer serializer(scene);
		const std::string text = serializer.SerializeToString();

		Check(text.find("InnerCone:") != std::string::npos,
			  "and the scene file still says InnerCone, not 'Inner cone'");
		Check(text.find("Inner cone") == std::string::npos,
			  "with no label anywhere in it");
	}

	// The shadow toggle on a light.
	//
	// One checkbox, but it reaches three places that are edited separately --
	// the component registry the inspector is generated from, the serializer
	// driven by that same registry, and the gather in Scene::RenderShadows --
	// and a light that silently keeps casting after being told not to is
	// indistinguishable from the feature not existing.
	void CheckShadowToggle()
	{
		// Described, therefore in the inspector: the panel is generated from
		// this, so a field that is here is a widget and one that is not is
		// nothing at all.
		bool described = false;
		if (const ComponentDesc* component = ComponentRegistry::Find("LightComponent"))
		{
			for (const FieldDesc& field : component->Fields)
			{
				if (std::string(field.Name) == "CastShadows")
					described = field.Type == FieldType::Bool;
			}
		}

		Check(described, "a light describes CastShadows as a checkbox");

		// And it survives a save and a load. The serializer walks the same
		// description, so this also fails if the field is described but the
		// round trip drops it.
		auto scene = std::make_shared<Scene>();
		Entity sun = scene->CreateEntity("Sun");
		auto& light = sun.AddComponent<LightComponent>().Light;
		light.Type = Light::LightType::Directional;
		light.CastShadows = false;

		SceneSerializer serializer(scene);
		const std::string text = serializer.SerializeToString();

		Check(text.find("CastShadows") != std::string::npos,
			  "and writes it to the scene file");

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer back(reloaded);
		Check(back.DeserializeFromString(text), "which loads again");

		Entity restored = reloaded->FindEntityByName("Sun");
		Check(restored && restored.HasComponent<LightComponent>() &&
			  !restored.GetComponent<LightComponent>().Light.CastShadows,
			  "with the light still not casting");

		// Nothing to render when nothing casts. RenderShadows leaves the
		// cascade count at zero, and the shader's whole shadow term is switched
		// off by that one number.
		ShadowMap::Invalidate();
		Check(!ShadowMap::HasCascades(),
			  "and a scene where no light casts renders no cascades at all");
	}

	// Cascaded shadow maps.
	//
	// Every property that matters here is invisible in a still frame and
	// obvious in motion, which makes this the worst possible feature to verify
	// by looking at a screenshot. A cascade that changes size as the camera
	// turns makes every shadow edge crawl; one that is not snapped to its own
	// texel grid makes them shimmer on each sub-texel step. Both render
	// perfectly convincing single frames.
	void CheckShadowCascades()
	{
		constexpr uint32_t kCount = 4;
		constexpr uint32_t kResolution = 1024;

		const Vec3 lightDirection = Math::Normalize(Vec3(-0.4f, -1.0f, -0.25f));
		const float fov = Math::Radians(60.0f);
		const float aspect = 16.0f / 9.0f;
		const float distance = 60.0f;

		auto fit = [&](const Mat4& cameraTransform, ShadowCascade* out, bool flip = false)
		{
			ShadowMap::ComputeCascades(cameraTransform, fov, aspect, 0.1f, distance,
									   lightDirection, kCount, kResolution, 0.85f, flip, out);
		};

		ShadowCascade level[kCount];
		fit(Mat4(1.0f), level);

		// --- splits -------------------------------------------------------------
		{
			bool increasing = true;
			for (uint32_t i = 1; i < kCount; i++)
				increasing = increasing && level[i].SplitDepth > level[i - 1].SplitDepth;

			Check(increasing, "cascade splits increase with distance");
			Check(std::fabs(level[kCount - 1].SplitDepth - distance) < 0.01f,
				  "and the last one ends exactly at the shadow distance");
			Check(level[0].TexelWorldSize < level[kCount - 1].TexelWorldSize,
				  "so the near cascade's texels are the smaller ones");
		}

		// --- the property the sphere fit exists for -------------------------------
		// Turn the camera through a full circle and the cascades must not change
		// size. A box fitted to the frustum corners would breathe with every
		// degree, and every shadow edge in the cascade would crawl with it.
		{
			bool stable = true;
			for (int step = 1; step < 24; step++)
			{
				const float yaw = Math::TwoPi * (float)step / 24.0f;
				const float pitch = Math::Radians(-35.0f) * std::sin(yaw * 3.0f);

				Mat4 turned = Math::Rotate(Mat4(1.0f), yaw, Vec3(0, 1, 0));
				turned = Math::Rotate(turned, pitch, Vec3(1, 0, 0));

				ShadowCascade rotated[kCount];
				fit(turned, rotated);

				for (uint32_t i = 0; i < kCount; i++)
				{
					stable = stable && std::fabs(rotated[i].TexelWorldSize -
												 level[i].TexelWorldSize) < 1e-6f;
				}
			}

			Check(stable, "and rotating the camera does not change a cascade's size");
		}

		// --- texel snapping -------------------------------------------------------
		// The projection's origin has to land on a whole texel. Without it the
		// sampled grid slides continuously under the geometry and every edge
		// shimmers on sub-texel movement.
		{
			bool snapped = true;
			for (int step = 0; step < 8; step++)
			{
				// Deliberately not a multiple of anything: a texel here is
				// centimetres, and these are millimetre steps.
				const Mat4 nudged = Math::Translate(
					Mat4(1.0f), Vec3(0.0013f * (float)step, 0.0f, 0.0007f * (float)step));

				ShadowCascade moved[kCount];
				fit(nudged, moved);

				for (uint32_t i = 0; i < kCount; i++)
				{
					const Vec4 origin =
						moved[i].ViewProjection * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
					const Vec2 texels = Vec2(origin) * ((float)kResolution * 0.5f);

					snapped = snapped &&
							  std::fabs(texels.x - std::round(texels.x)) < 1e-3f &&
							  std::fabs(texels.y - std::round(texels.y)) < 1e-3f;
				}
			}

			Check(snapped, "and the projection stays snapped to whole texels as it moves");
		}

		// --- the lookup matrix -----------------------------------------------------
		{
			// A point in the middle of the near cascade must land inside the map.
			const Vec3 inside(0.0f, 0.0f, -level[0].SplitDepth * 0.5f);
			const Vec4 lookup = level[0].LookupMatrix * Vec4(inside, 1.0f);
			const Vec3 coordinate = Vec3(lookup) / lookup.w;

			Check(coordinate.x > 0.0f && coordinate.x < 1.0f &&
				  coordinate.y > 0.0f && coordinate.y < 1.0f,
				  "a point inside a cascade maps into its shadow map");
			Check(coordinate.z >= 0.0f && coordinate.z <= 1.0f,
				  "with a depth already in the range the comparison expects");

			// The flip is a backend property, and the only difference it makes
			// is which way up v runs. Getting it wrong shadows the scene with a
			// mirror image of itself, which looks like broken geometry rather
			// than a wrong matrix.
			ShadowCascade flipped[kCount];
			fit(Mat4(1.0f), flipped, true);

			const Vec4 other = flipped[0].LookupMatrix * Vec4(inside, 1.0f);
			const Vec3 mirrored = Vec3(other) / other.w;

			Check(std::fabs(mirrored.x - coordinate.x) < 1e-5f &&
				  std::fabs(mirrored.z - coordinate.z) < 1e-5f &&
				  std::fabs(mirrored.y - (1.0f - coordinate.y)) < 1e-5f,
				  "and asking for the flip mirrors v and nothing else");
		}

		// --- degenerate input ------------------------------------------------------
		{
			ShadowCascade one[ShadowMap::kMaxCascades];
			ShadowMap::ComputeCascades(Mat4(1.0f), fov, aspect, 0.1f, distance,
									   Vec3(0.0f), 99, kResolution, 0.85f, false, one);
			Check(one[ShadowMap::kMaxCascades - 1].SplitDepth > 0.0f,
				  "asking for more cascades than exist fills the ones that do");

			// A light pointing straight down is the case where an up vector of
			// +Y is parallel to the view and lookAt degenerates.
			ShadowCascade overhead[kCount];
			ShadowMap::ComputeCascades(Mat4(1.0f), fov, aspect, 0.1f, distance,
									   Vec3(0.0f, -1.0f, 0.0f), kCount, kResolution,
									   0.85f, false, overhead);

			const Vec4 lookup =
				overhead[0].LookupMatrix * Vec4(0.0f, 0.0f, -5.0f, 1.0f);
			const Vec3 coordinate = Vec3(lookup) / lookup.w;
			Check(std::isfinite(coordinate.x) && std::isfinite(coordinate.y) &&
				  std::isfinite(coordinate.z),
				  "and a light pointing straight down still produces a usable matrix");
		}
	}

	// Diffuse irradiance.
	//
	// The half of image-based lighting that replaces a flat ambient colour, and
	// a convolution whose output is a very blurry cube -- so "looks about
	// right" is the only thing a screenshot can say about it, and it says that
	// about a wrong answer too. These are the properties that pin it down.
	void CheckIrradiance()
	{
		auto uniform = [](uint32_t size, const Vec3& colour)
		{
			CubeFaces cube;
			cube.Size = size;
			cube.Pixels.resize((size_t)CubeFaces::kFaceCount * size * size * 4);

			for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			{
				float* pixels = cube.Face(face);
				for (uint32_t i = 0; i < size * size; i++)
				{
					pixels[i * 4 + 0] = colour.r;
					pixels[i * 4 + 1] = colour.g;
					pixels[i * 4 + 2] = colour.b;
					pixels[i * 4 + 3] = 1.0f;
				}
			}
			return cube;
		};

		// The strongest single property: the cosine-weighted average of a
		// constant is that constant. It fails if the weights are wrong, if the
		// hemisphere is the wrong size, or if the normalisation is off -- which
		// covers most of the ways to get an integral wrong.
		{
			const CubeFaces source = uniform(8, Vec3(0.6f, 0.3f, 0.9f));
			const CubeFaces result = IrradianceFromCube(source, 8, 24);

			Check(result.Valid() && result.Size == 8, "irradiance produces a complete cube");

			bool constant = true;
			for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			{
				for (uint32_t y = 0; y < 8; y++)
				{
					for (uint32_t x = 0; x < 8; x++)
					{
						const Vec3 value = result.Sample(face, x, y);
						constant = constant && Math::Length(value - Vec3(0.6f, 0.3f, 0.9f)) < 0.02f;
					}
				}
			}

			Check(constant, "and a uniform environment convolves to that same colour everywhere");
		}

		// Direction. A sky that is bright above and dark below has to produce
		// irradiance that is bright facing up and dark facing down -- which is
		// the entire reason this exists rather than one number.
		{
			CubeFaces source = uniform(16, Vec3(0.0f));
			float* top = source.Face(2);   // +Y
			for (uint32_t i = 0; i < 16 * 16; i++)
			{
				top[i * 4 + 0] = 1.0f;
				top[i * 4 + 1] = 1.0f;
				top[i * 4 + 2] = 1.0f;
			}

			const CubeFaces result = IrradianceFromCube(source, 8, 32);
			const Vec3 up = result.Sample(2, 4, 4);
			const Vec3 down = result.Sample(3, 4, 4);
			const Vec3 side = result.Sample(4, 4, 4);

			Check(up.r > side.r && side.r > down.r,
				  "a bright sky lights upward faces most and downward faces least");
			Check(down.r < 0.02f, "with nothing at all arriving from below");
			// Energy: no direction can receive more than the brightest thing
			// visible anywhere.
			Check(up.r <= 1.001f, "and no surface receives more than was emitted");
		}

		// The lookup has to be the inverse of the face table, on every axis. If
		// they disagree the irradiance cube comes out rotated relative to the
		// sky it was made from, which looks like nothing at all until a scene is
		// lit by it and then looks like the sun is in the wrong place.
		//
		// Light one face at a time and the brightest direction must be that
		// face's own. Doing all six catches an axis swap, which lighting only
		// +Y would not -- +Y is the one direction the tangent frame special
		// cases.
		{
			bool aligned = true;

			for (uint32_t lit = 0; lit < CubeFaces::kFaceCount; lit++)
			{
				CubeFaces source = uniform(8, Vec3(0.0f));
				float* pixels = source.Face(lit);
				for (uint32_t i = 0; i < 8 * 8; i++)
					pixels[i * 4 + 0] = 1.0f;

				const CubeFaces result = IrradianceFromCube(source, 4, 32);

				uint32_t brightest = 0;
				float best = -1.0f;
				for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
				{
					const float value = result.Sample(face, 2, 2).r;
					if (value > best)
					{
						best = value;
						brightest = face;
					}
				}

				if (brightest != lit)
				{
					RV_CORE_ERROR("  lighting face {0} produced most irradiance on face {1}",
								  lit, brightest);
					aligned = false;
				}
			}

			Check(aligned, "and the brightest direction is the one the light came from, on every axis");
		}

		Check(!IrradianceFromCube(CubeFaces{}, 8).Valid(),
			  "an empty environment convolves to nothing rather than crashing");

		// Which sky the ambient comes from when the irradiance is missing.
		//
		// There are two ways to have no irradiance and they want opposite
		// answers, which is the whole reason this takes the cube as well. With
		// no cube, Draw is showing the gradient and the gradient's irradiance
		// matches the screen. With a cube but no irradiance, Draw is showing
		// the cube -- and the gradient's colours then describe a sky that is
		// not there. That case used to fall through to the gradient anyway and
		// light the scene a plausible wrong colour in silence.
		if (Renderer::HasDevice())
		{
			auto& device = Renderer::GetDevice();

			SceneEnvironment gradientSky;
			gradientSky.Sky = SkyType::Gradient;

			SceneEnvironment cubemapSky;
			cubemapSky.Sky = SkyType::Cubemap;

			// Any real cube will do as a stand-in for a loaded sky; the
			// gradient's own is one that certainly exists and is not the black
			// cube the failure path returns.
			auto someCube = Skybox::ResolveEnvironment(gradientSky, nullptr);
			auto black = TextureLoader::BlackCube(device);
			auto gradientIrradiance = Skybox::ResolveIrradiance(gradientSky, nullptr, nullptr);

			Check(someCube != nullptr && black != nullptr && gradientIrradiance != nullptr &&
				  someCube != black && gradientIrradiance != black,
				  "the gradient sky has a cube and an irradiance of its own");

			Check(Skybox::ResolveIrradiance(cubemapSky, someCube, someCube) == someCube,
				  "a cubemap sky with an irradiance uses it");

			const auto orphaned = Skybox::ResolveIrradiance(cubemapSky, someCube, nullptr);
			Check(orphaned == black,
				  "a cubemap sky with a cube but no irradiance contributes no ambient");
			Check(orphaned != gradientIrradiance,
				  "and specifically not the gradient's, which is a sky that is not on screen");

			Check(Skybox::ResolveIrradiance(cubemapSky, nullptr, nullptr) == gradientIrradiance,
				  "but with no cube at all the gradient is what is drawn, so it is what lights");
		}
	}

	// Skeletons and pose evaluation.
	//
	// The headline check is the bind-pose one: a skeleton standing where it was
	// modelled must produce an identity skinning matrix for every bone. It
	// fails if the inverse bind matrices are wrong, if the hierarchy composes
	// in the wrong order, or if anything is in the wrong space -- three
	// separate mistakes whose symptom is the same character, subtly deformed,
	// and none of which a screenshot tells apart.
	void CheckSkeleton()
	{
		// A three-bone chain: a root at the origin and two bones a metre apart
		// up the Y axis.
		Skeleton skeleton;
		{
			Bone root;
			root.Name = "root";
			root.Parent = -1;

			Bone middle;
			middle.Name = "middle";
			middle.Parent = 0;
			middle.RestPosition = { 0.0f, 1.0f, 0.0f };

			Bone tip;
			tip.Name = "tip";
			tip.Parent = 1;
			tip.RestPosition = { 0.0f, 1.0f, 0.0f };

			skeleton.Bones = { root, middle, tip };

			// Each inverse bind is the inverse of that bone's mesh-space rest
			// transform, composed the long way round -- which is what an
			// exporter does, and what stops the identity property below from
			// being circular.
			Pose rest;
			RestPose(skeleton, rest);

			std::vector<Mat4> global;
			ComposeGlobal(skeleton, rest, global);

			for (size_t i = 0; i < skeleton.Bones.size(); i++)
				skeleton.Bones[i].InverseBind = Math::Inverse(global[i]);
		}

		Check(skeleton.IsWellOrdered(), "a skeleton lists parents before children");
		Check(skeleton.Find("middle") == 1 && skeleton.Find("absent") == -1,
			  "bones are findable by name");

		{
			Skeleton broken;
			Bone child; child.Parent = 1;
			Bone parent; parent.Parent = -1;
			broken.Bones = { child, parent };
			Check(!broken.IsWellOrdered(), "and a child before its parent is rejected");
		}

		auto nearlyIdentity = [](const Mat4& m)
		{
			for (int c = 0; c < 4; c++)
			{
				for (int r = 0; r < 4; r++)
				{
					const float expected = c == r ? 1.0f : 0.0f;
					if (std::fabs(m[c][r] - expected) > 1e-4f)
						return false;
				}
			}
			return true;
		};

		// The property everything else rests on.
		{
			Pose rest;
			RestPose(skeleton, rest);

			std::vector<Mat4> skinning;
			ComposeSkinning(skeleton, rest, skinning);

			bool identity = skinning.size() == 3;
			for (const Mat4& m : skinning)
				identity = identity && nearlyIdentity(m);

			Check(identity, "at the bind pose every skinning matrix is the identity");
		}

		// A clip that animates nothing leaves the skeleton at rest, not at the
		// origin.
		{
			Anim::Clip empty;
			empty.Duration = 1.0f;

			Pose pose;
			SamplePose(skeleton, empty, 0.5f, true, pose);

			std::vector<Mat4> skinning;
			ComposeSkinning(skeleton, pose, skinning);

			bool identity = true;
			for (const Mat4& m : skinning)
				identity = identity && nearlyIdentity(m);

			Check(identity, "a clip that animates no bone holds them all at rest");
		}

		// A clip that rotates the middle bone, checked by where it puts the
		// tip -- the observable a person would actually notice.
		Anim::Clip clip;
		clip.Name = "bend";
		clip.Tracks.resize(3);
		{
			clip.Tracks[1].Rotation.Times = { 0.0f, 2.0f };
			clip.Tracks[1].Rotation.Values = {
				Quat(1.0f, 0.0f, 0.0f, 0.0f),
				Math::AngleAxis(Math::Radians(90.0f), Vec3(0.0f, 0.0f, 1.0f)),
			};

			// A second channel type, so the sampler is exercised on more than
			// rotation alone.
			clip.Tracks[0].Position.Times = { 0.0f, 2.0f };
			clip.Tracks[0].Position.Values = { Vec3(0.0f), Vec3(0.0f) };
		}
		clip.RecomputeDuration();

		Check(std::fabs(clip.Duration - 2.0f) < 1e-5f,
			  "a clip's duration comes from its last key rather than from an author");

		auto tipAt = [&](float time, bool loop)
		{
			Pose pose;
			SamplePose(skeleton, clip, time, loop, pose);

			std::vector<Mat4> global;
			ComposeGlobal(skeleton, pose, global);

			return Vec3(global[2][3]);
		};

		Check(Math::Length(tipAt(0.0f, false) - Vec3(0.0f, 2.0f, 0.0f)) < 1e-4f,
			  "the chain starts straight");
		Check(Math::Length(tipAt(2.0f, false) - Vec3(-1.0f, 1.0f, 0.0f)) < 1e-4f,
			  "and a ninety degree bend swings the tip out to the side");

		// Halfway the bone is at 45 degrees, so the tip is on the arc rather
		// than on the chord between the two ends. That difference is the whole
		// reason the sampler slerps.
		{
			const float leg = std::sqrt(0.5f);
			Check(Math::Length(tipAt(1.0f, false) - Vec3(-leg, 1.0f + leg, 0.0f)) < 1e-3f,
				  "halfway through, the tip is on the arc and not on the chord");
		}

		// Time outside the clip.
		{
			Check(Math::Length(tipAt(50.0f, false) - Vec3(-1.0f, 1.0f, 0.0f)) < 1e-4f,
				  "a time past the end holds the last pose when not looping");
			Check(Math::Length(tipAt(2.5f, true) - tipAt(0.5f, true)) < 1e-4f,
				  "and wraps when looping");

			// fmod keeps the numerator's sign, so a negative time lands outside
			// the clip unless it is pushed back in. A blend running backwards
			// produces one.
			Check(Math::Length(tipAt(-0.5f, true) - tipAt(1.5f, true)) < 1e-4f,
				  "including backwards, where fmod alone would leave the clip");
		}

		// Two keys either side of the antipode must take the short arc.
		{
			Skeleton one;
			Bone solo; solo.Name = "solo"; solo.Parent = -1;
			one.Bones = { solo };

			Anim::Clip spin;
			spin.Tracks.resize(1);
			spin.Tracks[0].Rotation.Times = { 0.0f, 1.0f };
			spin.Tracks[0].Rotation.Values = {
				Math::AngleAxis(Math::Radians(10.0f), Vec3(0.0f, 1.0f, 0.0f)),
				// The same rotation as minus ten degrees, written with the
				// opposite sign -- which is what an exporter emits about half
				// the time.
				-Math::AngleAxis(Math::Radians(-10.0f), Vec3(0.0f, 1.0f, 0.0f)),
			};
			spin.RecomputeDuration();

			Pose pose;
			SamplePose(one, spin, 0.5f, false, pose);

			// Halfway between plus and minus ten is zero, so the bone faces
			// forward. The long way round would put it at 180.
			const Vec3 forward = pose[0].Rotation * Vec3(0.0f, 0.0f, -1.0f);
			Check(forward.z < -0.99f, "a quaternion pair takes the short arc between them");
		}

		// Blending.
		{
			Pose a, b, mixed;
			RestPose(skeleton, a);
			SamplePose(skeleton, clip, 2.0f, false, b);

			BlendPoses(a, b, 0.0f, mixed);
			Check(Math::Length(mixed[1].Rotation - a[1].Rotation) < 1e-4f,
				  "a blend of zero is the first pose");

			BlendPoses(a, b, 1.0f, mixed);
			Check(std::fabs(Math::Dot(mixed[1].Rotation, b[1].Rotation)) > 0.9999f,
				  "and a blend of one is the second");

			BlendPoses(a, b, 0.5f, mixed);
			const float angle = Math::Degrees(Math::Angle(Math::Normalize(mixed[1].Rotation)));
			Check(std::fabs(angle - 45.0f) < 0.5f, "and halfway is halfway round the arc");
		}

		// Nothing at all, which is what a scene with no skeleton hands in.
		{
			Skeleton none;
			Anim::Clip clipless;
			Pose pose;
			SamplePose(none, clipless, 1.0f, true, pose);

			std::vector<Mat4> skinning;
			ComposeSkinning(none, pose, skinning);

			Check(pose.empty() && skinning.empty(),
				  "an empty skeleton samples to an empty pose rather than crashing");
		}
	}

	// Importing a skinned model.
	//
	// Against an asset this repository generates rather than one somebody
	// downloaded, so every number below is a discrepancy against something
	// known. tools/scripts/make_skinned_gltf.py writes it: a two-bone post,
	// bone 1 a metre above bone 0, weights crossing over in the middle.
	void CheckSkinnedImport()
	{
		const std::filesystem::path model =
			std::filesystem::path("assets") / "models" / "limb.gltf";

		if (!std::filesystem::exists(model))
		{
			// Staged per target like every other asset. A missing file is a
			// build problem, not a silent skip -- a test that quietly does
			// nothing is worse than one that is absent.
			Check(false, "the skinned test model is staged beside the executable");
			return;
		}

		Assets::ImportedModel imported;
		Check(Assets::GltfImporter::Import(model, imported), "a skinned glTF imports");

		Check(imported.HasSkeleton() && imported.Skeleton.Size() == 2,
			  "and brings two bones with it");
		Check(imported.Skeleton.IsWellOrdered(),
			  "ordered parents first, whatever order the file listed them in");
		Check(imported.Skeleton.Bones[0].Parent == -1 && imported.Skeleton.Bones[1].Parent == 0,
			  "with the chain intact");

		// The rest transform, straight off the node.
		Check(Math::Length(imported.Skeleton.Bones[1].RestPosition -
						  Vec3(0.0f, 1.0f, 0.0f)) < 1e-5f,
			  "the second bone rests a metre above the first");

		// The inverse bind, which is the thing an importer most often ignores.
		// Identity for both would look right in every other respect.
		{
			const Mat4& bind = imported.Skeleton.Bones[1].InverseBind;
			const Vec3 offset = Vec3(bind[3]);
			Check(Math::Length(offset - Vec3(0.0f, -1.0f, 0.0f)) < 1e-5f,
				  "and its inverse bind subtracts that metre rather than being the identity");
		}

		// The bind-pose property again, now on imported data rather than a
		// fixture: rest pose in, identity out.
		{
			Pose rest;
			RestPose(imported.Skeleton, rest);

			std::vector<Mat4> skinning;
			ComposeSkinning(imported.Skeleton, rest, skinning);

			bool identity = true;
			for (const Mat4& m : skinning)
			{
				for (int c = 0; c < 4 && identity; c++)
				{
					for (int r = 0; r < 4 && identity; r++)
					{
						const float expected = c == r ? 1.0f : 0.0f;
						identity = std::fabs(m[c][r] - expected) < 1e-4f;
					}
				}
			}

			Check(identity, "the imported skeleton is the identity at its bind pose");
		}

		// The mesh.
		Check(!imported.Primitives.empty(), "the model has geometry");
		if (imported.Primitives.empty())
			return;

		const Assets::ImportedPrimitive& primitive = imported.Primitives[0];
		Check(primitive.IsSkinned(), "which is skinned");
		Check(primitive.Joints.size() == primitive.Vertices.size() &&
			  primitive.Weights.size() == primitive.Vertices.size(),
			  "with one influence set per vertex");

		// Weights sum to one. The importer normalises rather than trusting the
		// file, because a sum of 0.98 darkens a limb by two percent and reads
		// as bad lighting rather than as bad weights.
		{
			bool normalised = true;
			bool inRange = true;
			for (size_t i = 0; i < primitive.Weights.size(); i++)
			{
				const Vec4& w = primitive.Weights[i];
				normalised = normalised && std::fabs(w.x + w.y + w.z + w.w - 1.0f) < 1e-4f;

				for (int c = 0; c < 4; c++)
					inRange = inRange && primitive.Joints[i][c] < imported.Skeleton.Size();
			}

			Check(normalised, "every vertex's weights sum to one");
			Check(inRange, "and every joint index addresses a bone that exists");
		}

		// The weights actually vary along the post -- the bottom belongs to
		// bone 0 and the top to bone 1. A rig where every vertex went to one
		// bone would pass every check above and hinge instead of bending.
		{
			float lowest = 1e9f, highest = -1e9f;
			float weightAtLowest = 0.0f, weightAtHighest = 0.0f;

			for (size_t i = 0; i < primitive.Vertices.size(); i++)
			{
				const float y = primitive.Vertices[i].Position.y;
				// The influence of bone 1, whichever slot holds it.
				float upper = 0.0f;
				for (int c = 0; c < 4; c++)
				{
					if (primitive.Joints[i][c] == 1)
						upper += primitive.Weights[i][c];
				}

				if (y < lowest)  { lowest = y;  weightAtLowest = upper; }
				if (y > highest) { highest = y; weightAtHighest = upper; }
			}

			Check(weightAtLowest < 0.01f, "the bottom of the post belongs to the first bone");
			Check(weightAtHighest > 0.99f, "and the top to the second");
		}

		// The animation.
		Check(imported.Clips.size() == 1, "one clip came with it");
		if (imported.Clips.empty())
			return;

		const Anim::Clip& clip = imported.Clips[0];
		Check(clip.Duration > 1.9f && clip.Duration < 2.1f,
			  "two seconds long, taken from its last key");
		Check(clip.Tracks.size() == imported.Skeleton.Size(),
			  "with a track for every bone");
		Check(clip.Tracks[0].IsEmpty() && !clip.Tracks[1].Rotation.IsEmpty(),
			  "animating the second bone's rotation and nothing else");

		// The clip bends. Sampled at rest and at the quarter point, the tip of
		// the post must have moved -- which is the end-to-end statement that
		// import, sampling and composition agree.
		{
			Pose start, bent;
			SamplePose(imported.Skeleton, clip, 0.0f, true, start);
			SamplePose(imported.Skeleton, clip, clip.Duration * 0.25f, true, bent);

			std::vector<Mat4> a, b;
			ComposeGlobal(imported.Skeleton, start, a);
			ComposeGlobal(imported.Skeleton, bent, b);

			const Vec3 restTip = Vec3(a[1][3]);
			const Vec3 bentTip = Vec3(b[1][3]);

			// The bone's origin does not move -- it is the child that swings --
			// so the check is on its orientation instead.
			Check(Math::Length(restTip - bentTip) < 1e-5f,
				  "the animated bone's own origin stays put");

			const Vec3 restAxis = Vec3(a[1] * Vec4(0.0f, 1.0f, 0.0f, 0.0f));
			const Vec3 bentAxis = Vec3(b[1] * Vec4(0.0f, 1.0f, 0.0f, 0.0f));

			Check(Math::Length(restAxis - bentAxis) > 0.1f,
				  "and the direction it points does not");
			// The rotation is about +Z, so the axis leans towards -X.
			Check(bentAxis.x < -0.05f, "leaning the way the clip says");
		}
	}

	// The skinned vertex path.
	//
	// The check that matters is the stride. Vertex layouts are reflected from
	// the shader, and the reflection ignored a vector's width for integers --
	// so a uvec4 of joint indices came back as one uint, four bytes where the
	// buffer holds sixteen. Every attribute after it then sat at the wrong
	// offset and the mesh drew as a spray of triangles across the world.
	//
	// Nothing about that is visible in a compile, a validation layer or a draw
	// count. It is visible in a screenshot, and in this.
	void CheckSkinnedVertexLayout()
	{
		Check(sizeof(SkinnedVertex) == 64, "a skinned vertex is the size the shader expects");
		Check(offsetof(SkinnedVertex, Joints) == 32 && offsetof(SkinnedVertex, Weights) == 48,
			  "with its influences where the shader expects them");

		// Integer vectors reflect at their real width.
		Check(RHI::FormatSize(RHI::Format::R32G32B32A32_UINT) == 16 &&
			  RHI::FormatSize(RHI::Format::R32G32B32A32_SINT) == 16,
			  "a four-component integer format is sixteen bytes");
		Check(RHI::FormatSize(RHI::Format::R32G32_UINT) == 8 &&
			  RHI::FormatSize(RHI::Format::R32G32B32_UINT) == 12,
			  "and the narrower ones are not sixteen");

		// The shader itself, reflected. This is the end of the chain the bug
		// was in: whatever the enum says, what matters is the stride the
		// pipeline is built with.
		auto compiled = RHI::ShaderCompiler::CompileFromFile("assets/shaders/pbr_skinned.rvshader");
		Check(compiled.has_value(), "the skinned shader compiles");
		if (!compiled)
			return;

		const RHI::VertexLayout& layout = compiled->Reflection.VertexInput;
		Check(layout.Bindings.size() == 1, "and reflects one vertex binding");
		if (layout.Bindings.empty())
			return;

		Check(layout.Bindings[0].Stride == sizeof(SkinnedVertex),
			  "whose stride matches SkinnedVertex exactly");
		Check(layout.Attributes.size() == 5,
			  "with five attributes: position, normal, uv, joints and weights");

		// The joints specifically, since they are the ones that were wrong.
		bool joints = false;
		for (const RHI::VertexAttribute& attribute : layout.Attributes)
		{
			if (attribute.Location != 3)
				continue;

			joints = attribute.Format == RHI::Format::R32G32B32A32_UINT &&
					 attribute.Offset == offsetof(SkinnedVertex, Joints);
		}

		Check(joints, "and the joint indices reflect as four unsigned integers, not one");
	}

	// The light grid.
	//
	// The failure that matters is the CPU and the shader disagreeing about
	// which cell a point is in. Where they do, a fragment reads a neighbour's
	// light list -- which is not a crash, not a validation error, and looks
	// like lighting that snaps as the camera moves rather than like an
	// indexing bug. So the shader's formula is reproduced here and checked
	// against the function that bins the lights.
	void CheckLightGrid()
	{
		const float nearPlane = 0.05f;
		const float farPlane = 1000.0f;

		Check(LightGrid::SliceForDepth(nearPlane, nearPlane, farPlane) == 0,
			  "the near plane lands in the first depth slice");
		Check(LightGrid::SliceForDepth(farPlane, nearPlane, farPlane) == LightGrid::kSlices - 1,
			  "and the far plane in the last");
		Check(LightGrid::SliceForDepth(0.0f, nearPlane, farPlane) == 0 &&
			  LightGrid::SliceForDepth(-50.0f, nearPlane, farPlane) == 0,
			  "a depth at or behind the eye does not take a logarithm of it");
		Check(LightGrid::SliceForDepth(farPlane * 10.0f, nearPlane, farPlane) == LightGrid::kSlices - 1,
			  "and one past the far plane clamps rather than running off the grid");

		// Monotonic, and it actually spreads across the slices rather than
		// collapsing everything into one.
		{
			bool rising = true;
			uint32_t distinct = 0;
			uint32_t previous = 0;
			for (int i = 0; i <= 200; i++)
			{
				const float t = (float)i / 200.0f;
				const float depth = nearPlane * std::pow(farPlane / nearPlane, t);
				const uint32_t slice = LightGrid::SliceForDepth(depth, nearPlane, farPlane);

				rising = rising && slice >= previous;
				if (i == 0 || slice != previous)
					distinct++;
				previous = slice;
			}

			Check(rising, "slices never go backwards as depth increases");
			Check(distinct == LightGrid::kSlices,
				  "and a sweep from near to far visits every one of them");
		}

		// The shader computes the slice as log(z) * scale + bias, with those
		// two arriving in a uniform. If that disagrees with the binning above,
		// the lights are in the wrong cells.
		{
			const float scale = LightGrid::SliceScale(nearPlane, farPlane);
			const float bias = LightGrid::SliceBias(nearPlane, farPlane);

			bool agrees = true;
			for (int i = 1; i <= 200; i++)
			{
				const float t = (float)i / 200.0f;
				const float depth = nearPlane * std::pow(farPlane / nearPlane, t);

				const uint32_t fromShader = (uint32_t)std::clamp(
					(int)(std::log(depth) * scale + bias), 0, (int)LightGrid::kSlices - 1);

				agrees = agrees && fromShader == LightGrid::SliceForDepth(depth, nearPlane, farPlane);
			}

			Check(agrees, "the shader's scale and bias reproduce the binning exactly");
		}

		// Binning. A camera at the origin looking down -Z, as everywhere else.
		{
			const Mat4 projection =
				Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, nearPlane, farPlane);
			Camera camera(projection);

			float derivedNear = 0.0f, derivedFar = 0.0f;
			LightGrid::DepthRangeOf(projection, derivedNear, derivedFar);

			Check(std::fabs(derivedNear - nearPlane) < 1e-4f,
				  "the near plane comes back out of the projection matrix exactly");

			// The far plane does not, and cannot.
			//
			// It is recovered from P[2][2] + 1, and with a far-to-near ratio of
			// twenty thousand P[2][2] is -1.00005 -- so the addition cancels
			// almost every significant bit a float has and 1000 comes back as
			// 1001.08. A tenth of a percent, and inherent rather than fixable.
			//
			// It does not matter, and the reason is worth stating: the shader
			// is handed the slice scale and bias computed from this same
			// derived value, so the two sides agree with each other exactly
			// whatever the arithmetic did. Nothing compares it against the
			// number the camera was built with.
			Check(std::fabs(derivedFar - farPlane) / farPlane < 0.01f,
				  "and the far plane to within a fraction of a percent");

			LightGrid grid;

			// One small light straight ahead reaches some cells and not most.
			LightList one;
			LightRenderData light;
			light.Type = Light::LightType::Point;
			light.Position = { 0.0f, 0.0f, -20.0f };
			light.Range = 3.0f;
			one.push_back(light);

			grid.Build(camera, Mat4(1.0f), one, 0);

			uint32_t occupied = 0;
			for (const LightGrid::Cell& cell : grid.Cells())
				occupied += cell.Count > 0 ? 1 : 0;

			Check(occupied > 0, "a light in front of the camera lands in the grid");
			Check(occupied < LightGrid::kCellCount / 2,
				  "and a small one does not land in half of it");
			Check(grid.MaxCellLoad() == 1, "with one light in the busiest cell");

			// Behind the camera: no cell at all.
			LightList behind;
			LightRenderData back = light;
			back.Position = { 0.0f, 0.0f, 200.0f };
			behind.push_back(back);

			grid.Build(camera, Mat4(1.0f), behind, 0);
			Check(grid.Indices().empty(), "a light behind the camera lands in none of it");

			// Directional lights are skipped rather than binned: they reach
			// everything, so binning them would put a copy in every cell.
			LightList sun;
			LightRenderData directional;
			directional.Type = Light::LightType::Directional;
			sun.push_back(directional);
			sun.push_back(light);

			grid.Build(camera, Mat4(1.0f), sun, 1);
			Check(grid.MaxCellLoad() == 1,
				  "the directional light is not binned, only the point light");

			bool indicesPointPastTheSun = true;
			for (uint32_t index : grid.Indices())
				indicesPointPastTheSun = indicesPointPastTheSun && index == 1;
			Check(indicesPointPastTheSun,
				  "and the indices address the light buffer, not the positional subset");

			// A light that reaches the whole scene is in every cell, which is
			// the case clustering cannot help and is worth knowing is handled
			// rather than mis-binned.
			LightList huge;
			LightRenderData big = light;
			big.Range = 10000.0f;
			huge.push_back(big);

			grid.Build(camera, Mat4(1.0f), huge, 0);
			Check(grid.MaxCellLoad() == 1 && grid.Indices().size() > LightGrid::kCellCount / 2,
				  "a light reaching everything is binned into nearly every cell");
		}
	}

	// Frustum culling.
	//
	// The failure that matters is asymmetric: drawing something invisible costs
	// a draw, and skipping something visible costs a hole in the picture. So
	// every check here is about *not* culling something that should be drawn,
	// and only the last is about culling what should not.
	void CheckFrustumCulling()
	{
		const Mat4 projection = Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
		// At the origin looking down -Z, which is where a camera with an
		// identity transform points.
		const Frustum frustum(projection * Math::Inverse(Mat4(1.0f)));

		const Vec3 unit(0.5f);

		Check(frustum.Intersects(Vec3(0.0f, 0.0f, -10.0f), unit),
			  "a box in front of the camera is drawn");
		Check(!frustum.Intersects(Vec3(0.0f, 0.0f, 10.0f), unit),
			  "one behind it is not");
		Check(!frustum.Intersects(Vec3(100.0f, 0.0f, -10.0f), unit),
			  "one far to the side is not");
		Check(!frustum.Intersects(Vec3(0.0f, 0.0f, -500.0f), unit),
			  "and one beyond the far plane is not");

		// The near plane is z = 0, not z = -w: glm is built with
		// GLM_FORCE_DEPTH_ZERO_TO_ONE. Building it the OpenGL way puts the
		// plane half the frustum away and culls things that are plainly in
		// view, which is the mistake this check exists for.
		Check(frustum.Intersects(Vec3(0.0f, 0.0f, -0.3f), Vec3(0.05f)),
			  "something just past the near plane is drawn, not culled");

		// Conservative: straddling counts as inside. A box half out of view is
		// half in it.
		Check(frustum.Intersects(Vec3(0.0f, 0.0f, -10.0f), Vec3(50.0f)),
			  "a box straddling the frustum is drawn");

		// A shadow cascade's frustum is orthographic and points somewhere else
		// entirely. Culling has to work against whatever matrix it is given.
		{
			const Mat4 ortho = Math::Orthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f, 50.0f);
			const Mat4 view = Math::LookAt(Vec3(0.0f, 20.0f, 0.0f),
											   Vec3(0.0f), Vec3(0.0f, 0.0f, 1.0f));
			const Frustum cascade(ortho * view);

			Check(cascade.Intersects(Vec3(0.0f), unit),
				  "an orthographic frustum contains what is under it");
			Check(!cascade.Intersects(Vec3(60.0f, 0.0f, 0.0f), unit),
				  "and not what is well outside it");
		}

		// Transformed bounds. A rotated box's axis-aligned bound grows, and has
		// to -- shrinking it would cull something still on screen.
		{
			AABB box;
			box.Min = Vec3(-0.5f);
			box.Max = Vec3(0.5f);

			Vec3 centre, extents;
			Frustum::TransformBounds(box, Mat4(1.0f), centre, extents);
			Check(Math::Length(centre) < 1e-5f && std::fabs(extents.x - 0.5f) < 1e-5f,
				  "an untransformed box keeps its own bounds");

			const Mat4 turned = Math::Rotate(Mat4(1.0f), Math::Radians(45.0f),
												 Vec3(0.0f, 1.0f, 0.0f));
			Frustum::TransformBounds(box, turned, centre, extents);
			Check(extents.x > 0.69f && extents.x < 0.72f,
				  "and a box turned 45 degrees grows to contain itself");

			const Mat4 moved = Math::Translate(Mat4(1.0f), Vec3(3.0f, 0.0f, 0.0f));
			Frustum::TransformBounds(box, moved, centre, extents);
			Check(std::fabs(centre.x - 3.0f) < 1e-5f && std::fabs(extents.x - 0.5f) < 1e-5f,
				  "moving a box moves its bounds and does not grow them");

			const Mat4 scaled = Math::Scale(Mat4(1.0f), Vec3(4.0f));
			Frustum::TransformBounds(box, scaled, centre, extents);
			Check(std::fabs(extents.x - 2.0f) < 1e-5f, "and scaling scales them");
		}
	}

	// The environment BRDF table.
	//
	// A table of a function that has known values at its corners, which is a
	// rare luxury: most of what a renderer computes can only be checked against
	// itself. These are the analytic ones.
	void CheckEnvironmentBRDF()
	{
		constexpr uint32_t kSize = 64;
		const std::vector<float> table = IntegrateEnvironmentBRDF(kSize, 256);

		Check(table.size() == (size_t)kSize * kSize * 2, "the BRDF table has two channels");

		auto at = [&](uint32_t x, uint32_t y)
		{
			return Vec2(table[((size_t)y * kSize + x) * 2 + 0],
							 table[((size_t)y * kSize + x) * 2 + 1]);
		};

		// A mirror seen head on reflects exactly F0: scale one, bias nothing.
		// This is the corner where the integral has a closed form, and it fails
		// if the geometry term, the importance sampling or the Fresnel split is
		// wrong.
		{
			const Vec2 mirror = at(kSize - 1, 0);
			Check(std::fabs(mirror.x - 1.0f) < 0.02f && mirror.y < 0.02f,
				  "a smooth surface seen head on reflects its F0 and nothing more");
		}

		// Energy: for a fully reflective material, F0 = 1, the answer is
		// scale + bias -- and no surface reflects more than arrives.
		{
			bool conserving = true;
			bool positive = true;
			for (uint32_t y = 0; y < kSize; y++)
			{
				for (uint32_t x = 0; x < kSize; x++)
				{
					const Vec2 value = at(x, y);
					conserving = conserving && (value.x + value.y) <= 1.02f;
					positive = positive && value.x >= 0.0f && value.y >= 0.0f;
				}
			}

			Check(conserving, "and no entry reflects more light than reached it");
			Check(positive, "with nothing negative anywhere in the table");
		}

		// Fresnel: at a grazing angle everything becomes a mirror, so the bias
		// term -- the part that does not depend on F0 -- rises as the surface
		// turns away. Checked on a rough row, where the effect is largest.
		{
			const uint32_t rough = kSize / 2;
			Check(at(0, rough).y > at(kSize - 1, rough).y,
				  "and grazing angles carry more of the response than head-on ones");
		}

		// Degenerate requests are clamped rather than refused: a zero-sized
		// table would be a texture nothing could sample.
		Check(!IntegrateEnvironmentBRDF(0, 0).empty(),
			  "a table of no size is clamped to one that works");
	}

	// Reflection probes.
	//
	// The capture basis is the whole thing worth checking, and it cannot be
	// checked by looking: a mirrored face, a face rotated by 90 degrees and an
	// upside-down one all produce a reflection that moves correctly with the
	// camera and is simply wrong. So each face's basis is compared against the
	// cube-map face table it has to agree with -- if the capture and the
	// sampling disagree about which direction a texel is, everything else in
	// the feature is decoration.
	void CheckReflectionProbe()
	{
		const Vec3 origin(3.0f, -2.0f, 7.0f);

		bool centred = true;
		bool positioned = true;
		bool verticalMatches = true;
		bool horizontalMatches = true;

		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
		{
			const Mat4 transform = ReflectionProbe::FaceTransform(face, origin);

			// A camera looks down its own -Z, here and everywhere else.
			const Vec3 forward = Math::Normalize(Vec3(transform * Vec4(0, 0, -1, 0)));
			const Vec3 up = Math::Normalize(Vec3(transform * Vec4(0, 1, 0, 0)));
			const Vec3 right = Math::Normalize(Vec3(transform * Vec4(1, 0, 0, 0)));

			positioned = positioned && Math::Length(Vec3(transform[3]) - origin) < 1e-5f;

			// The middle of a face image looks down that face's axis.
			centred = centred &&
					  Math::Length(forward - CubeFaceDirection(face, 0.5f, 0.5f)) < 1e-5f;

			// The top row of a face image is the camera's *down*, not its up:
			// a face is captured with the basis every cube-map tutorial uses,
			// which stores the first texel row at the bottom of the rendered
			// image. CopyToTextureLayer is what makes that true on both
			// backends -- this is the assertion it exists to satisfy.
			//
			// The frustum is 90 degrees, so the edge of the image is exactly
			// one unit of up per unit of forward.
			const Vec3 top = Math::Normalize(forward - up);
			verticalMatches = verticalMatches &&
							  Math::Length(top - CubeFaceDirection(face, 0.5f, 0.0f)) < 1e-5f;

			// Horizontally nothing is flipped, so the right of the image is the
			// camera's right. A cube map is addressed left-handed, which is why
			// this does not simply fall out and is worth stating.
			const Vec3 edge = Math::Normalize(forward + right);
			horizontalMatches = horizontalMatches &&
								Math::Length(edge - CubeFaceDirection(face, 1.0f, 0.5f)) < 1e-5f;
		}

		Check(positioned, "a probe's faces are all captured from its own position");
		Check(centred, "each face looks down the axis the cube-map table gives it");
		Check(verticalMatches, "and its top row is the direction that table's v = 0 is");
		Check(horizontalMatches, "and its right column the direction u = 1 is");

		// A 90-degree square frustum: six of them tile the sphere of directions
		// exactly, which is the reason a cube map is six squares.
		{
			const Mat4 projection = ReflectionProbe::FaceProjection(0.1f, 50.0f);
			const Vec4 corner = projection * Vec4(1.0f, 1.0f, -1.0f, 1.0f);
			Check(std::fabs(corner.x / corner.w - 1.0f) < 1e-4f &&
				  std::fabs(corner.y / corner.w - 1.0f) < 1e-4f,
				  "the face frustum is exactly 90 degrees and square");
		}

		if (!Renderer::HasDevice())
			return;

		ReflectionProbe probe(Renderer::GetDevice(), 64);
		Check(probe.GetFaceSize() == 64, "a probe allocates the face size asked for");
		Check(probe.GetCube() != nullptr, "with a cube behind it");
		Check(!probe.IsComplete(), "and reports itself unusable until it has been captured");

		if (probe.GetCube())
		{
			Check(probe.GetCube()->GetDesc().Type == RHI::TextureType::TextureCube,
				  "which the RHI knows is a cube");
			// The scene renders in linear HDR, and a capture that stored
			// anything else would clip the values bloom and the tone curve are
			// about to read.
			Check(probe.GetCube()->GetFormat() == RHI::Format::R16G16B16A16_SFLOAT,
				  "in the same format the scene renders into");
		}

		// Below the floor, so the clamp has something to do.
		ReflectionProbe tiny(Renderer::GetDevice(), 1);
		Check(tiny.GetFaceSize() >= 8, "and a face size of one is clamped to something renderable");
	}

	// The sky.
	//
	// The matrix is the whole feature: everything else is a colour lookup. A
	// sky that is mirrored, upside down, or rotating with the camera instead of
	// staying put all render perfectly happily, so each one is asked about
	// directly.
	void CheckSky()
	{
		Check(Skybox::IsReady(), "the sky shader compiled");

		const Mat4 projection = Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

		auto direction = [](const Mat4& matrix, float x, float y)
		{
			const Vec4 point = matrix * Vec4(x, y, 1.0f, 1.0f);
			return Math::Normalize(Vec3(point) / point.w);
		};

		// --- a camera at rest ---------------------------------------------------
		const Mat4 rest = Skybox::BuildDirectionMatrix(projection, Mat4(1.0f), 0.0f);

		Check(Math::Length(direction(rest, 0.0f, 0.0f) - Vec3(0.0f, 0.0f, -1.0f)) < 1e-4f,
			  "the centre of the screen looks along the camera's forward axis");
		Check(direction(rest, 0.0f, 0.9f).y > 0.3f, "the top of the screen is up");
		Check(direction(rest, 0.9f, 0.0f).x > 0.3f, "and the right of it is right");

		// --- moving does not move the sky ----------------------------------------
		// The one property that distinguishes a sky from a very large box.
		{
			const Mat4 moved = Math::Translate(Mat4(1.0f), Vec3(120.0f, -40.0f, 75.0f));
			const Mat4 elsewhere = Skybox::BuildDirectionMatrix(projection, moved, 0.0f);

			bool same = true;
			for (float y = -0.9f; y <= 0.9f; y += 0.6f)
			{
				for (float x = -0.9f; x <= 0.9f; x += 0.6f)
					same = same && Math::Length(direction(rest, x, y) - direction(elsewhere, x, y)) < 1e-4f;
			}
			Check(same, "walking the camera across the world does not move the sky");
		}

		// --- turning the camera does ---------------------------------------------
		{
			const Mat4 turned = Math::Rotate(Mat4(1.0f), Math::HalfPi,
												 Vec3(0.0f, 1.0f, 0.0f));
			const Mat4 matrix = Skybox::BuildDirectionMatrix(projection, turned, 0.0f);
			Check(Math::Length(direction(matrix, 0.0f, 0.0f) - Vec3(-1.0f, 0.0f, 0.0f)) < 1e-4f,
				  "turning the camera a quarter turn left points it at -X");
		}

		// --- and so does turning the sky ------------------------------------------
		{
			const Mat4 spun = Skybox::BuildDirectionMatrix(projection, Mat4(1.0f),
															   Math::HalfPi);
			Check(Math::Length(direction(spun, 0.0f, 0.0f) - Vec3(-1.0f, 0.0f, 0.0f)) < 1e-4f,
				  "and rotating the sky itself turns it the same way, not the opposite one");
		}

		// --- what the mode means --------------------------------------------------
		// A cubemap sky with no cubemap must not paint the frame black. Draw
		// is a no-op without a command list, so this is checked by asking it to
		// run outside a frame: what is being verified is that it declines
		// rather than dereferencing the null texture.
		SceneEnvironment environment;
		environment.Sky = SkyType::Cubemap;
		environment.SkyTexture = UUID::Invalid();
		Skybox::Draw(Camera(projection), Mat4(1.0f), environment, nullptr);
		Check(true, "a cubemap sky with no cubemap is survivable");

		environment.Sky = SkyType::Color;
		Skybox::Draw(Camera(projection), Mat4(1.0f), environment, nullptr);
		Check(true, "and a colour background draws nothing at all");
	}

	void CheckFrameGraph()
	{
		if (!Renderer::HasDevice())
			return;

		RenderGraph graph(Renderer::GetDevice());

		auto build = [&](uint32_t width, uint32_t height, const SceneEnvironment& environment)
		{
			graph.Begin(width, height);

			FrameDesc frame;
			frame.Output = graph.Backbuffer();
			frame.Width = width;
			frame.Height = height;
			frame.Environment = environment;
			frame.OutputFormat = RHI::Format::R8G8B8A8_UNORM;
			frame.DrawScene = [](RGPassContext&) {};

			BuildFrame(graph, frame);
			return graph.Compile();
		};

		auto hasPass = [&](const char* name)
		{
			for (size_t i = 0; i < graph.GetPassCount(); i++)
			{
				if (graph.GetPassName(i).find(name) != std::string::npos)
					return true;
			}
			return false;
		};

		SceneEnvironment environment;

		// --- the full chain -----------------------------------------------------
		Check(build(1600, 900, environment), "the standard frame compiles");
		Check(hasPass("Scene"), "it draws the scene");
		Check(hasPass("Bloom prefilter"), "thresholds for bloom");
		Check(hasPass("Bloom down"), "downsamples");
		Check(hasPass("Bloom up"), "and blurs back up");
		Check(hasPass("Tonemap"), "tone maps");
		Check(hasPass("FXAA"), "and anti-aliases");

		const size_t withEverything = graph.GetPassCount();

		// --- bloom off ----------------------------------------------------------
		environment.BloomEnabled = false;
		Check(build(1600, 900, environment), "with bloom off it still compiles");
		Check(!hasPass("Bloom"), "and has no bloom passes at all");
		Check(hasPass("Tonemap"), "but still tone maps, because that is not optional");
		Check(graph.GetPassCount() < withEverything, "so the frame is shorter");

		// --- anti-aliasing off ---------------------------------------------------
		environment.BloomEnabled = true;
		environment.AA = AntiAliasing::None;
		Check(build(1600, 900, environment), "with anti-aliasing off it compiles");
		Check(!hasPass("FXAA"), "and skips the FXAA pass");
		// Without FXAA the tonemap has to land in the output directly, rather
		// than in an intermediate nobody then presents.
		Check(hasPass("Tonemap"), "with tone mapping writing the output itself");

		environment.AA = AntiAliasing::FXAA;

		// --- a tiny frame --------------------------------------------------------
		// The chain has to stop before the levels are a handful of texels,
		// where the filters stop meaning anything and a zero-sized target is
		// one division away.
		Check(build(64, 64, environment), "a small frame compiles");
		const size_t smallPasses = graph.GetPassCount();
		Check(build(1600, 900, environment), "a large one compiles");
		Check(graph.GetPassCount() > smallPasses,
			  "and has more bloom levels than the small one");

		// --- degenerate sizes -----------------------------------------------------
		graph.Begin(0, 0);
		{
			FrameDesc frame;
			frame.Output = graph.Backbuffer();
			frame.Width = 0;
			frame.Height = 0;
			frame.DrawScene = [](RGPassContext&) {};
			BuildFrame(graph, frame);
		}
		Check(!graph.Compile(), "a zero-sized frame describes nothing and is refused");
	}

	// Packaging.
	//
	// Checked by packaging a throwaway project and looking at what came out,
	// rather than by asserting the function returned true. The failures that
	// matter here are all "it built something that does not work": a missing
	// sidecar, a project file still pointing at the source layout, a start
	// scene left behind.
	void CheckPackaging()
	{
		const std::filesystem::path previous = Project::File();
		const std::filesystem::path root =
			ScratchDir("package-test");
		const std::filesystem::path output = root / "out";

		std::error_code error;
		std::filesystem::remove_all(root, error);

		// A project with one asset and a start scene, built from nothing.
		Check(Project::Create(root / "src", "Packaged"), "a project to package");
		Assets::Registry::Init(Project::AssetRoot());

		std::filesystem::create_directories(Project::AssetRoot() / "scenes", error);

		{
			auto scene = std::make_shared<Scene>();
			Entity camera = scene->CreateEntity("Camera");
			camera.AddComponent<CameraComponent>();
			Entity cube = scene->CreateEntity("Cube");
			cube.AddComponent<MeshComponent>(PrimitiveType::Cube);

			SceneSerializer serializer(scene);
			serializer.Serialize((Project::AssetRoot() / "scenes" / "main.rage").string());
		}
		Assets::Registry::Refresh();

		// Stand-ins for the runtime and the engine's own assets. Packaging is
		// file copying, so what it copies matters and what produced the file
		// does not.
		const std::filesystem::path fakeRuntime = root / "bin" / "RageVRuntime.exe";
		std::filesystem::create_directories(fakeRuntime.parent_path(), error);
		{ std::ofstream stream(fakeRuntime); stream << "not really an executable"; }

		const std::filesystem::path fakeAssets = root / "bin" / "assets";
		std::filesystem::create_directories(fakeAssets / "shaders", error);
		{ std::ofstream stream(fakeAssets / "shaders" / "pbr.rvshader"); stream << "shader"; }

		PackageDesc desc;
		desc.OutputDirectory = output;
		desc.RuntimeExecutable = fakeRuntime;
		desc.EngineAssets = fakeAssets;

		// --- refused before anything is written --------------------------------
		Project::Config().StartScene.clear();
		PackageResult result = PackageProject(desc);
		Check(!result.Success, "a project with no start scene will not package");
		Check(!std::filesystem::exists(output),
			  "and nothing is written, so a refused build leaves no half-made game");

		Project::Config().StartScene = "scenes/missing.rage";
		Check(!PackageProject(desc).Success, "nor one whose start scene does not exist");

		// The engine is a DLL, so a package without it is an executable that
		// cannot start -- and it would not be found out here, but on the first
		// machine that is not the one it was built on.
		Project::Config().StartScene = "scenes/main.rage";
		Check(!PackageProject(desc).Success, "nor one with no engine DLL beside the runtime");
		Check(!std::filesystem::exists(output),
			  "and that one is refused before anything is written too");

		{ std::ofstream stream(fakeRuntime.parent_path() / "RageV.dll"); stream << "not really a DLL"; }

		// A built game module, which the package must carry: its scripts are
		// as much the game as the assets are.
		std::filesystem::create_directories(
			ModuleBuild::ModuleFor(root / "src", "Packaged").parent_path(), error);
		{ std::ofstream stream(ModuleBuild::ModuleFor(root / "src", "Packaged")); stream << "module"; }

		// --- the real thing ----------------------------------------------------
		result = PackageProject(desc);
		Check(result.Success, "a complete project packages");

		Check(std::filesystem::exists(output / "RageV.dll"),
			  "the engine ships with it, under the name its import table asks for");
		Check(std::filesystem::exists(output / "Packaged.dll"),
			  "and so does the game module, beside the .rvproject where "
			  "GameModule looks in a package");

		Check(std::filesystem::exists(output / "Packaged.exe"),
			  "the executable is named after the game, not after the engine");
		Check(std::filesystem::exists(output / "Packaged.rvproject"),
			  "a project file ships beside it");
		Check(std::filesystem::exists(output / "assets" / "shaders" / "pbr.rvshader"),
			  "engine assets ship, or the renderer cannot start");
		Check(std::filesystem::exists(output / "content" / "scenes" / "main.rage"),
			  "and the project's assets, under content/");
		Check(std::filesystem::exists(output / "ragev.ini"),
			  "with a config file the player can edit");

		// The sidecar is the identity: a scene refers to an asset by handle and
		// the handle lives in the .meta. A build without them is a build where
		// every reference resolves to nothing.
		Check(std::filesystem::exists(output / "content" / "scenes" / "main.rage.meta"),
			  "the .meta sidecars ship, because they are the asset identity");

		// --- the shipped project file must describe the shipped layout ---------
		{
			auto packaged = std::make_shared<Scene>();
			const YAML::Node file = YAML::LoadFile((output / "Packaged.rvproject").string());
			const YAML::Node& node = file;

			Check(node["Project"]["AssetDirectory"].as<std::string>() == "content",
				  "the shipped project points at content/, not the source layout");
			Check(node["Project"]["StartScene"].as<std::string>() == "scenes/main.rage",
				  "and keeps the start scene");
		}

		// --- refuses to build over something ------------------------------------
		Check(!PackageProject(desc).Success,
			  "packaging into a directory that already has files is refused");

		desc.Overwrite = true;
		Check(PackageProject(desc).Success, "unless overwrite is asked for");

		std::filesystem::remove_all(root, error);

		if (!previous.empty() && Project::Load(previous))
			Assets::Registry::Init(Project::AssetRoot());
	}

	// What the standalone runtime does, without the window.
	//
	// The runtime is three steps -- open the project, load its start scene,
	// run it -- and each of them is a place a shipped game silently shows an
	// empty window. Checking them here means the failure is a red test rather
	// than a bug report from someone who cannot see a log.
	void CheckRuntimePath()
	{
		Check(Project::GetActive() != nullptr, "a project is open");
		if (!Project::GetActive())
			return;

		const std::string& start = Project::Config().StartScene;
		Check(!start.empty(), "the project names a start scene");

		const std::filesystem::path scenePath = Project::AssetPath(start);
		Check(std::filesystem::exists(scenePath), "which exists on disk");
		if (!std::filesystem::exists(scenePath))
			return;

		auto scene = std::make_shared<Scene>();
		SceneSerializer serializer(scene);
		Check(serializer.Deserialize(scenePath.string()), "and loads");

		Check(scene->GetRegistry().view<IDComponent>().size() > 0,
			  "into a scene with something in it");
		Check((bool)scene->GetPrimaryCameraEntity(),
			  "including a camera, without which a game renders nothing");

		// A runtime starts running: there is no Play button, and this is most
		// of what separates it from the editor.
		scene->OnRuntimeStart();
		Check(scene->GetPhysics() != nullptr, "starting it builds the physics world");

		constexpr float dt = 1.0f / 60.0f;
		for (int i = 0; i < 30; i++)
			scene->OnFixedUpdateRuntime(dt);
		scene->OnUpdateRuntime(dt);

		Check(scene->GetPhysics()->GetBodyCount() > 0, "with bodies in it");

		scene->OnRuntimeStop();
	}

	// Clicking in the viewport.
	//
	// Worth testing rather than trying by hand, because every failure mode here
	// looks like "picking is slightly wrong" and they are hard to tell apart by
	// eye: a flipped y axis is only visible off the centre line, a missing
	// inverse only when something is rotated, and an ignored scale only when
	// something is not unit-sized.
	void CheckPicking()
	{
		// A camera at +Z looking back at the origin, which is the convention
		// everything else in the engine uses.
		EditorCamera camera(45.0f, 1.0f, 0.1f, 1000.0f);
		const Mat4 cameraTransform = Math::Translate(Mat4(1.0f), { 0.0f, 0.0f, 10.0f });

		// --- the ray itself ----------------------------------------------------
		const Ray centre = ScreenPointToRay(camera, cameraTransform, { 0.0f, 0.0f });
		Check(centre.Direction.z < -0.99f, "the centre of the screen looks straight forward");

		const Ray right = ScreenPointToRay(camera, cameraTransform, { 0.9f, 0.0f });
		Check(right.Direction.x > 0.05f, "the right of the screen looks right");

		// The y flip is the classic one: a window's origin is top-left and clip
		// space's is bottom-left, and getting it wrong is invisible along the
		// horizontal centre line.
		const Ray top = ScreenPointToRay(camera, cameraTransform, { 0.0f, 0.9f });
		Check(top.Direction.y > 0.05f, "and the top of the screen looks up");

		// --- boxes -------------------------------------------------------------
		{
			Ray ray;
			ray.Origin = { 0.0f, 0.0f, 10.0f };
			ray.Direction = { 0.0f, 0.0f, -1.0f };

			float distance = 0.0f;
			Check(RayIntersectsBox(ray, { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, distance) &&
				  std::fabs(distance - 9.0f) < 1e-3f,
				  "a ray hits a box at its near face");

			// Behind the ray is a miss, not a hit at a negative distance.
			ray.Direction = { 0.0f, 0.0f, 1.0f };
			Check(!RayIntersectsBox(ray, { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, distance),
				  "and misses one behind it");

			// Parallel to two slabs at once, which is where a per-axis branch
			// on a zero direction component gets it wrong.
			ray.Origin = { 5.0f, 0.0f, 0.0f };
			ray.Direction = { -1.0f, 0.0f, 0.0f };
			Check(RayIntersectsBox(ray, { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, distance),
				  "a ray exactly parallel to two axes still hits");
		}

		// --- picking a scene ---------------------------------------------------
		auto scene = std::make_shared<Scene>();

		Entity front = scene->CreateEntity("Front");
		front.AddComponent<MeshComponent>(PrimitiveType::Cube);
		front.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 2.0f };

		Entity behind = scene->CreateEntity("Behind");
		behind.AddComponent<MeshComponent>(PrimitiveType::Cube);
		behind.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, -4.0f };

		Entity aside = scene->CreateEntity("Aside");
		aside.AddComponent<MeshComponent>(PrimitiveType::Cube);
		aside.GetComponent<TransformComponent>().Position = { 6.0f, 0.0f, 0.0f };

		Ray down;
		down.Origin = { 0.0f, 0.0f, 10.0f };
		down.Direction = { 0.0f, 0.0f, -1.0f };

		PickResult hit = PickEntity(*scene, down);
		Check((bool)hit, "a ray through two objects picks one");
		Check(hit.Entity == front, "and it is the nearer of them");

		// Empty space picks nothing rather than the closest thing to the ray.
		Ray miss;
		miss.Origin = { 0.0f, 40.0f, 10.0f };
		miss.Direction = { 0.0f, 0.0f, -1.0f };
		Check(!PickEntity(*scene, miss), "a ray through empty space picks nothing");

		// The cube's own geometry, not its bounding box: a ray through the gap
		// beside it must miss even though it passes near.
		Ray beside;
		beside.Origin = { 0.9f, 0.9f, 10.0f };
		beside.Direction = { 0.0f, 0.0f, -1.0f };
		Check(!PickEntity(*scene, beside),
			  "a ray just outside a mesh misses rather than hitting its bounds");

		// Scale has to reach the test, or a scaled-up object is pickable only
		// over its original size.
		aside.GetComponent<TransformComponent>().Scale = Vec3(8.0f);
		Ray atAside;
		atAside.Origin = { 6.0f, 3.0f, 10.0f };
		atAside.Direction = { 0.0f, 0.0f, -1.0f };
		Check(PickEntity(*scene, atAside).Entity == aside,
			  "a scaled object is pickable over its scaled size");

		// A trigger volume has no mesh at all, and finding it in the hierarchy
		// is the only alternative to picking it by its collider.
		Entity trigger = scene->CreateEntity("Trigger");
		trigger.GetComponent<TransformComponent>().Position = { -6.0f, 0.0f, 0.0f };
		auto& collider = trigger.AddComponent<ColliderComponent>();
		collider.HalfExtents = { 2.0f, 2.0f, 2.0f };
		collider.IsTrigger = true;

		Ray atTrigger;
		atTrigger.Origin = { -6.0f, 0.0f, 10.0f };
		atTrigger.Direction = { 0.0f, 0.0f, -1.0f };
		Check(PickEntity(*scene, atTrigger).Entity == trigger,
			  "an entity with only a collider is still selectable");

		// The hit point is on the surface, which is what a future "place at
		// cursor" would be built on.
		hit = PickEntity(*scene, down);
		Check(std::fabs(hit.Point.z - 2.5f) < 0.05f, "the hit lands on the surface it struck");
	}

	// The collider overlay.
	//
	// Checked by counting the lines it emits, which is the part that can be
	// wrong without anyone noticing: a shape silently drawing nothing looks
	// exactly like a scene with no collider in it. Whether it looks right is
	// not something a test can answer, but whether it drew is.
	void CheckColliderOverlay()
	{
		auto scene = std::make_shared<Scene>();

		// A camera to draw through. The overlay never reaches a command list
		// here -- there is no frame -- so it accumulates lines and is measured
		// before EndScene throws them away.
		EditorCamera camera(45.0f, 1.6f, 0.1f, 1000.0f);

		auto lineCountFor = [&](ColliderShape shape)
		{
			auto probe = std::make_shared<Scene>();
			Entity entity = probe->CreateEntity("Collider");
			entity.AddComponent<ColliderComponent>(shape);

			DebugRenderer::BeginScene(camera, camera.GetTransform());
			Physics::DrawColliders(*probe);
			const uint32_t lines = DebugRenderer::GetLineCount();
			DebugRenderer::EndScene();
			return lines;
		};

		Check(lineCountFor(ColliderShape::Box) == 12, "a box collider draws its twelve edges");
		Check(lineCountFor(ColliderShape::Sphere) == 72,
			  "a sphere draws three rings rather than a lat/long mesh");
		Check(lineCountFor(ColliderShape::Capsule) == 100,
			  "a capsule draws two rings, four struts and four cap arcs");

		// Nothing to draw is not the same as failing to draw.
		DebugRenderer::BeginScene(camera, camera.GetTransform());
		Physics::DrawColliders(*scene);
		Check(DebugRenderer::GetLineCount() == 0, "a scene with no colliders draws nothing");
		DebugRenderer::EndScene();

		// An entity with a collider but no rigid body still draws: a collider
		// is scene geometry whether or not anything simulates it.
		Entity bare = scene->CreateEntity("Bare");
		bare.AddComponent<ColliderComponent>();
		DebugRenderer::BeginScene(camera, camera.GetTransform());
		Physics::DrawColliders(*scene);
		Check(DebugRenderer::GetLineCount() == 12, "a collider with no rigid body still draws");
		DebugRenderer::EndScene();

		// The overlay is frustum culled, like the meshes it annotates.
		//
		// Checked by the line count rather than by looking, because a shape
		// drawn a kilometre off screen and a shape not drawn at all produce the
		// same picture -- which is why this was the last thing in the renderer
		// still submitting whatever the registry held.
		{
			auto far_away = std::make_shared<Scene>();
			Entity behind = far_away->CreateEntity("Behind the camera");
			behind.AddComponent<ColliderComponent>();
			behind.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 4000.0f };

			DebugRenderer::BeginScene(camera, camera.GetTransform());
			Physics::DrawColliders(*far_away);
			const uint32_t lines = DebugRenderer::GetLineCount();
			const uint32_t culled = DebugRenderer::GetCulledCount();
			DebugRenderer::EndScene();

			Check(lines == 0 && culled == 1, "a collider outside the frustum is not submitted");
		}

		// And the other direction, which is the one that costs a hole in the
		// picture rather than a wasted draw.
		{
			auto visible = std::make_shared<Scene>();
			Entity front = visible->CreateEntity("In view");
			front.AddComponent<ColliderComponent>();

			DebugRenderer::BeginScene(camera, camera.GetTransform());
			Physics::DrawColliders(*visible);
			const uint32_t lines = DebugRenderer::GetLineCount();
			const uint32_t culled = DebugRenderer::GetCulledCount();
			DebugRenderer::EndScene();

			Check(lines == 12 && culled == 0, "and one inside it still is");
		}

		// Outside a scene there is no frustum, so nothing may be dropped for
		// being outside one. Debug draw is called from wherever a question is
		// being asked, and must not need a camera to have been set up first.
		Check(DebugRenderer::GetCulledCount() == 0,
			  "with no scene begun, nothing is culled");

		// The overlay must describe what is simulated, so the sizing has to be
		// the same function the shape is built from.
		{
			ColliderComponent box;
			box.HalfExtents = { 0.5f, 0.5f, 0.5f };
			const ScaledCollider sized = Physics::ScaleCollider(box, { 2.0f, 3.0f, 4.0f });
			Check(std::fabs(sized.HalfExtents.x - 1.0f) < 1e-5f &&
				  std::fabs(sized.HalfExtents.y - 1.5f) < 1e-5f &&
				  std::fabs(sized.HalfExtents.z - 2.0f) < 1e-5f,
				  "a box collider takes the entity's scale per axis");

			ColliderComponent sphere(ColliderShape::Sphere);
			sphere.Radius = 1.0f;
			// A sphere has one radius, so the largest axis wins -- enclosing
			// the mesh rather than cutting into it.
			Check(std::fabs(Physics::ScaleCollider(sphere, { 2.0f, 3.0f, 1.0f }).Radius - 3.0f) < 1e-5f,
				  "a sphere takes the largest axis, so it encloses rather than clips");

			// A mirrored entity has a size, not a negative size.
			Check(Physics::ScaleCollider(box, { -2.0f, 1.0f, 1.0f }).HalfExtents.x > 0.0f,
				  "a negative scale mirrors rather than inverting the shape");
		}
	}

	// Audio, checked through the scene rather than through the mixer.
	//
	// Whether a sound is audible depends on the machine; what the scene does
	// about it must not. Audio::Engine allocates and tracks voices with or
	// without an output device precisely so this is testable either way, and
	// every check below holds in both cases.
	void CheckAudio()
	{
		const AssetHandle clip = Assets::Registry::GetHandle("audio/impact.wav");
		Check(clip.IsValid(), "a .wav in the assets folder is registered as an audio asset");
		Check(Assets::Registry::GetMetadata(clip).Type == AssetType::Audio,
			  "and typed from its extension");
		// The demo scene refers to all three by path, and a handle that stops
		// resolving is a silent failure there rather than a loud one.
		Check(Assets::Registry::GetHandle("audio/chime.wav").IsValid() &&
			  Assets::Registry::GetHandle("audio/hum.wav").IsValid(),
			  "as are the other sample clips");

		Audio::Engine::StopAll();

		// --- bus volumes ------------------------------------------------------
		Audio::Engine::SetBusVolume(AudioBus::Music, 0.25f);
		Check(std::fabs(Audio::Engine::GetBusVolume(AudioBus::Music) - 0.25f) < 1e-4f,
			  "a bus volume reads back as it was set");
		Check(std::fabs(Audio::Engine::GetBusVolume(AudioBus::SFX) - 1.0f) < 1e-4f,
			  "and does not affect the other buses");
		Audio::Engine::SetBusVolume(AudioBus::Music, 1.0f);

		// --- what does and does not produce a voice ---------------------------
		AudioPlayback missing;
		missing.Clip = AssetHandle::Invalid();
		Check(Audio::Engine::Play(missing) == 0, "a source with no clip plays nothing");

		AudioPlayback good;
		good.Clip = clip;
		const AudioVoice voice = Audio::Engine::Play(good);
		Check(voice != 0, "a valid clip yields a voice");
		Check(Audio::Engine::IsPlaying(voice), "which is playing");
		Audio::Engine::Stop(voice);
		Check(!Audio::Engine::IsPlaying(voice), "and stops when told to");
		Check(Audio::Engine::GetVoiceCount() == 0, "leaving nothing behind");

		// --- play on awake ----------------------------------------------------
		auto scene = std::make_shared<Scene>();

		Entity speaker = scene->CreateEntity("Speaker");
		speaker.GetComponent<TransformComponent>().Position = { 3.0f, 0.0f, -2.0f };
		{
			auto& source = speaker.AddComponent<AudioSourceComponent>();
			source.Clip = clip;
			source.Loop = true;          // so it cannot end before it is checked
			source.PlayOnAwake = true;
		}

		Entity quiet = scene->CreateEntity("Quiet");
		{
			auto& source = quiet.AddComponent<AudioSourceComponent>();
			source.Clip = clip;
			source.PlayOnAwake = false;
		}

		scene->OnRuntimeStart();

		Check(speaker.GetComponent<AudioSourceComponent>().Voice != 0,
			  "play starts a source marked play-on-awake");
		Check(quiet.GetComponent<AudioSourceComponent>().Voice == 0,
			  "and leaves one that is not alone");

		scene->OnUpdateRuntime(1.0f / 60.0f);
		Audio::Engine::Update();
		Check(Audio::Engine::GetVoiceCount() == 1, "a looping voice survives a frame");

		// --- the listener ------------------------------------------------------
		Check(!scene->GetPrimaryListenerEntity(),
			  "a scene with no listener and no camera has nowhere to hear from");

		Entity camera = scene->CreateEntity("Camera");
		camera.AddComponent<CameraComponent>();
		Check(scene->GetPrimaryListenerEntity() == camera,
			  "the camera is the listener when nothing else claims to be");

		Entity ears = scene->CreateEntity("Ears");
		ears.AddComponent<AudioListenerComponent>();
		Check(scene->GetPrimaryListenerEntity() == ears,
			  "an explicit listener takes precedence over the camera");

		Entity better = scene->CreateEntity("Better Ears");
		better.AddComponent<AudioListenerComponent>().ListenerRank = 0;
		ears.GetComponent<AudioListenerComponent>().ListenerRank = 5;
		Check(scene->GetPrimaryListenerEntity() == better,
			  "and the lowest rank wins among several");

		// --- destroying a source stops it --------------------------------------
		scene->DeleteEntity(speaker);
		Check(Audio::Engine::GetVoiceCount() == 0,
			  "destroying an entity stops the sound it was playing");

		// --- stop silences everything ------------------------------------------
		Entity again = scene->CreateEntity("Again");
		{
			auto& source = again.AddComponent<AudioSourceComponent>();
			source.Clip = clip;
			source.Loop = true;
		}
		scene->OnRuntimeStart();
		Check(Audio::Engine::GetVoiceCount() == 1, "restarting play starts it again");

		scene->OnRuntimeStop();
		Check(Audio::Engine::GetVoiceCount() == 0, "stopping the scene silences everything");
		Check(again.GetComponent<AudioSourceComponent>().Voice == 0,
			  "and clears the voice off the component");
	}

	// Where 2.5 and 2.6 meet: a built-in script hearing about a collision and
	// turning it into a sound.
	//
	// Each half is tested on its own above. This is the join, which is the part
	// that is easy to leave broken -- a callback that fires into a script that
	// cannot reach the mixer looks fine from either side.
	void CheckCollisionSound()
	{
		Audio::Engine::StopAll();

		auto scene = std::make_shared<Scene>();

		Entity ground = scene->CreateEntity("Ground");
		ground.GetComponent<TransformComponent>().Position = { 0.0f, -1.0f, 0.0f };
		ground.AddComponent<RigidBodyComponent>(BodyType::Static);
		ground.AddComponent<ColliderComponent>().HalfExtents = { 25.0f, 1.0f, 25.0f };

		Entity box = scene->CreateEntity("Box");
		box.GetComponent<TransformComponent>().Position = { 0.0f, 5.0f, 0.0f };
		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>();
		box.AddComponent<NativeScriptComponent>("ImpactSound");

		auto& source = box.AddComponent<AudioSourceComponent>();
		source.Clip = Assets::Registry::GetHandle("audio/impact.wav");
		source.PlayOnAwake = false;

		constexpr float dt = 1.0f / 60.0f;
		scene->OnRuntimeStart();

		Check(Audio::Engine::GetVoiceCount() == 0,
			  "a source that is not play-on-awake is silent at the start");

		// Falling, not yet landed.
		for (int i = 0; i < 20; i++)
			scene->OnFixedUpdateRuntime(dt);

		Check(Audio::Engine::GetVoiceCount() == 0, "and while it is still in the air");

		for (int i = 0; i < 40; i++)
			scene->OnFixedUpdateRuntime(dt);

		Check(Audio::Engine::GetVoiceCount() > 0, "landing plays the clip");
		Check(box.GetComponent<AudioSourceComponent>().Voice == 0,
			  "as a one-shot, so a second hit overlaps rather than cutting the first off");

		scene->OnRuntimeStop();
		Check(Audio::Engine::GetVoiceCount() == 0, "and stop silences it");
	}

	// Renders a short arrangement through the engine's own mixer and writes it
	// to a .wav.
	//
	// The measurements below prove the mixer produces the right numbers. This
	// exists because a person cannot check a number by ear, and "audio works"
	// is a claim they are entitled to verify themselves: the file this writes
	// is what the engine would have sent to the speakers, and it opens in
	// anything.
	//
	// Not part of the test run -- it is behind --dump-audio=<file>.
	void DumpRenderedAudio(const std::string& path)
	{
		const uint32_t rate = Audio::Engine::GetSampleRate();
		const uint32_t channels = Audio::Engine::GetChannels();
		if (Audio::Engine::GetMode() != AudioMode::Offline || rate == 0 || channels != 2)
		{
			RV_CORE_ERROR("--dump-audio needs the offline mixer");
			return;
		}

		const AssetHandle impact = Assets::Registry::GetHandle("audio/impact.wav");
		const AssetHandle chime = Assets::Registry::GetHandle("audio/chime.wav");
		const AssetHandle hum = Assets::Registry::GetHandle("audio/hum.wav");

		Audio::Engine::StopAll();
		Audio::Engine::SetListener({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });

		std::vector<float> mix;

		// Rendered in small blocks so the arrangement can change between them,
		// which is exactly what a frame does.
		constexpr float kBlock = 1.0f / 60.0f;
		const uint64_t blockFrames = (uint64_t)(kBlock * rate);
		std::vector<float> block(blockFrames * channels);

		auto advance = [&](float seconds, const std::function<void(float)>& each)
		{
			// Rounded, not truncated. 0.5 / (1/60) is 29.999998 in float, so
			// truncating renders 29 blocks and every section starts fractionally
			// earlier than the last -- an error that accumulates down the
			// arrangement.
			const int blocks = (int)std::lround(seconds / kBlock);
			for (int i = 0; i < blocks; i++)
			{
				each((float)i * kBlock);
				const uint64_t got = Audio::Engine::RenderFrames(block.data(), blockFrames);
				mix.insert(mix.end(), block.begin(), block.begin() + got * channels);
				Audio::Engine::Update();
			}
		};

		// 1. Three impacts at falling volume: the clip plays, and volume works.
		for (int i = 0; i < 3; i++)
		{
			AudioPlayback hit;
			hit.Clip = impact;
			hit.Spatial = false;
			hit.Volume = 1.0f - i * 0.3f;
			Audio::Engine::Play(hit);
			advance(0.5f, [](float) {});
		}

		// 2. A chime on its own: a second clip, and a different decode path.
		AudioPlayback bell;
		bell.Clip = chime;
		bell.Spatial = false;
		Audio::Engine::Play(bell);
		advance(1.2f, [](float) {});

		// 3. The drone travelling from left to right, twice round its
		// two-second loop.
		//
		// The drone rather than the chime, which is what this was first: a
		// bell decays in under a second, so it had gone quiet long before it
		// reached the right-hand side and the sweep demonstrated nothing. A
		// sustained sound shows the pan and the seamless loop at once.
		AudioPlayback drone;
		drone.Clip = hum;
		drone.Loop = true;
		drone.Spatial = true;
		drone.Volume = 0.8f;
		drone.MinDistance = 3.0f;
		drone.MaxDistance = 60.0f;
		drone.Position = { -10.0f, 0.0f, -2.0f };

		const AudioVoice travelling = Audio::Engine::Play(drone);
		constexpr float kSweep = 4.5f;
		advance(kSweep, [&](float t)
		{
			const float x = -10.0f + (t / kSweep) * 20.0f;
			Audio::Engine::SetVoicePosition(travelling, { x, 0.0f, -2.0f });
		});
		Audio::Engine::Stop(travelling);

		// --- write it out ----------------------------------------------------
		// 16-bit PCM, by hand. miniaudio is built with MA_NO_ENCODING, and a
		// canonical WAV header is 44 bytes -- pulling in an encoder to write
		// them would be the larger change.
		std::ofstream file(path, std::ios::binary);
		if (!file)
		{
			RV_CORE_ERROR("Could not write {0}", path);
			return;
		}

		const uint32_t frames = (uint32_t)(mix.size() / channels);
		const uint32_t dataBytes = frames * channels * 2;
		const uint32_t byteRate = rate * channels * 2;

		auto u32 = [&](uint32_t v) { file.write((const char*)&v, 4); };
		auto u16 = [&](uint16_t v) { file.write((const char*)&v, 2); };

		file.write("RIFF", 4);   u32(36 + dataBytes);   file.write("WAVE", 4);
		file.write("fmt ", 4);   u32(16);
		u16(1);                                  // PCM
		u16((uint16_t)channels);
		u32(rate);
		u32(byteRate);
		u16((uint16_t)(channels * 2));           // block align
		u16(16);                                 // bits
		file.write("data", 4);   u32(dataBytes);

		for (float sample : mix)
		{
			const float clamped = Math::Clamp(sample, -1.0f, 1.0f);
			u16((uint16_t)(int16_t)(clamped * 32767.0f));
		}

		RV_CORE_INFO("Wrote {0}: {1:.2f}s, {2} Hz, {3} channels",
					 path, (float)frames / (float)rate, rate, channels);
	}

	// The one check that looks at the sound itself.
	//
	// Everything else about audio can pass while the engine emits silence: a
	// voice being created, tracked and retired says nothing about whether a
	// sample ever came out of it. Offline mode drives the same mixing graph a
	// device would pull and hands back what it produced, so the output can be
	// measured instead of listened to -- which is the only way to check it on a
	// machine nobody is sitting at.
	void CheckRenderedAudio()
	{
		Check(Audio::Engine::GetMode() == AudioMode::Offline, "the offline mixer is running");
		Check(Audio::Engine::GetChannels() == 2 && Audio::Engine::GetSampleRate() == 48000,
			  "with a stated format, so the mix is the same on every machine");

		const uint32_t channels = Audio::Engine::GetChannels();
		const uint32_t rate = Audio::Engine::GetSampleRate();
		if (channels != 2 || rate == 0)
			return;

		const AssetHandle clip = Assets::Registry::GetHandle("audio/impact.wav");

		std::vector<float> buffer;

		// Root mean square of one channel: the loudness of the block, rather
		// than of whichever sample the block happened to start on.
		auto render = [&](float seconds, float& leftRms, float& rightRms)
		{
			const uint64_t frames = (uint64_t)(seconds * rate);
			buffer.assign((size_t)frames * channels, 0.0f);

			const uint64_t got = Audio::Engine::RenderFrames(buffer.data(), frames);

			double left = 0.0, right = 0.0;
			for (uint64_t i = 0; i < got; i++)
			{
				left += (double)buffer[i * channels] * buffer[i * channels];
				right += (double)buffer[i * channels + 1] * buffer[i * channels + 1];
			}

			leftRms = got ? (float)std::sqrt(left / (double)got) : 0.0f;
			rightRms = got ? (float)std::sqrt(right / (double)got) : 0.0f;
		};

		float left = 0.0f, right = 0.0f;

		// --- silence in, silence out -----------------------------------------
		Audio::Engine::StopAll();
		render(0.05f, left, right);
		Check(left == 0.0f && right == 0.0f, "nothing playing renders exact silence");

		// --- a clip actually produces samples --------------------------------
		AudioPlayback flat;
		flat.Clip = clip;
		flat.Spatial = false;
		const AudioVoice loud = Audio::Engine::Play(flat);
		render(0.1f, left, right);
		const float loudLevel = Math::Max(left, right);

		Check(loudLevel > 0.01f, "playing a clip renders audible samples");
		Check(std::fabs(left - right) < 1e-5f, "and an unpositioned sound is centred");
		Audio::Engine::Stop(loud);

		// --- volume scales it ------------------------------------------------
		flat.Volume = 0.25f;
		const AudioVoice quiet = Audio::Engine::Play(flat);
		render(0.1f, left, right);
		const float quietLevel = Math::Max(left, right);
		Audio::Engine::Stop(quiet);

		// A quarter of the amplitude, within a wide tolerance: the block starts
		// at the same point in the same clip, so the two are directly
		// comparable, but resampling makes an exact ratio the wrong thing to
		// demand.
		Check(quietLevel > 0.0f && quietLevel < loudLevel * 0.5f,
			  "volume scales what is rendered");

		// --- 3D positioning pans ---------------------------------------------
		Audio::Engine::SetListener({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });

		AudioPlayback placed;
		placed.Clip = clip;
		placed.Spatial = true;
		placed.MinDistance = 1.0f;
		placed.MaxDistance = 100.0f;

		placed.Position = { 5.0f, 0.0f, 0.0f };
		const AudioVoice onTheRight = Audio::Engine::Play(placed);
		render(0.1f, left, right);
		Audio::Engine::Stop(onTheRight);
		const float rightSideL = left, rightSideR = right;

		placed.Position = { -5.0f, 0.0f, 0.0f };
		const AudioVoice onTheLeft = Audio::Engine::Play(placed);
		render(0.1f, left, right);
		Audio::Engine::Stop(onTheLeft);
		const float leftSideL = left, leftSideR = right;

		Check(rightSideR > rightSideL * 1.2f, "a sound to the right is louder in the right ear");
		Check(leftSideL > leftSideR * 1.2f, "and one to the left in the left");

		// --- and distance attenuates -----------------------------------------
		// Not `near` and `far`: both are macros in the Windows headers, which
		// turns them into a declaration with no name.
		placed.Position = { 0.0f, 0.0f, -1.0f };
		const AudioVoice closeBy = Audio::Engine::Play(placed);
		render(0.1f, left, right);
		const float nearLevel = Math::Max(left, right);
		Audio::Engine::Stop(closeBy);

		placed.Position = { 0.0f, 0.0f, -60.0f };
		const AudioVoice distant = Audio::Engine::Play(placed);
		render(0.1f, left, right);
		const float farLevel = Math::Max(left, right);
		Audio::Engine::Stop(distant);

		Check(nearLevel > farLevel * 2.0f, "and further away is quieter");

		// --- stopping is silent again ----------------------------------------
		render(0.05f, left, right);
		Check(left == 0.0f && right == 0.0f, "stopping every voice returns it to silence");

		// --- a one-shot ends on its own --------------------------------------
		// impact.wav is 0.28s. Rendering past the end of it is the only way to
		// exercise the retirement path against real playback rather than
		// against a voice that was stopped by hand.
		AudioPlayback oneShot;
		oneShot.Clip = clip;
		oneShot.Spatial = false;
		Audio::Engine::Play(oneShot);

		render(0.5f, left, right);
		Check(Math::Max(left, right) > 0.0f, "a one-shot renders");

		Audio::Engine::Update();
		Check(Audio::Engine::GetVoiceCount() == 0, "and retires itself once it has played out");

		render(0.05f, left, right);
		Check(left == 0.0f && right == 0.0f, "leaving silence behind it");
	}

	// The source's settings are scene data and have to survive a save; the
	// voice is not and must not.
	void CheckAudioSerialization()
	{
		auto scene = std::make_shared<Scene>();

		Entity entity = scene->CreateEntity("Source");
		auto& source = entity.AddComponent<AudioSourceComponent>();
		source.Clip = Assets::Registry::GetHandle("audio/chime.wav");
		source.Bus = AudioBus::Music;
		source.Volume = 0.42f;
		source.Pitch = 1.35f;
		source.Loop = true;
		source.PlayOnAwake = false;
		source.Spatial = false;
		source.MinDistance = 2.5f;
		source.MaxDistance = 88.0f;
		source.Stream = true;
		source.Voice = 12345;   // must not survive

		entity.AddComponent<AudioListenerComponent>().ListenerRank = 7;

		SceneSerializer serializer(scene);
		const std::string yaml = serializer.SerializeToString();

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		reader.DeserializeFromString(yaml);

		Entity restored = reloaded->FindEntityByName("Source");
		Check((bool)restored, "the entity survives the round trip");
		if (!restored)
			return;

		Check(restored.HasComponent<AudioSourceComponent>(), "with its audio source");
		const auto& back = restored.GetComponent<AudioSourceComponent>();

		Check(back.Clip == source.Clip, "the clip is stored by handle");
		Check(back.Bus == AudioBus::Music, "the bus survives");
		Check(std::fabs(back.Volume - 0.42f) < 1e-4f &&
			  std::fabs(back.Pitch - 1.35f) < 1e-4f, "volume and pitch survive");
		Check(back.Loop && !back.PlayOnAwake && !back.Spatial && back.Stream,
			  "and every flag survives");
		Check(std::fabs(back.MinDistance - 2.5f) < 1e-4f &&
			  std::fabs(back.MaxDistance - 88.0f) < 1e-4f, "as do the distances");
		Check(back.Voice == 0, "the playing voice does not, because it belongs to one run");

		Check(restored.HasComponent<AudioListenerComponent>() &&
			  restored.GetComponent<AudioListenerComponent>().ListenerRank == 7,
			  "a listener and its rank survive");
	}

	// Stop has to put the scene back exactly, including everything physics
	// moved -- which is most of the scene after a run.
	void CheckPhysicsRestoresOnStop()
	{
		auto scene = std::make_shared<Scene>();

		Entity ground = scene->CreateEntity("Ground");
		ground.GetComponent<TransformComponent>().Position = { 0.0f, -1.0f, 0.0f };
		ground.AddComponent<RigidBodyComponent>(BodyType::Static);
		ground.AddComponent<ColliderComponent>().HalfExtents = { 25.0f, 1.0f, 25.0f };

		Entity box = scene->CreateEntity("Box");
		box.GetComponent<TransformComponent>().Position = { 0.0f, 6.0f, 0.0f };
		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>();

		SceneSerializer serializer(scene);
		const std::string snapshot = serializer.SerializeToString();

		scene->OnRuntimeStart();
		for (int i = 0; i < 120; i++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		scene->OnUpdateRuntime(1.0f / 60.0f);

		Check(serializer.SerializeToString() != snapshot, "the simulation moved things");

		scene->OnRuntimeStop();
		Check(serializer.DeserializeFromString(snapshot), "the snapshot restores");
		Check(serializer.SerializeToString() == snapshot,
			  "stopping puts everything physics moved back exactly");
	}

	void CheckInputMap()
	{
		InputMap::ClearBindings();

		InputMap::BindKey("Gameplay", "Jump", RV_KEY_SPACE);
		InputMap::BindKeyAxis("Gameplay", "MoveRight", RV_KEY_D, RV_KEY_A);
		InputMap::BindKey("Menu", "Back", RV_KEY_ESCAPE);

		const std::vector<std::string> actions = InputMap::GetActionNames();
		Check(std::find(actions.begin(), actions.end(), "Jump") != actions.end(),
			  "an action appears once bound");
		Check(std::find(actions.begin(), actions.end(), "Back") != actions.end(),
			  "actions from several contexts coexist");

		Check(InputMap::IsContextEnabled("Gameplay"), "a context is enabled by default");
		InputMap::SetContextEnabled("Menu", false);
		Check(!InputMap::IsContextEnabled("Menu"), "a context can be disabled");

		// No device state in a headless test, so what is checked is that
		// queries are total: an unknown name answers rather than throwing.
		Check(!InputMap::IsActionDown("Jump"), "an unpressed action is not down");
		Check(!InputMap::IsActionDown("NoSuchAction"), "an unknown action is not down");
		Check(InputMap::GetAxis("NoSuchAxis") == 0.0f, "an unknown axis reads zero");

		// The wheel is fed rather than sampled, so it can be driven here.
		InputMap::OnScroll(3.0f);
		InputMap::BindMouseAxis("Gameplay", "Zoom", MouseAxis::Wheel);
		InputMap::Update();
		Check(std::fabs(InputMap::GetAxis("Zoom") - 3.0f) < 1e-4f, "the wheel reaches its axis");

		// It has to be consumed, or it would read as scrolling forever.
		InputMap::Update();
		Check(InputMap::GetAxis("Zoom") == 0.0f, "the wheel clears after one frame");

		InputMap::ClearBindings();
		InputMap::LoadDefaults();
		Check(!InputMap::GetActionNames().empty(), "defaults provide actions");
		Check(!InputMap::GetAxisNames().empty(), "defaults provide axes");
	}

	void CheckCameraRanking()
	{
		auto scene = std::make_shared<Scene>();

		Entity low = scene->CreateEntity("Rank 5");
		low.AddComponent<CameraComponent>().ViewRank = 5;

		Entity tieA = scene->CreateEntity("Rank 2 A");
		tieA.AddComponent<CameraComponent>().ViewRank = 2;

		Entity tieB = scene->CreateEntity("Rank 2 B");
		tieB.AddComponent<CameraComponent>().ViewRank = 2;

		Entity winner = scene->GetPrimaryCameraEntity();
		Check(winner && winner.GetComponent<CameraComponent>().ViewRank == 2,
			  "the lowest ViewRank wins");

		// Ties must resolve on entity id rather than on registry order, or
		// adding an unrelated camera could silently change the view.
		const Entity expected = tieA.GetUUID() < tieB.GetUUID() ? tieA : tieB;
		Check(winner == expected, "a rank tie breaks on entity id, not creation order");

		// Repeatable within a run, and repeatable across runs because the
		// comparison is on ids rather than on iteration.
		Check(scene->GetPrimaryCameraEntity() == winner, "camera selection is stable");

		// Rank 0 is the highest priority, so a new camera at the default rank
		// takes over -- which is what makes "add a camera and look through it"
		// behave the way people expect.
		Entity fresh = scene->CreateEntity("Default rank");
		fresh.AddComponent<CameraComponent>();
		Check(scene->GetPrimaryCameraEntity() == fresh, "a default-rank camera outranks rank 2");

		scene->DeleteEntity(fresh);
		Check(scene->GetPrimaryCameraEntity() == expected,
			  "removing the winner falls back to the next rank");
	}

	size_t EntityCount(const std::shared_ptr<Scene>& scene)
	{
		return scene->GetRegistry().view<IDComponent>().size();
	}

	// The property that matters for undo is not "it changes something back" but
	// "the scene is bit-for-bit what it was". Comparing serialized snapshots is
	// the only check that catches a command restoring four fields out of five.
	void CheckUndoRestoresExactly(const std::shared_ptr<Scene>& scene, CommandStack& stack,
								  std::unique_ptr<EditorCommand> command, const std::string& what)
	{
		SceneSerializer serializer(scene);
		const std::string before = serializer.SerializeToString();

		stack.Push(std::move(command));
		const std::string during = serializer.SerializeToString();
		Check(during != before, what + ": changed something");

		stack.Undo();
		Check(serializer.SerializeToString() == before, what + ": undo restores the scene exactly");

		stack.Redo();
		Check(serializer.SerializeToString() == during, what + ": redo reapplies it exactly");

		stack.Undo();
	}

	void CheckUndo(const std::shared_ptr<Scene>& scene)
	{
		CommandStack stack;

		// Held as ids, not as Entity handles. Undoing a delete recreates the
		// entity with the same UUID but a *different* entt handle, so a stored
		// handle is stale the moment anything is deleted -- which is exactly
		// why the commands themselves address entities by UUID.
		UUID rootID = UUID::Invalid();
		UUID childID = UUID::Invalid();

		for (auto handle : scene->GetRegistry().view<TagComponent>())
		{
			Entity entity{ handle, scene.get() };
			if (entity.GetName() == "Root")       rootID = entity.GetUUID();
			else if (entity.GetName() == "Child") childID = entity.GetUUID();
		}

		if (!rootID.IsValid() || !childID.IsValid())
		{
			Check(false, "undo fixture entities present");
			return;
		}

		// Deleting a parent takes its children with it, so this also covers
		// restoring a whole subtree with its ids and parent links intact.
		const size_t populated = EntityCount(scene);
		CheckUndoRestoresExactly(scene, stack,
			std::make_unique<DeleteEntityCommand>(scene, scene->GetEntityByUUID(rootID)),
			"delete subtree");
		Check(EntityCount(scene) == populated, "entity count is back after undoing a delete");
		Check(scene->HasEntity(rootID) && scene->HasEntity(childID),
			  "restored entities keep their original ids");

		// Removing a component must restore its values, not just its presence.
		CheckUndoRestoresExactly(scene, stack,
			std::make_unique<RemoveComponentCommand>(scene, childID, "MeshComponent"),
			"remove component");

		CheckUndoRestoresExactly(scene, stack,
			std::make_unique<AddComponentCommand>(scene, rootID, "ColorComponent"),
			"add component");

		// Reparenting rewrites the child's local transform to preserve its world
		// position, so undo has to put that back too.
		CheckUndoRestoresExactly(scene, stack,
			std::make_unique<ReparentCommand>(scene, scene->GetEntityByUUID(childID), Entity{}),
			"unparent");

		CheckUndoRestoresExactly(scene, stack,
			std::make_unique<FieldEditCommand>(scene, rootID, "TagComponent", "Tag",
											   FieldValue(std::string("Root")),
											   FieldValue(std::string("Renamed"))),
			"field edit");

		// A cycle must be refused rather than recorded.
		ReparentCommand cycle(scene, scene->GetEntityByUUID(rootID),
									 scene->GetEntityByUUID(childID));
		Check(!cycle.IsValid(), "parenting an entity to its own descendant is rejected");

		// Redo is dropped once a new edit diverges from the undone branch.
		stack.Push(std::make_unique<AddComponentCommand>(scene, rootID, "ColorComponent"));
		stack.Undo();
		Check(stack.CanRedo(), "redo is available after an undo");
		stack.Push(std::make_unique<AddComponentCommand>(scene, childID, "ColorComponent"));
		Check(!stack.CanRedo(), "a new edit clears the redo branch");
	}
}

// A scratch directory nobody else is using.
//
// The process id is in the name because these paths were fixed, and two
// scenetest processes overlapping would then collide: one removing the tree
// while the other creates in it. Sequential runs can collide too -- Windows
// defers a deletion while any handle is open, so a directory removed by the
// previous process can still exist when the next one checks for it.
//
// That was observed once as two unexplained failures in a run that had passed
// twenty-three times before and after. This does not prove that was the cause;
// it removes the only mechanism found that could produce it.
static std::filesystem::path ScratchDir(const std::string& name)
{
	return std::filesystem::temp_directory_path()
		 / ("ragev-" + name + "-" + std::to_string(GetCurrentProcessId()));
}

int RunTests(int argc, char** argv);

int main(int argc, char** argv)
{
	// A test tool that dies to an uncaught exception tells you nothing but
	// "abort() has been called". Report what actually went wrong.
	try
	{
		return RunTests(argc, argv);
	}
	catch (const std::exception& e)
	{
		RV_CORE_ERROR("Uncaught exception: {0}", e.what());
		return 1;
	}
	catch (...)
	{
		RV_CORE_ERROR("Uncaught non-standard exception");
		return 1;
	}
}

int RunTests(int argc, char** argv)
{
	Log::Init();

	Backend backend = Backend::Vulkan;
	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];
		if (argument == "--rhi=opengl" || argument == "--rhi=gl")
			backend = Backend::OpenGL;
		else if (argument == "--rhi=vulkan" || argument == "--rhi=vk")
			backend = Backend::Vulkan;
	}

	// The engine's window, not one this executable makes with GLFW directly.
	//
	// GLFW keeps its state in globals, and the engine is a DLL with its own copy
	// of them. A window created here would be invisible to the engine: on Vulkan
	// it asks for the HWND and gets null, on OpenGL it tries to load the function
	// pointers and finds no current context. Both fail at device creation, which
	// is a long way from the cause.
	//
	// Going through Window::Create also means the tests exercise the same
	// startup the editor and the runtime do, hints and all -- and the backend has
	// to reach EngineConfig for those hints to be right, which is what Init does
	// with the same --rhi argument parsed above.
	EngineConfig::Init(argc, argv);

	WindowProps props("RageV scene test", 640, 480);
	props.Visible = false;   // nothing is drawn; the device exists so materials can allocate

	std::unique_ptr<Window> window(Window::Create(props));
	if (!window)
	{
		RV_CORE_ERROR("window creation failed");
		return 1;
	}

	DeviceDesc deviceDesc;
	deviceDesc.Backend = backend;
	// An opaque handle straight through: DeviceDesc names no windowing type, so
	// there is nothing here to cast to and no GLFW header to reach for. See the
	// include note at the top for why this executable must not have one.
	deviceDesc.Window = window->GetNativeWindow();
	deviceDesc.Width = 640;
	deviceDesc.Height = 480;
	deviceDesc.VSync = true;
	deviceDesc.EnableValidation = true;
	deviceDesc.FramesInFlight = 2;

	auto device = RHIDevice::Create(deviceDesc);
	if (!device)
	{
		RV_CORE_ERROR("device creation failed");
		return 1;
	}
	// Also compiles both renderers' shaders, so a broken shader fails here
	// rather than silently later.
	Renderer::Init(*device);
	// The same resolution the editor and the runtime use, so the tests exercise
	// a real project rather than a folder that happens to be beside them.
	Project::OpenConfigured();
	Assets::Registry::Init(Project::GetActive() ? Project::AssetRoot()
											 : std::filesystem::path("assets"));
	Assets::Manager::Init(*device);
	// Opens a real device if there is one. The audio checks are written to
	// hold either way, so a machine with no sound card is not a failing build.
	Audio::Engine::Init();

	RV_CORE_INFO("Scene round-trip test on {0}", device->GetCaps().APIName);

	// --- simulation loop -----------------------------------------------------
	CheckFixedStep();
	CheckInputMap();
	CheckScriptApi();
	CheckLiveScriptChanges();

	// --- physics -------------------------------------------------------------
	CheckRenderGraph();
	CheckPrimitiveWinding();
	CheckCubemap();
	CheckSky();
	CheckRenderersReady();
	CheckFieldLabels();
	CheckShadowToggle();
	CheckShadowCascades();
	CheckIrradiance();
	CheckEnvironmentBRDF();
	CheckFrustumCulling();
	CheckLightGrid();
	CheckSkeleton();
	CheckSkinnedImport();
	CheckSkinnedVertexLayout();
	CheckReflectionProbe();
	CheckFrameGraph();
	CheckGameModule();
	CheckProject();
	CheckPackaging();
	CheckRuntimePath();
	CheckPhysics();
	CheckParticles();
	CheckColliderOverlay();
	CheckPicking();
	CheckContactCallbacks();
	CheckTriggerCallbacks();
	CheckPhysicsRestoresOnStop();
	CheckPlayModeRestore();

	// --- audio ---------------------------------------------------------------
	CheckAudio();
	CheckCollisionSound();
	CheckAudioSerialization();

	// Then the whole suite again with no output device. The claim this makes is
	// that what the engine does with sound does not depend on whether the
	// machine can play it -- and a claim like that is worth running rather than
	// believing, since the day it stops being true is a day nobody notices on a
	// desk with speakers.
	RV_CORE_INFO("Audio again, with no output device");
	Audio::Engine::Shutdown();
	Audio::Engine::Init(AudioMode::Silent);
	Check(!Audio::Engine::IsAvailable(), "the silent path really is silent");
	CheckAudio();
	CheckCollisionSound();

	// And once more against the mixer itself, where the output can be measured.
	RV_CORE_INFO("Audio again, offline, measuring the mix");
	Audio::Engine::Shutdown();
	Audio::Engine::Init(AudioMode::Offline);
	CheckAudio();
	CheckCollisionSound();
	CheckRenderedAudio();

	// Opt-in, and after the checks so it cannot affect them.
	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];
		if (argument.rfind("--dump-audio=", 0) == 0)
			DumpRenderedAudio(argument.substr(std::string("--dump-audio=").size()));
	}

	// --- cameras -------------------------------------------------------------
	CheckCameraRanking();

	// --- prefabs -------------------------------------------------------------
	CheckPrefabs();

	// --- assets --------------------------------------------------------------
	CheckPrimitiveHandles();
	CheckGltfImport();
	CheckModelInstantiation();

	// --- pass 1: fixture -> A ------------------------------------------------
	std::string a;
	{
		auto scene = BuildFixture();
		SceneSerializer serializer(scene);
		a = serializer.SerializeToString();
		CheckUniqueIDs(scene);
	}

	// --- pass 2: A -> scene -> B ---------------------------------------------
	std::string b;
	{
		auto scene = std::make_shared<Scene>();
		SceneSerializer serializer(scene);
		Check(serializer.DeserializeFromString(a), "Deserialize reports success");

		CheckHierarchyPreserved(scene);
		CheckUniqueIDs(scene);

		const SceneEnvironment& environment = scene->GetEnvironment();
		Check(std::fabs(environment.AmbientColor.b - 0.44f) < 1e-4f &&
			  std::fabs(environment.AmbientIntensity - 0.37f) < 1e-4f,
			  "scene ambient survives the round trip");

		Check(environment.Sky == SkyType::Cubemap &&
			  std::fabs(environment.SkyZenith.g - 0.55f) < 1e-4f &&
			  std::fabs(environment.SkyGround.r - 0.77f) < 1e-4f &&
			  std::fabs(environment.SkyIntensity - 1.75f) < 1e-4f &&
			  std::fabs(environment.SkyRotation - 0.9f) < 1e-4f &&
			  (uint64_t)environment.SkyTexture == 0x5ceec0de1234ull,
			  "and so does the sky, handle included");

		b = serializer.SerializeToString();
	}

	// --- pass 3: undo/redo ---------------------------------------------------
	{
		auto scene = std::make_shared<Scene>();
		SceneSerializer serializer(scene);
		serializer.DeserializeFromString(a);
		CheckUndo(scene);
	}

	Check(a == b, "save -> load -> save is byte-identical");

	if (a != b)
	{
		// Print the first divergence rather than two multi-kilobyte blobs.
		size_t at = 0;
		while (at < a.size() && at < b.size() && a[at] == b[at])
			at++;

		const size_t from = at > 120 ? at - 120 : 0;
		RV_CORE_ERROR("First difference at byte {0}", at);
		RV_CORE_ERROR("  A: ...{0}", a.substr(from, 240));
		RV_CORE_ERROR("  B: ...{0}", b.substr(from, 240));
	}

	// --- pass 3: loading twice into one scene must replace, not merge --------
	{
		auto scene = std::make_shared<Scene>();
		SceneSerializer serializer(scene);
		serializer.DeserializeFromString(a);
		const size_t first = scene->GetRegistry().view<IDComponent>().size();
		serializer.DeserializeFromString(a);
		const size_t second = scene->GetRegistry().view<IDComponent>().size();

		Check(first == second, "loading a scene replaces the current one rather than merging");
	}

	// --- the generated C++ script template -----------------------------------
	//
	// Scripts/TemplateProbe.cpp *is* what "New Script..." writes, kept in the
	// tree so that the template is compiled and registered by every build. It
	// caught a real defect the first time: a file whose only contents are a
	// static registrar has no referenced symbol, so the linker discards the
	// object file, and the script compiles, links, and never appears in the
	// dropdown. The template anchors itself with an exported symbol and a
	// /include: directive; this is what proves that still works.
	{
		Check(ScriptRegistry::IsRegistered("TemplateProbe"),
			  "the generated C++ script template registers itself");

		const std::vector<ScriptField>& fields = ScriptRegistry::FieldsOf("TemplateProbe");
		Check(fields.size() == 1 && fields[0].Name == "Speed",
			  "and its example field reaches the inspector");
	}

	// --- C++ script fields ---------------------------------------------------
	//
	// C++ has no reflection, so a script's editable fields are whatever the
	// registration named. Checked against the built-in scripts, which are also
	// the worked examples: if these stop having fields, the documented way to
	// declare one has stopped working.
	{
		const std::vector<ScriptField>& spinner = ScriptRegistry::FieldsOf("Spinner");
		Check(spinner.size() == 1, "a registered C++ script reports its fields");

		if (!spinner.empty())
		{
			Check(spinner[0].Name == "Speed", "by the name the registration gave");
			Check(spinner[0].Kind == ScriptFieldKind::Float, "with the right kind");
			Check(spinner[0].Default == "1.2",
				  "and the default from a constructed instance, not zero");
		}

		Check(ScriptRegistry::FieldsOf("Mover").size() == 2, "and a script may declare several");
		Check(ScriptRegistry::FieldsOf("NoSuchScript").empty(),
			  "while an unknown script reports none rather than failing");

		// A script with no declared fields is the common case and is not an
		// error -- most scripts have nothing worth tuning.
		Check(ScriptRegistry::FieldsOf("TriggerZone").empty(),
			  "and a script that declares none is fine");

		// Reading and writing through the registered accessors.
		if (!spinner.empty())
		{
			ScriptableEntity* instance = ScriptRegistry::Create("Spinner");
			Check(instance != nullptr, "a C++ script instantiates");

			if (instance)
			{
				Check(spinner[0].Get(instance) == "1.2", "and its field reads back");

				spinner[0].Set(instance, "3.5");
				Check(spinner[0].Get(instance) == "3.5", "and writes through");

				// A malformed value must not zero the field. Somebody typing
				// into the inspector passes through half-written numbers on the
				// way to a good one.
				spinner[0].Set(instance, "not a number");
				Check(spinner[0].Get(instance) == "3.5",
					  "while a malformed value leaves it alone rather than zeroing it");

				delete instance;
			}
		}
	}

	// --- overrides on a C++ script, through a scene --------------------------
	{
		auto authored = std::make_shared<Scene>();
		Entity host = authored->CreateEntity("Spun");
		auto& script = host.AddComponent<NativeScriptComponent>("Spinner");
		script.Set("Speed", "2.5");

		SceneSerializer writer(authored);
		const std::string text = writer.SerializeToString();
		Check(text.find("Speed") != std::string::npos,
			  "a C++ script's field override is written to the scene");

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		Check(reader.DeserializeFromString(text), "and the scene loads again");

		Entity restored = reloaded->FindEntityByName("Spun");
		Check(restored && restored.HasComponent<NativeScriptComponent>(),
			  "with the component intact");

		if (restored && restored.HasComponent<NativeScriptComponent>())
		{
			const auto& back = restored.GetComponent<NativeScriptComponent>();
			Check(back.Find("Speed") && *back.Find("Speed") == "2.5", "and the value somebody typed");
		}

		// The claim that matters: the running script uses the overridden value.
		// Spinner turns Speed radians a second about Y.
		reloaded->OnRuntimeStart();
		Entity spun = reloaded->FindEntityByName("Spun");
		spun.GetComponent<TransformComponent>().Rotation = Vec3(0.0f);

		for (int step = 0; step < 10; step++)
			reloaded->OnFixedUpdateRuntime(1.0f / 60.0f);

		const float turned = spun.GetComponent<TransformComponent>().Rotation.y;
		Check(std::fabs(turned - (2.5f * 10.0f / 60.0f)) < 1e-4f,
			  "and the scene steps it at the overridden speed, not the default");

		reloaded->OnRuntimeStop();
	}

	// --- the script component's label ----------------------------------------
	//
	// A tag for the inspector, and nothing but: it names the component the way
	// the Tag row names the entity. It used to be bound to the script name,
	// which meant typing in it silently repointed the entity at a script that
	// did not exist.
	//
	// The unlabelled case is the one worth a check. A component that writes an
	// empty key would change every scene file that predates labels, and the
	// round-trip test above would still pass -- it compares a file against
	// itself after a reload, not against what was on disk before the feature.
	{
		auto authored = std::make_shared<Scene>();
		Entity native = authored->CreateEntity("Labelled");
		auto& script = native.AddComponent<NativeScriptComponent>("Spinner");
		script.Label = "Door opener";

		Entity plain = authored->CreateEntity("Unlabelled");
		plain.AddComponent<NativeScriptComponent>("Spinner");

		Entity managed = authored->CreateEntity("Managed");
		auto& csharp = managed.AddComponent<ManagedScriptComponent>("RageV.Builtin.Spinner");
		csharp.Label = "Lift";

		SceneSerializer writer(authored);
		const std::string text = writer.SerializeToString();

		Check(text.find("Door opener") != std::string::npos,
			  "a script component's label is written to the scene");
		Check(text.find("Lift") != std::string::npos, "for either language");

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		Check(reader.DeserializeFromString(text), "and the scene loads again");

		Entity back = reloaded->FindEntityByName("Labelled");
		Check(back && back.HasComponent<NativeScriptComponent>() &&
			  back.GetComponent<NativeScriptComponent>().Label == "Door opener",
			  "with the label somebody typed");

		Check(back && back.GetComponent<NativeScriptComponent>().ScriptName == "Spinner",
			  "and the script it names untouched by it");

		Entity bare = reloaded->FindEntityByName("Unlabelled");
		Check(bare && bare.HasComponent<NativeScriptComponent>() &&
			  bare.GetComponent<NativeScriptComponent>().Label.empty(),
			  "while a component with no label still has none");

		Entity cs = reloaded->FindEntityByName("Managed");
		Check(cs && cs.HasComponent<ManagedScriptComponent>() &&
			  cs.GetComponent<ManagedScriptComponent>().Label == "Lift",
			  "and a C# component's label survives the same trip");

		// NativeScriptComponent's copy constructor and assignment are written by
		// hand, because they have to drop the live instance pointer rather than
		// let two components own one. That makes every field added to the
		// component one they can silently forget to carry.
		if (back && back.HasComponent<NativeScriptComponent>())
		{
			const NativeScriptComponent copied = back.GetComponent<NativeScriptComponent>();
			Check(copied.Label == "Door opener", "a copied component keeps its label");

			NativeScriptComponent assigned;
			assigned = back.GetComponent<NativeScriptComponent>();
			Check(assigned.Label == "Door opener", "and so does an assigned one");
		}
	}

	// --- the settings writer -------------------------------------------------
	//
	// The backend picker and the vsync checkbox each rewrite one line of
	// ragev.ini and must leave every other line exactly as it was. Worth a
	// check because the failure mode is silent and permanent: a writer that
	// truncates the file takes the audio, window and ui-scale settings with it,
	// and nobody finds out until the next start.
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		const fs::path ini = fs::current_path(ec) / "ragev.ini";

		// Whatever is staged here belongs to the tools, not to this test.
		const bool had = fs::exists(ini, ec);
		std::string saved;
		if (had)
		{
			std::ifstream in(ini, std::ios::binary);
			saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		{
			std::ofstream out(ini, std::ios::trunc);
			out << "# a comment\n[section]\naudio=off\nrhi=opengl\nwidth=1280\n";
		}

		Check(EngineConfig::SaveBackendPreference(Backend::Vulkan), "the backend preference writes");
		Check(EngineConfig::SaveVSyncPreference(false), "the vsync preference writes");

		std::string written;
		{
			std::ifstream in(ini);
			written.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		Check(written.find("rhi=vulkan") != std::string::npos,
			  "the key it was asked to change is changed");
		Check(written.find("rhi=opengl") == std::string::npos,
			  "and the old value is gone, not merely shadowed by a later line");
		Check(written.find("vsync=off") != std::string::npos,
			  "a key that was absent is appended");
		Check(written.find("audio=off") != std::string::npos &&
			  written.find("width=1280") != std::string::npos,
			  "every other setting survives the rewrite");
		Check(written.find("# a comment") != std::string::npos &&
			  written.find("[section]") != std::string::npos,
			  "so do comments and section headers");

		if (had)
		{
			std::ofstream out(ini, std::ios::trunc | std::ios::binary);
			out << saved;
		}
		else
		{
			fs::remove(ini, ec);
		}
	}

	// NOTE: this block names glm deliberately and must not be migrated with the
	// rest of the tree. It is the only thing that can tell whether RageV::Math
	// agrees with the library it delegates to; rewritten to use RageV::Math on
	// both sides it would compare the wrapper against itself and pass forever.
	// That happened once during the migration and was caught here.
	// --- the math layer against the library it replaces ----------------------
	//
	// RageV::Math exists so that no public header names glm. That only holds up
	// if it computes the same answers, and "looks right on screen" is not a way
	// to find a transposed matrix or a quaternion interpolated the long way
	// round -- both of which are visible only in specific poses.
	//
	// So every operation is checked against glm directly, here, once. After the
	// migration there will be no glm left at these call sites to compare
	// against, which is exactly why the comparison is worth writing now.
	{
		using namespace RageV::Math;

		const auto closeF = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
		const auto closeV3 = [&](const Vec3& a, const glm::vec3& b)
		{
			return closeF(a.x, b.x) && closeF(a.y, b.y) && closeF(a.z, b.z);
		};
		const auto closeV4 = [&](const Vec4& a, const glm::vec4& b)
		{
			return closeF(a.x, b.x) && closeF(a.y, b.y) && closeF(a.z, b.z) && closeF(a.w, b.w);
		};
		const auto closeM4 = [&](const Mat4& a, const glm::mat4& b)
		{
			for (int c = 0; c < 4; c++)
			{
				if (!closeV4(a[c], b[c]))
					return false;
			}
			return true;
		};
		const auto closeQ = [&](const Quat& a, const glm::quat& b)
		{
			// A quaternion and its negation are the same rotation, so the
			// comparison has to allow either. Requiring identical components
			// would fail on a correct answer.
			const bool same = closeF(a.x, b.x) && closeF(a.y, b.y) && closeF(a.z, b.z) && closeF(a.w, b.w);
			const bool flipped = closeF(a.x, -b.x) && closeF(a.y, -b.y) && closeF(a.z, -b.z) && closeF(a.w, -b.w);
			return same || flipped;
		};

		// Deliberately awkward numbers. Axis-aligned test values pass through a
		// transposed matrix unharmed, which is how that bug survives a test.
		const Vec3 a{ 1.5f, -2.25f, 0.75f };
		const Vec3 b{ -0.5f, 3.0f, 2.5f };
		const glm::vec3 ga = ToGlm(a);
		const glm::vec3 gb = ToGlm(b);

		Check(closeF(Dot(a, b), glm::dot(ga, gb)), "Dot matches glm");
		Check(closeV3(Cross(a, b), glm::cross(ga, gb)), "Cross matches glm, including handedness");
		Check(closeF(Length(a), glm::length(ga)), "Length matches glm");
		Check(closeF(Distance(a, b), glm::distance(ga, gb)), "Distance matches glm");
		Check(closeV3(Normalize(a), glm::normalize(ga)), "Normalize matches glm");
		Check(closeV3(a + b, ga + gb) && closeV3(a - b, ga - gb) && closeV3(a * b, ga * gb),
			  "component-wise arithmetic matches glm");
		Check(closeV3(a * 2.5f, ga * 2.5f) && closeV3(a / 2.5f, ga / 2.5f), "scalar arithmetic matches glm");
		Check(closeF(MaxComponent(a), glm::compMax(ga)), "MaxComponent matches glm::compMax");
		Check(closeV3(Mix(a, b, 0.3f), glm::mix(ga, gb, 0.3f)), "Mix matches glm");
		Check(closeF(Radians(90.0f), glm::radians(90.0f)), "Radians matches glm");

		// Deliberately *not* glm's behaviour, and the reason it is worth a
		// check: glm returns NaNs here, those NaNs propagate into every derived
		// transform, and the object and its children vanish with no error.
		Check(Normalize(Vec3{ 0.0f, 0.0f, 0.0f }) == Vec3{}, "normalising a zero vector yields zero, not NaN");

		Check(closeF(Mod(-1.0f, TwoPi), 5.2831855f), "Mod takes the sign of the divisor, so angles wrap forwards");

		// --- matrices ---
		const glm::mat4 gm = glm::translate(glm::mat4(1.0f), ga) *
							 glm::rotate(glm::mat4(1.0f), 0.7f, glm::normalize(gb)) *
							 glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 0.5f, 2.0f));
		const Mat4 m = FromGlm(gm);

		Check(closeM4(m, gm), "a matrix survives the round trip through the bridge");
		Check(std::memcmp(ValuePtr(m), glm::value_ptr(gm), sizeof(float) * 16) == 0,
			  "ValuePtr is byte-identical to glm::value_ptr, so uniform uploads are unchanged");

		const glm::mat4 gn = glm::rotate(glm::mat4(1.0f), -0.35f, glm::vec3(0.0f, 1.0f, 0.0f));
		Check(closeM4(m * FromGlm(gn), gm * gn), "Mat4 * Mat4 matches glm, in the same order");
		Check(closeV4(m * Vec4(a, 1.0f), gm * glm::vec4(ga, 1.0f)), "Mat4 * Vec4 matches glm");
		Check(closeM4(Inverse(m), glm::inverse(gm)), "Inverse matches glm");
		Check(closeM4(Transpose(m), glm::transpose(gm)), "Transpose matches glm");
		Check(closeM4(Translate(m, b), glm::translate(gm, gb)), "Translate matches glm");
		Check(closeM4(Rotate(m, 0.4f, Normalize(b)), glm::rotate(gm, 0.4f, glm::normalize(gb))),
			  "Rotate matches glm");
		Check(closeM4(Scale(m, b), glm::scale(gm, gb)), "Scale matches glm");

		Check(closeM4(Perspective(Radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f),
					  glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f)),
			  "Perspective matches glm");
		Check(closeM4(Orthographic(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f),
					  glm::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f)),
			  "Orthographic matches glm");
		Check(closeM4(LookAt(a, b, Vec3{ 0.0f, 1.0f, 0.0f }),
					  glm::lookAt(ga, gb, glm::vec3(0.0f, 1.0f, 0.0f))),
			  "LookAt matches glm");

		// --- decomposition ---
		{
			Vec3 translation{}, scale{};
			Quat rotation{};
			Check(Decompose(m, translation, rotation, scale), "Decompose succeeds on a valid transform");
			Check(closeV3(translation, ga), "and recovers the translation");
			Check(closeF(scale.x, 1.5f) && closeF(scale.y, 0.5f) && closeF(scale.z, 2.0f),
				  "and the scale");

			// The check that matters: what came apart must go back together.
			const Mat4 rebuilt = Translate(Mat4::Identity(), translation) * ToMat4(rotation) *
								 Scale(Mat4::Identity(), scale);
			Check(closeM4(rebuilt, gm), "and recomposing reproduces the original matrix");
		}

		// --- rotations ---
		const glm::quat gq = glm::angleAxis(0.9f, glm::normalize(gb));
		const Quat q = AngleAxis(0.9f, Normalize(b));

		Check(closeQ(q, gq), "AngleAxis matches glm");
		Check(closeF(Angle(q), glm::angle(gq)), "Angle matches glm");
		Check(closeM4(ToMat4(q), glm::toMat4(gq)), "Quat to Mat4 matches glm");
		Check(closeQ(ToQuat(ToMat4(q)), gq), "Mat4 back to Quat matches glm");
		Check(closeV3(q * a, gq * ga), "rotating a vector matches glm");

		const glm::quat gq2 = glm::angleAxis(-0.4f, glm::vec3(1.0f, 0.0f, 0.0f));
		Check(closeQ(q * FromGlm(gq2), gq * gq2), "Quat * Quat matches glm, in the same order");

		const Vec3 euler{ 0.3f, -1.1f, 0.65f };
		Check(closeQ(FromEuler(euler), glm::quat(ToGlm(euler))), "FromEuler matches glm");
		Check(closeV3(ToEuler(FromEuler(euler)), ToGlm(euler)), "and ToEuler inverts it");

		Check(closeQ(Slerp(q, FromGlm(gq2), 0.35f), glm::slerp(gq, gq2, 0.35f)), "Slerp matches glm");

		// The hemisphere fix. Negating a quaternion leaves the rotation
		// unchanged, so interpolating towards it must take the short way -- and
		// glm::slerp, handed the negated form directly, does not.
		{
			const Quat negated{ -gq2.w, -gq2.x, -gq2.y, -gq2.z };
			Check(closeQ(Slerp(q, negated, 0.35f), glm::slerp(gq, gq2, 0.35f)),
				  "Slerp takes the short way round a negated quaternion");
		}
	}

	// --- hosting the .NET runtime -------------------------------------------
	//
	// Optional by design: the engine builds and runs with no .NET installed, so
	// these checks describe whichever of the two states this machine is in
	// rather than failing on one of them. What is *not* optional is that the
	// unavailable path be honest -- an engine that silently pretends C# works
	// is the failure this whole section exists to catch.
	{
		const std::filesystem::path managed = std::filesystem::path("managed");
		const std::filesystem::path assembly = managed / "RageV.ScriptCore.dll";
		const std::filesystem::path config = managed / "RageV.ScriptCore.runtimeconfig.json";

		const bool staged = std::filesystem::exists(assembly);
		if (!staged)
		{
			RV_CORE_WARN("No managed assembly staged; C# checks describe the unavailable path only");
		}
		else
		{
			// EnableDynamicLoading in the csproj is what produces this, and its
			// absence is the most common reason a first attempt at hosting .NET
			// fails -- with an error code rather than a sentence.
			Check(std::filesystem::exists(config),
				  "the script assembly ships a runtimeconfig.json");
		}

		const std::filesystem::path root = DotNetHost::FindRuntimeRoot();
		const bool booted = DotNetHost::Init(config);

		if (!booted)
		{
			Check(!DotNetHost::IsAvailable(), "an unavailable runtime reports itself unavailable");
			Check(!DotNetHost::GetUnavailableReason().empty(),
				  "and says why, in a sentence fit to show someone");
			RV_CORE_WARN("C# scripting unavailable here: {0}", DotNetHost::GetUnavailableReason());
		}
		else
		{
			Check(DotNetHost::IsAvailable(), "the runtime reports itself available");
			Check(DotNetHost::GetUnavailableReason().empty(), "and offers no reason it should not be");
			Check(!DotNetHost::GetRuntimeVersion().empty(), "the loaded framework version is known");
			Check(!root.empty(), "the installation it came from is known");

			Check(DotNetHost::Init(config), "Init is idempotent");

			using HandshakeFn = int(__cdecl*)(int);
			const auto handshake = (HandshakeFn)DotNetHost::GetFunctionPointer(
				assembly, "RageV.Interop, RageV.ScriptCore", "Handshake");

			Check(handshake != nullptr, "a static managed method binds to a function pointer");

			if (handshake)
			{
				// The whole point of 5.1 in one line: native code called managed
				// code and got an answer back.
				// Against the engine's own constant, not a literal. Hardcoding
				// the version here meant that bumping the protocol -- the one
				// thing the check exists to survive -- broke the check itself.
				const int32_t protocol = Managed::Interop::kProtocolVersion;
				Check(handshake(protocol) == protocol,
					  "native calls managed, and the protocol versions agree");

				// The mismatch path is worth a check of its own. It is the one
				// that fires after a partial rebuild, and it has to report which
				// version it is talking to rather than merely failing.
				Check(handshake(protocol + 97) == -protocol,
					  "a protocol mismatch is reported, with the managed version");
			}

			Check(DotNetHost::GetFunctionPointer(assembly, "RageV.NoSuchType, RageV.ScriptCore",
												 "Handshake") == nullptr,
				  "binding a type that does not exist fails rather than returning something");
			Check(DotNetHost::GetFunctionPointer(assembly, "RageV.Interop, RageV.ScriptCore",
												 "NoSuchMethod") == nullptr,
				  "and so does binding a method that does not exist");
		}

		// --- the interop table, against a real scene -------------------------
		//
		// "The table was handed over" and "the table works" are different
		// claims, and only the second one matters. The managed self-test walks
		// every shape that crosses the boundary and reports each as its own
		// bit, so a failure says which marshalling is wrong rather than that
		// something is.
		if (booted && staged)
		{
			auto scene = std::make_shared<Scene>();
			Entity probe = scene->CreateEntity("InteropProbe");
			probe.GetComponent<TransformComponent>().Position = { 3.0f, 4.0f, 5.0f };

			Managed::Interop::SetScene(scene.get());
			Check(Managed::Interop::Init(assembly), "the interop table is handed to managed code");

			if (Managed::Interop::IsReady())
			{
				using SelfTestFn = int32_t(__cdecl*)(uint64_t);
				const auto selfTest = (SelfTestFn)DotNetHost::GetFunctionPointer(
					assembly, "RageV.Interop, RageV.ScriptCore", "SelfTest");

				Check(selfTest != nullptr, "and the managed self-test binds");

				if (selfTest)
				{
					const int32_t result = selfTest((uint64_t)probe.GetUUID());

					// Reported bit by bit. A single pass/fail here would say
					// "interop is broken" and leave the next person to find out
					// which of nine things it was.
					static const char* const kShapes[] = {
						"the table is bound on the managed side",
						"a struct out-parameter crosses (GetPosition)",
						"a struct in-parameter crosses and sticks (SetPosition)",
						"a string crosses outward (FindEntityByName)",
						"a truncated string still reports the length that would have fit",
						"a string crosses back (GetEntityName)",
						"a float return crosses, from a call with no arguments",
						"a destroyed or unknown entity answers rather than faults",
						"managed code can reach the engine log",
					};

					for (int bit = 0; bit < (int)std::size(kShapes); bit++)
						Check((result & (1 << bit)) != 0, kShapes[bit]);

					Check(result == 511, "every shape that crosses the boundary works");
				}

				// The engine's own view of the table, so a change to it is
				// caught here rather than only in managed code.
				const Managed::NativeApi& api = Managed::Interop::Api();
				Check(api.Log && api.GetPosition && api.GetEntityName && api.GetFixedDeltaTime,
					  "the native table is fully populated");

				Vec3 position{};
				Check(api.GetPosition((uint64_t)probe.GetUUID(), &position) != 0,
					  "and is callable from the native side too");

				// The managed self-test moved it, and the value has to have
				// landed in the actual component rather than in a copy.
				Check(std::fabs(position.x - 1.5f) < 1e-5f &&
					  std::fabs(position.y + 2.25f) < 1e-5f &&
					  std::fabs(position.z - 0.75f) < 1e-5f,
					  "a transform written from C# is the transform the engine reads");

				Check(api.EntityExists(0) == 0, "entity zero is the invalid entity");
			}

			// --- the script lifecycle ----------------------------------------
			//
			// The class library is only real if a Script subclass can be
			// instantiated, stepped, and told about a contact. Spinner is used
			// because the native and managed versions are line-for-line
			// comparable, so this also checks that the two APIs agree.
			if (Managed::Interop::IsReady())
			{
				const Managed::ManagedApi& managed = Managed::Interop::Managed();
				Check(managed.Create && managed.InvokeUpdate && managed.Destroy,
					  "the managed script lifecycle is bound");

				Entity spun = scene->CreateEntity("Spun");
				spun.GetComponent<TransformComponent>().Rotation = Vec3(0.0f);

				const int32_t handle = managed.Create("RageV.Builtin.Spinner", (uint64_t)spun.GetUUID());
				Check(handle != 0, "a C# script instantiates by type name");
				Check(managed.LiveCount() == 1, "and is counted as live");

				Check(managed.Create("RageV.Builtin.NoSuchScript", (uint64_t)spun.GetUUID()) == 0,
					  "a type that does not exist is refused rather than crashing");
				Check(managed.Create("RageV.Interop", (uint64_t)spun.GetUUID()) == 0,
					  "and so is a type that is not a Script");

				if (handle != 0)
				{
					managed.InvokeCreate(handle);

					// Spinner turns 1.2 radians a second about Y. Ten steps of
					// a sixtieth is a fifth of a second.
					for (int step = 0; step < 10; step++)
						managed.InvokeUpdate(handle, 1.0f / 60.0f);

					const Vec3 rotation = spun.GetComponent<TransformComponent>().Rotation;
					Check(std::fabs(rotation.y - (1.2f * 10.0f / 60.0f)) < 1e-4f,
						  "a C# OnUpdate moves the entity it is attached to, by the amount it should");
					Check(rotation.x == 0.0f && rotation.z == 0.0f,
						  "and leaves the other axes alone");

					// A contact, with the same awkward values used elsewhere.
					Managed::CollisionData contact{};
					contact.Other = (uint64_t)probe.GetUUID();
					contact.Trigger = 0;
					contact.Point = Vec3(1.5f, -2.25f, 0.75f);
					contact.Normal = Vec3(0.0f, 1.0f, 0.0f);
					contact.ImpactSpeed = 4.5f;

					// Spinner does not override it, so this proves the *call*
					// arrives and the struct crosses without anything throwing.
					// A script that throws is checked separately below.
					managed.InvokeContact(handle, (int32_t)Managed::ContactKind::CollisionEnter, &contact);
					Check(true, "a contact struct crosses into a script without faulting");

					managed.InvokeDestroy(handle);
					managed.Destroy(handle);
					Check(managed.LiveCount() == 0, "and releasing the handle drops the instance");
				}

				// An unknown handle must be ignored, not indexed.
				managed.InvokeUpdate(9999, 1.0f / 60.0f);
				Check(true, "stepping a handle that was never created is ignored");
			}

			// --- building a project's own scripts -----------------------------
			//
			// The whole point of 5.4: a project scaffolds a .csproj, the editor
			// compiles it, and the result loads. Checked end to end rather than
			// by inspecting the command line, because "we ran dotnet" and "a
			// script type from the project is now instantiable" are very
			// different claims.
			{
				const std::filesystem::path root = ScratchDir("scriptbuild");
				std::error_code ec;
				std::filesystem::remove_all(root, ec);

				Check(Project::Create(root, "Scripted"), "a project to build scripts in");
				const std::filesystem::path csproj =
					Managed::ScriptBuild::ProjectFileFor(root, "Scripted");

				Check(std::filesystem::exists(csproj),
					  "which scaffolds a .csproj named after the project");
				Check(std::filesystem::exists(root / "Scripts" / "Example.cs"),
					  "and a first script that already compiles");

				// The diagnostic parser is the part that quietly stops matching
				// when a toolchain changes its output, so it is checked directly
				// rather than only through a build that happens to fail.
				Managed::BuildDiagnostic parsed;
				Check(Managed::ScriptBuild::ParseDiagnostic(
						  R"(C:\p\Player.cs(12,7): error CS0103: The name 'x' does not exist [C:\p.csproj])",
						  parsed),
					  "an MSBuild error line parses");
				Check(parsed.Line == 12 && parsed.Column == 7 && parsed.Code == "CS0103" && parsed.IsError,
					  "into file, line, column and code");
				Check(parsed.Message == "The name 'x' does not exist",
					  "with the trailing project path stripped from the message");

				Managed::BuildDiagnostic warning;
				Check(Managed::ScriptBuild::ParseDiagnostic(
						  R"(D:\a\B.cs(3,1): warning CS0168: declared but never used)", warning)
					  && !warning.IsError,
					  "and a warning line parses as a warning");

				Check(!Managed::ScriptBuild::ParseDiagnostic("Build succeeded.", parsed),
					  "while an ordinary line is not mistaken for a diagnostic");

				// The C++ module's parser, same reasoning: each of MSVC's three
				// diagnostic shapes, checked directly rather than through a
				// build that happens to fail.
				Managed::BuildDiagnostic cpp;
				Check(ModuleBuild::ParseDiagnostic(
						  R"(C:\p\Rotator.cpp(12,5): error C2065: 'x': undeclared identifier [C:\p\Sample.vcxproj])",
						  cpp)
					  && cpp.Line == 12 && cpp.Column == 5 && cpp.Code == "C2065" && cpp.IsError,
					  "an MSVC compile error parses, project suffix stripped");

				Check(ModuleBuild::ParseDiagnostic(
						  R"(C:\p\Rotator.cpp(31): warning C4189: local variable is initialized but not referenced)",
						  cpp)
					  && cpp.Line == 31 && cpp.Column == 0 && !cpp.IsError,
					  "and one without a column, which older toolsets emit");

				// A delimited raw string: the symbol itself ends in `)"`, which
				// terminates a plain R"(...)" early and eats the next line.
				Check(ModuleBuild::ParseDiagnostic(
						  R"lnk(Rotator.obj : error LNK2019: unresolved external symbol "public: void f(void)")lnk",
						  cpp)
					  && cpp.Line == 0 && cpp.Code == "LNK2019" && cpp.IsError,
					  "a linker error parses, with no line to point at");

				Check(ModuleBuild::ParseDiagnostic(
						  "CMake Error at CMakeLists.txt:33 (message):", cpp)
					  && cpp.Line == 33 && cpp.Code == "CMake" && cpp.IsError,
					  "and a failed configure points at the CMakeLists line");

				Check(!ModuleBuild::ParseDiagnostic("  Rotator.cpp", cpp),
					  "while the compiler naming a file as it goes is not a diagnostic");

				// The process runner under everything above. cmd.exe is on
				// every Windows machine, which makes it the one dependable
				// child for a test.
				{
					const ChildProcess::Result echo = ChildProcess::Run("cmd /c echo hello");
					Check(echo.Launched && echo.ExitCode == 0
						  && echo.Output.find("hello") != std::string::npos,
						  "a child process runs and its output is captured");

					const ChildProcess::Result code = ChildProcess::Run("cmd /c exit 3");
					Check(code.Launched && code.ExitCode == 3,
						  "and its exit code comes back as itself");

					const ChildProcess::Result stderrToo =
						ChildProcess::Run("cmd /c echo lost 1>&2");
					Check(stderrToo.Launched
						  && stderrToo.Output.find("lost") != std::string::npos,
						  "stderr arrives merged with stdout");

					const ChildProcess::Result missing =
						ChildProcess::Run("rv-no-such-tool --version");
					Check(!missing.Launched, "a missing executable reads as not launched");

					// Cancellation, measured: a ping that would sit for ten
					// seconds, cancelled from the start, must come back in
					// far less -- and marked as cancelled, not as failed.
					std::atomic<bool> cancel{ true };
					const auto begun = std::chrono::steady_clock::now();
					const ChildProcess::Result stopped =
						ChildProcess::Run("ping -n 10 127.0.0.1", &cancel);
					const float waited = std::chrono::duration<float>(
						std::chrono::steady_clock::now() - begun).count();

					Check(stopped.Launched && stopped.Cancelled,
						  "a cancelled child reports the cancel");
					Check(waited < 5.0f,
						  "and dies with its whole tree instead of running out the clock");
				}

				Check(std::string(ModuleBuild::Configuration()) == "Debug",
					  "the module config matches this build of the engine");
				Check(ModuleBuild::ModuleFor("C:/proj", "Game")
						  == std::filesystem::path("C:/proj") / "bin" / "Debug" / "Game.dll",
					  "and the module lands in bin/<Config>/<name>.dll");

				// --- registry scopes: what makes a module *unloadable* ----------
				//
				// A factory registered under a scope must leave with it, and one
				// registered under none must survive every unload. This is the
				// invariant that keeps FreeLibrary from leaving dangling function
				// pointers behind a map that still looks correct.
				{
					const int scope = ScriptRegistry::BeginModuleScope();
					ScriptRegistry::Register("ScopedProbe",
											 []() -> ScriptableEntity* { return nullptr; });
					// A name the engine already owns: first registration wins,
					// so the builtin keeps the slot and must survive the
					// scope's unregister untouched.
					ScriptRegistry::Register("Spinner",
											 []() -> ScriptableEntity* { return nullptr; });
					ScriptRegistry::EndModuleScope();

					Check(ScriptRegistry::IsRegistered("ScopedProbe"),
						  "a script registered under a module scope registers");

					const std::vector<std::string> mine = ScriptRegistry::NamesInScope(scope);
					Check(mine.size() == 1 && mine[0] == "ScopedProbe",
						  "and the scope knows exactly what it owns -- not the "
						  "builtin a duplicate name collided with");

					const size_t removed = ScriptRegistry::UnregisterScope(scope);
					Check(removed == 1 && !ScriptRegistry::IsRegistered("ScopedProbe"),
						  "unregistering the scope removes its script");
					Check(ScriptRegistry::IsRegistered("Spinner"),
						  "and leaves the engine's own untouched");

					Check(ScriptRegistry::UnregisterScope(0) == 0,
						  "while scope zero -- the engine itself -- refuses to unregister");
				}

				if (!Managed::ScriptBuild::IsAvailable())
				{
					RV_CORE_WARN("No .NET SDK; the script build itself is not exercised here");
				}
				else
				{
					const std::filesystem::path scriptCore =
						std::filesystem::absolute("managed/RageV.ScriptCore.dll");

					const Managed::BuildResult built = Managed::ScriptBuild::Build(
						csproj, root / "Scripts" / "bin", scriptCore);

					Check(built.Success, "the scaffolded project compiles as generated");
					Check(built.ErrorCount() == 0, "with no errors");
					Check(std::filesystem::exists(built.Assembly),
						  "and produces an assembly named after the project");

					if (!built.Success)
						RV_CORE_ERROR("build output: {0}", built.Output);

					// Loading is a separate claim from building.
					if (built.Success && Managed::Interop::IsReady())
					{
						const int32_t types = Managed::Interop::Managed().LoadAssembly(
							built.Assembly.string().c_str());

						Check(types == 1, "the assembly loads and reports its one script type");

						const int32_t handle = Managed::Interop::Managed().Create(
							"Example", (uint64_t)probe.GetUUID());
						Check(handle != 0, "and a script from the *project* instantiates by name");

						if (handle != 0)
						{
							Managed::Interop::Managed().InvokeUpdate(handle, 1.0f / 60.0f);
							Check(true, "and steps without faulting");

							// --- 5.5: hot reload ---------------------------------
							//
							// With the instance still alive, a reload must refuse:
							// swapping the context under it would keep the old
							// code running behind a new facade.
							Check(Managed::Interop::Managed().LoadAssembly(
									  built.Assembly.string().c_str()) < 0,
								  "a reload with live instances is refused");

							Managed::Interop::Managed().Destroy(handle);
						}

						// A second script, a second build to the *same* file, a
						// second load. Each step is its own claim:
						// - the build succeeding proves the file is not held
						//   open (LoadFrom used to hold it, and the second
						//   build of every session failed);
						// - the load reporting two types proves the reload is
						//   genuine (a same-path reload used to hand back the
						//   stale assembly with one).
						{
							std::ofstream second(root / "Scripts" / "Reloaded.cs");
							second << "using RageV;\n"
								   << "public class Reloaded : Script\n"
								   << "{\n"
								   << "\tpublic override void OnCreate() { }\n"
								   << "}\n";
						}

						const Managed::BuildResult rebuilt = Managed::ScriptBuild::Build(
							csproj, root / "Scripts" / "bin", scriptCore);
						Check(rebuilt.Success,
							  "the assembly rebuilds while loaded -- nothing holds the file");

						if (rebuilt.Success)
						{
							const int32_t reloadedTypes =
								Managed::Interop::Managed().LoadAssembly(
									rebuilt.Assembly.string().c_str());
							Check(reloadedTypes == 2,
								  "and reloading swaps in the new assembly, not the stale one");

							const int32_t reloadedHandle = Managed::Interop::Managed().Create(
								"Reloaded", (uint64_t)probe.GetUUID());
							Check(reloadedHandle != 0,
								  "and a type that did not exist before the reload instantiates");
							if (reloadedHandle != 0)
								Managed::Interop::Managed().Destroy(reloadedHandle);

							// Opening a project must load its built assembly by
							// itself: pressing Build once per session was never
							// the contract, and a packaged game has no Build to
							// press. Scripts loaded only after a build until the
							// first project opened in a fresh editor proved it.
							Check(Project::Load(root / "Scripted.rvproject"),
								  "the project opens again");

							std::string types(512, '\0');
							Managed::Interop::Managed().ListScriptTypes(
								types.data(), (int32_t)types.size());
							Check(types.find("Reloaded") != std::string::npos,
								  "and opening it loaded the built C# on its own");
						}
					}
				}

				std::filesystem::remove_all(root, ec);
				Project::Close();
			}

			// --- script fields, and a C# script on an entity -------------------
			//
			// 5.6: the component exists, its fields come from reflecting the
			// type, and a scene round-trips both. Checked against Spinner,
			// whose one field -- `private float m_Speed = 1.2f` -- is also the
			// case that matters most: private, because that is how C# is
			// written, and defaulted in the initialiser rather than to zero.
			if (Managed::Interop::IsReady())
			{
				const std::vector<Managed::ScriptFieldDesc> fields =
					Managed::Interop::DescribeFields("RageV.Builtin.Spinner");

				Check(fields.size() == 1, "a script type reports its editable fields");

				if (!fields.empty())
				{
					Check(fields[0].Name == "m_Speed", "including private ones, which is how C# is written");
					Check(fields[0].Type == Managed::ScriptFieldType::Float, "with the right type");
					Check(fields[0].Default == "1.2",
						  "and the default from the field initialiser, not zero");
				}

				Check(Managed::Interop::DescribeFields("RageV.Builtin.NoSuchScript").empty(),
					  "an unknown type reports no fields rather than failing");

				// Reading and writing a field on a live instance.
				const Managed::ManagedApi& managed = Managed::Interop::Managed();
				const int32_t handle = managed.Create("RageV.Builtin.Spinner",
													  (uint64_t)probe.GetUUID());
				if (handle != 0)
				{
					std::string readBack(64, '\0');
					Check(managed.GetFieldValue(handle, "m_Speed", readBack.data(),
												(int32_t)readBack.size()) > 0,
						  "a field reads off a live instance");
					Check(std::string(readBack.c_str()) == "1.2", "as the value it actually holds");

					Check(managed.SetFieldValue(handle, "m_Speed", "4") == 1, "and can be written");

					readBack.assign(64, '\0');
					managed.GetFieldValue(handle, "m_Speed", readBack.data(), (int32_t)readBack.size());
					Check(std::string(readBack.c_str()) == "4", "and the write sticks");

					Check(managed.SetFieldValue(handle, "m_NoSuchField", "1") == 0,
						  "while writing a field that does not exist is refused");

					managed.Destroy(handle);
				}
			}

			// --- the component, and a scene round trip --------------------------
			{
				auto authored = std::make_shared<Scene>();
				Entity host = authored->CreateEntity("Scripted");
				auto& component = host.AddComponent<ManagedScriptComponent>("RageV.Builtin.Spinner");
				component.Set("m_Speed", "2.5");

				Check(component.Find("m_Speed") && *component.Find("m_Speed") == "2.5",
					  "a field override is stored on the component");

				component.Set("m_Speed", "3.5");
				Check(component.Fields.Values.size() == 1, "setting the same field twice replaces rather than appends");

				component.Clear("m_Speed");
				Check(component.Fields.Empty(), "and an override can be cleared back to the default");
				component.Set("m_Speed", "2.5");

				SceneSerializer writer(authored);
				const std::string text = writer.SerializeToString();
				Check(text.find("ManagedScriptComponent") != std::string::npos,
					  "the component is written to the scene");
				Check(text.find("m_Speed") != std::string::npos,
					  "with its field overrides");

				auto reloaded = std::make_shared<Scene>();
				SceneSerializer reader(reloaded);
				Check(reader.DeserializeFromString(text), "and the scene loads again");

				Entity restored = reloaded->FindEntityByName("Scripted");
				Check(restored && restored.HasComponent<ManagedScriptComponent>(),
					  "with the component still on the entity");

				if (restored && restored.HasComponent<ManagedScriptComponent>())
				{
					const auto& back = restored.GetComponent<ManagedScriptComponent>();
					Check(back.ScriptName == "RageV.Builtin.Spinner", "and the script it names");
					Check(back.Find("m_Speed") && *back.Find("m_Speed") == "2.5",
						  "and the value somebody typed");
					Check(back.Handle == 0, "and no live handle, which belongs to the run rather than the file");
				}

				// The pass that actually runs them. Spinner turns m_Speed radians
				// a second about Y, so an overridden speed has to be the speed
				// that shows up in the transform -- which is the whole point of
				// the field being editable.
				if (Managed::Interop::IsReady())
				{
					reloaded->OnRuntimeStart();
					Entity spun = reloaded->FindEntityByName("Scripted");
					spun.GetComponent<TransformComponent>().Rotation = Vec3(0.0f);

					for (int step = 0; step < 10; step++)
						reloaded->OnFixedUpdateRuntime(1.0f / 60.0f);

					const float turned = spun.GetComponent<TransformComponent>().Rotation.y;
					Check(std::fabs(turned - (2.5f * 10.0f / 60.0f)) < 1e-4f,
						  "and the scene steps it at the overridden speed, not the default");

					reloaded->OnRuntimeStop();
					Check(reloaded->FindEntityByName("Scripted")
							  .GetComponent<ManagedScriptComponent>().Handle == 0,
						  "and Stop releases the instance rather than leaking the handle");
				}
			}

			// --- contacts reach managed scripts through the scene ------------
			//
			// DeliverContact used to resolve only the native component and
			// return: every C# OnCollision*/OnTrigger* was written, bound,
			// documented and never called from a real simulation. The first
			// C# game found that in an afternoon; this is that afternoon,
			// run backwards.
			if (Managed::Interop::IsReady())
			{
				const Managed::ManagedApi& host = Managed::Interop::Managed();

				auto scene = std::make_shared<Scene>();

				Entity floor = scene->CreateEntity("Floor");
				floor.GetComponent<TransformComponent>().Position = { 0.0f, -1.0f, 0.0f };
				floor.AddComponent<RigidBodyComponent>(BodyType::Static);
				floor.AddComponent<ColliderComponent>().HalfExtents = { 10.0f, 1.0f, 10.0f };

				Entity box = scene->CreateEntity("Box");
				box.GetComponent<TransformComponent>().Position = { 0.0f, 2.0f, 0.0f };
				box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
				box.AddComponent<ColliderComponent>();
				box.AddComponent<ManagedScriptComponent>("RageV.Builtin.ContactCounter");

				scene->OnRuntimeStart();

				// Two seconds: fall from two units, land, settle.
				for (int step = 0; step < 120; step++)
					scene->OnFixedUpdateRuntime(1.0f / 60.0f);

				const int32_t counter = box.GetComponent<ManagedScriptComponent>().Handle;
				Check(counter != 0, "the counter script is alive on the falling box");

				std::string entered(32, '\0');
				const bool read = counter != 0 &&
					host.GetFieldValue(counter, "m_Entered",
									   entered.data(), (int32_t)entered.size()) > 0;
				Check(read && std::atoi(entered.c_str()) >= 1,
					  "a collision in the simulation reaches a managed OnCollisionEnter");

				std::string hardest(32, '\0');
				const bool readHit = counter != 0 &&
					host.GetFieldValue(counter, "m_HardestHit",
									   hardest.data(), (int32_t)hardest.size()) > 0;
				Check(readHit && std::atof(hardest.c_str()) > 0.5,
					  "carrying the impact speed the landing actually had");

				scene->OnRuntimeStop();
			}

			// --- protocol 4: the rest of the surface across the boundary -----
			//
			// Through the table itself rather than through C#, so a failure
			// names the function that broke instead of the script that
			// happened to call it. The C# wrappers are one-line forwards.
			if (Managed::Interop::IsReady())
			{
				auto scene = std::make_shared<Scene>();
				Managed::Interop::SetScene(scene.get());
				const Managed::NativeApi& api = Managed::Interop::Api();

				Entity named = scene->CreateEntity("Original");
				const uint64_t id = (uint64_t)named.GetUUID();

				Check(api.SetEntityName(id, "Renamed") == 1
					  && named.GetComponent<TagComponent>().Name == "Renamed",
					  "a rename through the table reaches the tag component");

				// Two of them, so the plural find means something.
				Entity twin = scene->CreateEntity("Twin");
				scene->CreateEntity("Twin");
				Check(api.FindEntitiesByName("Twin", nullptr, 0) == 2,
					  "the plural find counts without a buffer");
				uint64_t ids[4]{};
				api.FindEntitiesByName("Twin", ids, 4);
				Check(ids[0] != 0 && ids[1] != 0 && ids[0] != ids[1],
					  "and hands both ids across with one");

				// Hierarchy, set and read back across the boundary.
				const uint64_t twinId = (uint64_t)twin.GetUUID();
				api.SetParent(twinId, id);
				Check(api.GetParent(twinId) == id, "a parent set across the boundary reads back");
				Check(api.GetChildren(id, nullptr, 0) == 1, "and the child count agrees");
				api.SetParent(twinId, 0);
				Check(api.GetParent(twinId) == 0, "while parent zero moves it back to the root");

				// LookAt at +X: forward lands on the target direction.
				{
					named.GetComponent<TransformComponent>().Position = Vec3(0.0f);
					Vec3 target(10.0f, 0.0f, 0.0f);
					Vec3 up(0.0f, 1.0f, 0.0f);
					api.LookAt(id, &target, &up);

					Vec3 forward{};
					api.GetForward(id, &forward);
					Check(std::fabs(forward.x - 1.0f) < 1e-3f,
						  "LookAt turns the entity's forward onto the target");

					// Aiming at yourself is a no-op, not a NaN in the transform.
					Vec3 self(0.0f, 0.0f, 0.0f);
					api.LookAt(id, &self, &up);
					Check(std::isfinite(named.GetComponent<TransformComponent>().Rotation.x),
						  "and aiming at your own position changes nothing rather than NaN-ing");
				}

				// Raycasts: honest with no physics, correct with some.
				{
					Managed::RayHitData hit{};
					Vec3 origin(0.0f, 5.0f, 0.0f);
					Vec3 down(0.0f, -10.0f, 0.0f);
					Check(api.Raycast(&origin, &down, &hit) == 0,
						  "a raycast with no physics answers no-hit rather than faulting");

					Entity floor = scene->CreateEntity("RayFloor");
					floor.GetComponent<TransformComponent>().Position = { 0.0f, -1.0f, 0.0f };
					floor.AddComponent<RigidBodyComponent>(BodyType::Static);
					floor.AddComponent<ColliderComponent>().HalfExtents = { 25.0f, 1.0f, 25.0f };

					scene->OnRuntimeStart();
					const int32_t rayHit = api.Raycast(&origin, &down, &hit);
					Check(rayHit == 1 && hit.Entity == (uint64_t)floor.GetUUID(),
						  "and one with a floor under it names the floor");
					Check(std::fabs(hit.Position.y - 0.0f) < 0.05f && hit.Normal.y > 0.9f,
						  "at the surface, with the normal pointing back up the ray");
					scene->OnRuntimeStop();

					// Stop cleared the interop binding -- play mode owns it --
					// so the checks below need the scene bound again.
					Managed::Interop::SetScene(scene.get());
				}

				// Audio's no-op contract: no component and no such clip are
				// answers, not faults. Real playback is the audio suite's job.
				Check(api.PlaySource(id) == 0,
					  "playing a source on an entity without one is a quiet no");
				Check(api.IsSourcePlaying(id) == 0, "which is also not playing");
				Check(api.PlayOneShot(id, "audio/no-such-clip.wav", 1.0f) == 0,
					  "a one-shot with an unknown clip path declines");
				Check(api.PlayOneShot2D("audio/no-such-clip.wav", 1.0f) == 0,
					  "as does an unpositioned one");
				api.StopVoice(0);   // must tolerate a voice that never existed
				Check(true, "and stopping a voice that never existed is harmless");

				// --- protocol 5: pitch, and one-shots from a point -----------
				//
				// The same quiet-no contract for each new entry; audible
				// playback stays the audio suite's job.
				{
					const Vec3 somewhere{ 1.0f, 2.0f, 3.0f };
					Check(api.PlayOneShotAt("audio/no-such-clip.wav", &somewhere, 1.0f, 1.0f) == 0,
						  "a positional one-shot with an unknown clip declines");
					Check(api.PlayOneShotAt("", &somewhere, 1.0f, 1.0f) == 0,
						  "and an empty path from a point has no source clip to fall back on");
					Check(api.PlayOneShotPitched(id, "audio/no-such-clip.wav", 1.0f, 1.5f) == 0,
						  "the pitched entity one-shot keeps the unknown-clip contract");
					Check(api.PlayOneShotPitched(id, "", 1.0f, 1.0f) == 0,
						  "and the empty-path fallback still needs a source component to fall to");
					Check(api.PlayOneShot2DPitched("audio/no-such-clip.wav", 1.0f, 0.5f) == 0,
						  "as does the pitched 2D one");
				}

				Check(api.SpawnPrefab("prefabs/no-such.prefab") == 0,
					  "spawning an unknown prefab declines rather than faulting");

				// --- components, through the registry ------------------------
				//
				// The same registry that drives the inspector, exercised by
				// name and text -- which is exactly what a C# script sends.
				Check(api.HasComponent(id, "TransformComponent") == 1,
					  "a component the entity has answers yes by name");
				Check(api.HasComponent(id, "LightComponent") == 0,
					  "and one it lacks answers no");

				Check(api.AddComponent(id, "LightComponent") == 1,
					  "a component adds by its registry name");
				Check(api.AddComponent(id, "LightComponent") == 0,
					  "adding it twice is refused rather than asserted");
				Check(api.AddComponent(id, "NoSuchComponent") == 0,
					  "as is a name the registry has never heard of");

				Check(api.SetComponentField(id, "TransformComponent", "Position", "1 2 3") == 1,
					  "a field writes from its text form");
				Check(named.GetComponent<TransformComponent>().Position == Vec3(1.0f, 2.0f, 3.0f),
					  "and the write reaches the actual component");

				char text[64]{};
				Check(api.GetComponentField(id, "TransformComponent", "Position", text, 64) > 0
					  && std::string(text) == "1 2 3",
					  "and reads back in the same form");

				Check(api.GetComponentField(id, "TransformComponent", "NoSuchField", nullptr, 0) == -1,
					  "an unknown field is -1, distinguishable from an empty value");

				Check(api.RemoveComponent(id, "TransformComponent") == 0,
					  "an essential component refuses removal, as it does in the inspector");
				Check(api.RemoveComponent(id, "LightComponent") == 1
					  && api.HasComponent(id, "LightComponent") == 0,
					  "while an ordinary one removes and is gone");
			}

			// Managed code must not be able to keep a scene alive past Stop,
			// which is why the binding is a raw pointer and is cleared here.
			Managed::Interop::SetScene(nullptr);
			Check(Managed::Interop::Api().EntityExists((uint64_t)probe.GetUUID()) == 0,
				  "and with no scene bound, every entity lookup answers 'no'");

			Managed::Interop::Shutdown();
			Check(!Managed::Interop::IsReady(), "interop reports itself shut down");
		}

		DotNetHost::Shutdown();
		Check(!DotNetHost::IsAvailable(), "Shutdown leaves the host unavailable");
	}

	// Before teardown, so the game module -- if one loaded -- is freed while
	// the registry that holds its lambdas is still alive. See ~Application
	// for the exit-order crash this avoids.
	Project::Close();

	Audio::Engine::Shutdown();
	Assets::Manager::Shutdown();
	Assets::Registry::Shutdown();
	Renderer::Shutdown();
	device.reset();
	window.reset();

	if (g_Failures > 0)
	{
		RV_CORE_ERROR("{0} check(s) failed", g_Failures);
		return 1;
	}

	RV_CORE_INFO("OK");
	return 0;
}
