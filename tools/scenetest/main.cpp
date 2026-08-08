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
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/GltfImporter.h"

#include <GLFW/glfw3.h>
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
		const std::filesystem::path path = "assets/models/testcube.gltf";

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
	void CheckPrefabs()
	{
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
	AssetRegistry::Init("assets");
	AssetManager::Init(*device);

	RV_CORE_INFO("Scene round-trip test on {0}", device->GetCaps().APIName);

	// --- simulation loop -----------------------------------------------------
	CheckFixedStep();
	CheckInputMap();
	CheckScriptApi();
	CheckLiveScriptChanges();
	CheckPlayModeRestore();

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
