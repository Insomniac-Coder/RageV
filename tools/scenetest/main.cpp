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
#include "RageV/Core/FixedStep.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/KeyCodes.h"
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsDebugDraw.h"
#include "RageV/Physics/ColliderShapes.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Renderer/EditorCamera.h"
#include "RageV/Scene/ScenePicking.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/GltfImporter.h"
#include "RageV/Project/Project.h"

#include <GLFW/glfw3.h>
#include <fstream>
#include <functional>
#include <set>

using namespace RageV;
using namespace RageV::RHI;

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
		grandchild.AddComponent<ColorComponent>(glm::vec4(0.2f, 0.4f, 0.9f, 0.75f));
		scene->SetParent(grandchild, child);

		Entity spot = scene->CreateEntity("Spot Light");
		auto& light = spot.AddComponent<LightComponent>().Light;
		light.Type = Light::LightType::Spot;
		light.Color = { 0.9f, 0.75f, 0.4f };
		light.Intensity = 85.0f;
		light.Range = 22.5f;
		light.InnerCone = 18.0f;
		light.OuterCone = 34.0f;
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
		const glm::mat4 expected = scene->GetWorldTransform(child) *
								   grandchild.GetComponent<TransformComponent>().GetLocalTransform();
		const glm::mat4 actual = grandchild.GetComponent<TransformComponent>().World;

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

		ImportedModel model;
		if (!GltfImporter::Import(path, model))
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

		const ImportedPrimitive& primitive = model.Primitives[0];
		Check(primitive.Vertices.size() == 24, "24 vertices (4 per face, so normals are not shared)");
		Check(primitive.Indices.size() == 36, "36 indices");
		Check(primitive.Material == 0, "the primitive references material 0");

		// A vertex whose normal is zero renders black under every light, which
		// is the single most common symptom of a broken accessor read.
		bool normalsUnit = true;
		bool positionsInRange = true;
		for (const MeshVertex& vertex : primitive.Vertices)
		{
			normalsUnit = normalsUnit && std::fabs(glm::length(vertex.Normal) - 1.0f) < 1e-3f;
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
		Check(std::fabs(model.Nodes[1].Rotation.y - glm::radians(45.0f)) < 1e-3f,
			  "quaternion rotation converts to the right euler angles");
		Check(std::fabs(model.Nodes[1].Scale.x - 0.5f) < 1e-4f, "node scale decodes");
	}

	void CheckModelInstantiation()
	{
		const AssetHandle handle = AssetRegistry::GetHandle("models/testcube.gltf");
		Check(handle.IsValid(), "the registry minted a handle for the model");
		if (!handle.IsValid())
			return;

		Check(AssetRegistry::GetMetadata(handle).Type == AssetType::Mesh,
			  "the model is typed as a mesh");

		auto scene = std::make_shared<Scene>();
		Entity root = AssetManager::InstantiateModel(*scene, handle);
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
			Check((bool)AssetManager::GetMesh(mesh.Mesh), "the mesh handle resolves to GPU geometry");
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
			resolved = resolved || AssetManager::GetMesh(
				reloaded->GetRegistry().get<MeshComponent>(entity).Mesh) != nullptr;

		Check(resolved, "a saved scene's mesh handle still resolves after a reload");
	}

	void CheckPrimitiveHandles()
	{
		PrimitiveType type = PrimitiveType::Sphere;
		Check(AssetManager::IsPrimitive(PrimitiveHandle(PrimitiveType::Cylinder), type) &&
			  type == PrimitiveType::Cylinder,
			  "primitive handles round-trip through the builtin range");

		Check((bool)AssetManager::GetMesh(PrimitiveHandle(PrimitiveType::Sphere)),
			  "a primitive handle resolves to a mesh");

		// A random handle must not be mistaken for a builtin.
		Check(!AssetManager::IsPrimitive(AssetHandle(), type),
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
			m_Root = std::filesystem::temp_directory_path() / (std::string("ragev-") + name);

			std::error_code error;
			std::filesystem::remove_all(m_Root, error);

			if (Project::Create(m_Root, name))
				AssetRegistry::Init(Project::AssetRoot());
		}

		~ScratchProject()
		{
			if (!m_Previous.empty() && Project::Load(m_Previous))
				AssetRegistry::Init(Project::AssetRoot());

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

		const AssetHandle prefab = AssetManager::CreatePrefab(*scene, root, "prefabs/turret.rprefab");
		Check(prefab.IsValid(), "a prefab is written and gets a handle");
		if (!prefab.IsValid())
			return;

		Check(AssetRegistry::GetMetadata(prefab).Type == AssetType::Prefab,
			  "the prefab is typed as a prefab");
		Check(root.HasComponent<PrefabComponent>(),
			  "the source tree becomes an instance of the prefab it produced");

		auto target = std::make_shared<Scene>();
		Entity first = AssetManager::InstantiatePrefab(*target, prefab);
		Entity second = AssetManager::InstantiatePrefab(*target, prefab);

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
			transform.Position += glm::vec3(11.0f, -4.0f, 7.5f);
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
		PhysicsWorld* physics = scene->GetPhysics();
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

		const glm::vec3 velocity = physics->GetLinearVelocity(box.GetUUID());
		Check(glm::length(velocity) < 0.5f, "and settles rather than jittering forever");

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

		scene->OnRuntimeStop();
		Check(scene->GetPhysics() == nullptr, "stop tears the physics world down");
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
			glm::vec3 LastNormal{ 0.0f };
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
			log.LastImpact = glm::max(log.LastImpact, collision.ImpactSpeed);
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
	void CheckProject()
	{
		// Kept, because the checks below open other projects and everything
		// after this expects the sample back.
		const std::filesystem::path original = Project::File();

		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "ragev-project-test";

		std::error_code error;
		std::filesystem::remove_all(root, error);

		Check(Project::Create(root, "Probe"), "a project can be created");
		Check(std::filesystem::exists(root / "Probe.rvproject"),
			  "which writes a .rvproject");
		Check(std::filesystem::is_directory(root / "assets"),
			  "and an assets folder beside it");

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
		AssetRegistry::Init(Project::AssetRoot());
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
		const glm::mat4 cameraTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, 10.0f });

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
		aside.GetComponent<TransformComponent>().Scale = glm::vec3(8.0f);
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
			DrawPhysicsColliders(*probe);
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
		DrawPhysicsColliders(*scene);
		Check(DebugRenderer::GetLineCount() == 0, "a scene with no colliders draws nothing");
		DebugRenderer::EndScene();

		// An entity with a collider but no rigid body still draws: a collider
		// is scene geometry whether or not anything simulates it.
		Entity bare = scene->CreateEntity("Bare");
		bare.AddComponent<ColliderComponent>();
		DebugRenderer::BeginScene(camera, camera.GetTransform());
		DrawPhysicsColliders(*scene);
		Check(DebugRenderer::GetLineCount() == 12, "a collider with no rigid body still draws");
		DebugRenderer::EndScene();

		// The overlay must describe what is simulated, so the sizing has to be
		// the same function the shape is built from.
		{
			ColliderComponent box;
			box.HalfExtents = { 0.5f, 0.5f, 0.5f };
			const ScaledCollider sized = ScaleCollider(box, { 2.0f, 3.0f, 4.0f });
			Check(std::fabs(sized.HalfExtents.x - 1.0f) < 1e-5f &&
				  std::fabs(sized.HalfExtents.y - 1.5f) < 1e-5f &&
				  std::fabs(sized.HalfExtents.z - 2.0f) < 1e-5f,
				  "a box collider takes the entity's scale per axis");

			ColliderComponent sphere(ColliderShape::Sphere);
			sphere.Radius = 1.0f;
			// A sphere has one radius, so the largest axis wins -- enclosing
			// the mesh rather than cutting into it.
			Check(std::fabs(ScaleCollider(sphere, { 2.0f, 3.0f, 1.0f }).Radius - 3.0f) < 1e-5f,
				  "a sphere takes the largest axis, so it encloses rather than clips");

			// A mirrored entity has a size, not a negative size.
			Check(ScaleCollider(box, { -2.0f, 1.0f, 1.0f }).HalfExtents.x > 0.0f,
				  "a negative scale mirrors rather than inverting the shape");
		}
	}

	// Audio, checked through the scene rather than through the mixer.
	//
	// Whether a sound is audible depends on the machine; what the scene does
	// about it must not. AudioEngine allocates and tracks voices with or
	// without an output device precisely so this is testable either way, and
	// every check below holds in both cases.
	void CheckAudio()
	{
		const AssetHandle clip = AssetRegistry::GetHandle("audio/impact.wav");
		Check(clip.IsValid(), "a .wav in the assets folder is registered as an audio asset");
		Check(AssetRegistry::GetMetadata(clip).Type == AssetType::Audio,
			  "and typed from its extension");
		// The demo scene refers to all three by path, and a handle that stops
		// resolving is a silent failure there rather than a loud one.
		Check(AssetRegistry::GetHandle("audio/chime.wav").IsValid() &&
			  AssetRegistry::GetHandle("audio/hum.wav").IsValid(),
			  "as are the other sample clips");

		AudioEngine::StopAll();

		// --- bus volumes ------------------------------------------------------
		AudioEngine::SetBusVolume(AudioBus::Music, 0.25f);
		Check(std::fabs(AudioEngine::GetBusVolume(AudioBus::Music) - 0.25f) < 1e-4f,
			  "a bus volume reads back as it was set");
		Check(std::fabs(AudioEngine::GetBusVolume(AudioBus::SFX) - 1.0f) < 1e-4f,
			  "and does not affect the other buses");
		AudioEngine::SetBusVolume(AudioBus::Music, 1.0f);

		// --- what does and does not produce a voice ---------------------------
		AudioPlayback missing;
		missing.Clip = AssetHandle::Invalid();
		Check(AudioEngine::Play(missing) == 0, "a source with no clip plays nothing");

		AudioPlayback good;
		good.Clip = clip;
		const AudioVoice voice = AudioEngine::Play(good);
		Check(voice != 0, "a valid clip yields a voice");
		Check(AudioEngine::IsPlaying(voice), "which is playing");
		AudioEngine::Stop(voice);
		Check(!AudioEngine::IsPlaying(voice), "and stops when told to");
		Check(AudioEngine::GetVoiceCount() == 0, "leaving nothing behind");

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
		AudioEngine::Update();
		Check(AudioEngine::GetVoiceCount() == 1, "a looping voice survives a frame");

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
		Check(AudioEngine::GetVoiceCount() == 0,
			  "destroying an entity stops the sound it was playing");

		// --- stop silences everything ------------------------------------------
		Entity again = scene->CreateEntity("Again");
		{
			auto& source = again.AddComponent<AudioSourceComponent>();
			source.Clip = clip;
			source.Loop = true;
		}
		scene->OnRuntimeStart();
		Check(AudioEngine::GetVoiceCount() == 1, "restarting play starts it again");

		scene->OnRuntimeStop();
		Check(AudioEngine::GetVoiceCount() == 0, "stopping the scene silences everything");
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
		AudioEngine::StopAll();

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
		source.Clip = AssetRegistry::GetHandle("audio/impact.wav");
		source.PlayOnAwake = false;

		constexpr float dt = 1.0f / 60.0f;
		scene->OnRuntimeStart();

		Check(AudioEngine::GetVoiceCount() == 0,
			  "a source that is not play-on-awake is silent at the start");

		// Falling, not yet landed.
		for (int i = 0; i < 20; i++)
			scene->OnFixedUpdateRuntime(dt);

		Check(AudioEngine::GetVoiceCount() == 0, "and while it is still in the air");

		for (int i = 0; i < 40; i++)
			scene->OnFixedUpdateRuntime(dt);

		Check(AudioEngine::GetVoiceCount() > 0, "landing plays the clip");
		Check(box.GetComponent<AudioSourceComponent>().Voice == 0,
			  "as a one-shot, so a second hit overlaps rather than cutting the first off");

		scene->OnRuntimeStop();
		Check(AudioEngine::GetVoiceCount() == 0, "and stop silences it");
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
		const uint32_t rate = AudioEngine::GetSampleRate();
		const uint32_t channels = AudioEngine::GetChannels();
		if (AudioEngine::GetMode() != AudioMode::Offline || rate == 0 || channels != 2)
		{
			RV_CORE_ERROR("--dump-audio needs the offline mixer");
			return;
		}

		const AssetHandle impact = AssetRegistry::GetHandle("audio/impact.wav");
		const AssetHandle chime = AssetRegistry::GetHandle("audio/chime.wav");
		const AssetHandle hum = AssetRegistry::GetHandle("audio/hum.wav");

		AudioEngine::StopAll();
		AudioEngine::SetListener({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });

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
				const uint64_t got = AudioEngine::RenderFrames(block.data(), blockFrames);
				mix.insert(mix.end(), block.begin(), block.begin() + got * channels);
				AudioEngine::Update();
			}
		};

		// 1. Three impacts at falling volume: the clip plays, and volume works.
		for (int i = 0; i < 3; i++)
		{
			AudioPlayback hit;
			hit.Clip = impact;
			hit.Spatial = false;
			hit.Volume = 1.0f - i * 0.3f;
			AudioEngine::Play(hit);
			advance(0.5f, [](float) {});
		}

		// 2. A chime on its own: a second clip, and a different decode path.
		AudioPlayback bell;
		bell.Clip = chime;
		bell.Spatial = false;
		AudioEngine::Play(bell);
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

		const AudioVoice travelling = AudioEngine::Play(drone);
		constexpr float kSweep = 4.5f;
		advance(kSweep, [&](float t)
		{
			const float x = -10.0f + (t / kSweep) * 20.0f;
			AudioEngine::SetVoicePosition(travelling, { x, 0.0f, -2.0f });
		});
		AudioEngine::Stop(travelling);

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
			const float clamped = glm::clamp(sample, -1.0f, 1.0f);
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
		Check(AudioEngine::GetMode() == AudioMode::Offline, "the offline mixer is running");
		Check(AudioEngine::GetChannels() == 2 && AudioEngine::GetSampleRate() == 48000,
			  "with a stated format, so the mix is the same on every machine");

		const uint32_t channels = AudioEngine::GetChannels();
		const uint32_t rate = AudioEngine::GetSampleRate();
		if (channels != 2 || rate == 0)
			return;

		const AssetHandle clip = AssetRegistry::GetHandle("audio/impact.wav");

		std::vector<float> buffer;

		// Root mean square of one channel: the loudness of the block, rather
		// than of whichever sample the block happened to start on.
		auto render = [&](float seconds, float& leftRms, float& rightRms)
		{
			const uint64_t frames = (uint64_t)(seconds * rate);
			buffer.assign((size_t)frames * channels, 0.0f);

			const uint64_t got = AudioEngine::RenderFrames(buffer.data(), frames);

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
		AudioEngine::StopAll();
		render(0.05f, left, right);
		Check(left == 0.0f && right == 0.0f, "nothing playing renders exact silence");

		// --- a clip actually produces samples --------------------------------
		AudioPlayback flat;
		flat.Clip = clip;
		flat.Spatial = false;
		const AudioVoice loud = AudioEngine::Play(flat);
		render(0.1f, left, right);
		const float loudLevel = glm::max(left, right);

		Check(loudLevel > 0.01f, "playing a clip renders audible samples");
		Check(std::fabs(left - right) < 1e-5f, "and an unpositioned sound is centred");
		AudioEngine::Stop(loud);

		// --- volume scales it ------------------------------------------------
		flat.Volume = 0.25f;
		const AudioVoice quiet = AudioEngine::Play(flat);
		render(0.1f, left, right);
		const float quietLevel = glm::max(left, right);
		AudioEngine::Stop(quiet);

		// A quarter of the amplitude, within a wide tolerance: the block starts
		// at the same point in the same clip, so the two are directly
		// comparable, but resampling makes an exact ratio the wrong thing to
		// demand.
		Check(quietLevel > 0.0f && quietLevel < loudLevel * 0.5f,
			  "volume scales what is rendered");

		// --- 3D positioning pans ---------------------------------------------
		AudioEngine::SetListener({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });

		AudioPlayback placed;
		placed.Clip = clip;
		placed.Spatial = true;
		placed.MinDistance = 1.0f;
		placed.MaxDistance = 100.0f;

		placed.Position = { 5.0f, 0.0f, 0.0f };
		const AudioVoice onTheRight = AudioEngine::Play(placed);
		render(0.1f, left, right);
		AudioEngine::Stop(onTheRight);
		const float rightSideL = left, rightSideR = right;

		placed.Position = { -5.0f, 0.0f, 0.0f };
		const AudioVoice onTheLeft = AudioEngine::Play(placed);
		render(0.1f, left, right);
		AudioEngine::Stop(onTheLeft);
		const float leftSideL = left, leftSideR = right;

		Check(rightSideR > rightSideL * 1.2f, "a sound to the right is louder in the right ear");
		Check(leftSideL > leftSideR * 1.2f, "and one to the left in the left");

		// --- and distance attenuates -----------------------------------------
		// Not `near` and `far`: both are macros in the Windows headers, which
		// turns them into a declaration with no name.
		placed.Position = { 0.0f, 0.0f, -1.0f };
		const AudioVoice closeBy = AudioEngine::Play(placed);
		render(0.1f, left, right);
		const float nearLevel = glm::max(left, right);
		AudioEngine::Stop(closeBy);

		placed.Position = { 0.0f, 0.0f, -60.0f };
		const AudioVoice distant = AudioEngine::Play(placed);
		render(0.1f, left, right);
		const float farLevel = glm::max(left, right);
		AudioEngine::Stop(distant);

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
		AudioEngine::Play(oneShot);

		render(0.5f, left, right);
		Check(glm::max(left, right) > 0.0f, "a one-shot renders");

		AudioEngine::Update();
		Check(AudioEngine::GetVoiceCount() == 0, "and retires itself once it has played out");

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
		source.Clip = AssetRegistry::GetHandle("audio/chime.wav");
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

	if (!glfwInit())
	{
		RV_CORE_ERROR("glfwInit failed");
		return 1;
	}

	if (backend == Backend::Vulkan)
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	}
	else
	{
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	}
	// Nothing is drawn; the device exists only so materials can allocate.
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	GLFWwindow* window = glfwCreateWindow(640, 480, "RageV scene test", nullptr, nullptr);
	if (!window)
	{
		RV_CORE_ERROR("window creation failed");
		return 1;
	}

	DeviceDesc deviceDesc;
	deviceDesc.Backend = backend;
	deviceDesc.Window = window;
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
	AssetRegistry::Init(Project::GetActive() ? Project::AssetRoot()
											 : std::filesystem::path("assets"));
	AssetManager::Init(*device);
	// Opens a real device if there is one. The audio checks are written to
	// hold either way, so a machine with no sound card is not a failing build.
	AudioEngine::Init();

	RV_CORE_INFO("Scene round-trip test on {0}", device->GetCaps().APIName);

	// --- simulation loop -----------------------------------------------------
	CheckFixedStep();
	CheckInputMap();
	CheckScriptApi();
	CheckLiveScriptChanges();

	// --- physics -------------------------------------------------------------
	CheckProject();
	CheckPhysics();
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
	AudioEngine::Shutdown();
	AudioEngine::Init(AudioMode::Silent);
	Check(!AudioEngine::IsAvailable(), "the silent path really is silent");
	CheckAudio();
	CheckCollisionSound();

	// And once more against the mixer itself, where the output can be measured.
	RV_CORE_INFO("Audio again, offline, measuring the mix");
	AudioEngine::Shutdown();
	AudioEngine::Init(AudioMode::Offline);
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

	AudioEngine::Shutdown();
	AssetManager::Shutdown();
	AssetRegistry::Shutdown();
	Renderer::Shutdown();
	device.reset();
	glfwDestroyWindow(window);
	glfwTerminate();

	if (g_Failures > 0)
	{
		RV_CORE_ERROR("{0} check(s) failed", g_Failures);
		return 1;
	}

	RV_CORE_INFO("OK");
	return 0;
}
