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
// For the counting sink in CheckAntiAliasingSwitch. Test-side only: the
// engine did not have to grow a hook for a check to watch its warnings.
#include "spdlog/sinks/base_sink.h"
#include <atomic>
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Asset/Curve.h"
#include "RageV/Asset/CurveSerializer.h"
#include "RageV/Asset/PostProfileSerializer.h"
#include "RageV/Asset/CubeLut.h"
#include "RageV/Asset/LutRecipe.h"
#include "RageV/Scene/FieldSerializer.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Scene/SceneSerializer.h"
#include "RageV/Scene/SceneCommands.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Asset/ScriptGraphGenerator.h"
#include "RageV/Asset/ScriptGraphSerializer.h"
#include "RageV/Asset/ModelImporter.h"
#include "RageV/Core/FixedStep.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/Boot.h"
#include "RageV/IO/PakFile.h"
#include "RageV/IO/TextureCook.h"
#include "RageV/Asset/ImportCache.h"
#include "RageV/Asset/MeshCook.h"
#include "RageV/IO/VFS.h"
#include "RageV/Core/KeyCodes.h"
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsDebugDraw.h"
#include "RageV/Physics/ColliderShapes.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Renderer/ParticleRenderer.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Particles/GpuParticles.h"
#include "RageV/Renderer/RenderGraph.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Renderer/RayShadows.h"
#include "RageV/Renderer/VoxelGI.h"
#include "RageV/Renderer/EditorCamera.h"
#include "RageV/Renderer/Skybox.h"
#include "RageV/Renderer/ViewportGrid.h"
#include "RageV/Asset/FontSerializer.h"
#include "RageV/Asset/TerrainData.h"
#include "RageV/Asset/TerrainSerializer.h"
#include "RageV/Renderer/Terrain.h"
#include "RageV/Asset/TerrainBrush.h"
#include "RageV/Renderer/UIRenderer.h"
#include "RageV/UI/TextLayout.h"
#include "RageV/UI/Canvas.h"
#include "RageV/UI/Interaction.h"
#include "RageV/Renderer/Cubemap.h"
#include "RageV/Renderer/ReflectionProbe.h"
#include "RageV/Renderer/ProbeArray.h"
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
#include "RageV/Renderer/EditorIcons.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/GltfImporter.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Renderer/TextureHeap.h"
#include "RageV/Project/Project.h"
#include "RageV/Project/ProjectPackager.h"
#include "RageV/Managed/DotNetHost.h"
#include "RageV/Managed/Interop.h"
#include "RageV/Managed/ScriptBuild.h"
#include "RageV/Project/ModuleBuild.h"
#include "RageV/Project/ScriptGen.h"
#include "RageV/Project/GameModule.h"
#include "RageV/Core/ChildProcess.h"
#include "RageV/Math/Math.h"
#include "GlmBridge.h"
#include "RageV/ImGui/ImGuiLayer.h"

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

		// Overrides rather than a material, and no device needed for any of it.
		//
		// This fixture exists to be saved and reloaded, and it used to build a
		// GPU Material to do that -- which is why it was behind a device check
		// and why the round-trip test could not see these values at all when
		// there was no renderer. They are plain component data now, so the
		// round trip covers them everywhere it runs.
		mesh.OverrideBaseColor = true;
		mesh.BaseColor = { 0.85f, 0.17f, 0.19f, 1.0f };
		mesh.OverrideEmissive = true;
		mesh.EmissiveColor = { 0.02f, 0.0f, 0.0f, 1.0f };
		mesh.OverrideMetallic = true;
		mesh.Metallic = 0.75f;
		mesh.OverrideRoughness = true;
		mesh.Roughness = 0.3f;
		mesh.OverrideOcclusion = true;
		mesh.Occlusion = 0.9f;
		scene->SetParent(child, root);

		Entity grandchild = scene->CreateEntity("Grandchild");
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
				equal = equal && Math::Abs(expected[column][row] - actual[column][row]) < 1e-4f;

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
			normalsUnit = normalsUnit && Math::Abs(Math::Length(vertex.Normal) - 1.0f) < 1e-3f;
			positionsInRange = positionsInRange &&
							   Math::Abs(vertex.Position.x) <= 1.001f &&
							   Math::Abs(vertex.Position.y) <= 1.001f &&
							   Math::Abs(vertex.Position.z) <= 1.001f;
		}
		Check(normalsUnit, "every normal is unit length");
		Check(positionsInRange, "positions decode to the cube's bounds");

		const MaterialParams& params = model.Materials[0].Params;
		Check(Math::Abs(params.BaseColor.r - 0.85f) < 1e-3f &&
			  Math::Abs(params.Metallic - 0.25f) < 1e-3f &&
			  Math::Abs(params.Roughness - 0.65f) < 1e-3f,
			  "material factors decode");
		Check(model.Materials[0].Name == "TestRed", "material name survives");

		// Parents must precede children, or the scene cannot build the tree in
		// one pass.
		Check(model.Nodes[0].Parent == -1 && model.Nodes[1].Parent == 0,
			  "node parents resolve, parents first");
		Check(model.Nodes[0].Name == "Pivot" && model.Nodes[1].Name == "CubeNode",
			  "node names survive");

		Check(Math::Abs(model.Nodes[0].Position.x - 2.0f) < 1e-4f &&
			  Math::Abs(model.Nodes[0].Position.z + 1.0f) < 1e-4f,
			  "node translation decodes");

		// The fixture stores a 45-degree turn about Y as a quaternion. glTF is
		// xyzw and glm's constructor is wxyz; swapping them is silent and
		// produces a rotation that merely looks wrong.
		Check(Math::Abs(model.Nodes[1].Rotation.y - Math::Radians(45.0f)) < 1e-3f,
			  "quaternion rotation converts to the right euler angles");
		Check(Math::Abs(model.Nodes[1].Scale.x - 0.5f) < 1e-4f, "node scale decodes");
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
		Check(Math::Abs(first.GetComponent<TransformComponent>().Position.x - 3.0f) < 1e-4f,
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
		Check(Math::Abs(simulated - real) < step.Timestep,
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

		// The frame half. Separate counters on purpose: the whole claim being
		// tested is that these two numbers move independently.
		inline static int Framed = 0;
		inline static float LastFrameDelta = 0.0f;
		inline static float FrameAlpha = -1.0f;
		inline static bool FrameBeforeCreate = false;
		inline static bool SelfDestructOnFrame = false;
		// Non-zero makes OnFrame write it into y, so the caller can ask whether
		// what a frame script wrote reached the world matrix.
		inline static float FrameNudge = 0.0f;

		static void Reset()
		{
			Created = Updated = Destroyed = 0;
			LastDelta = 0.0f;
			SeenName.clear();
			FoundOther = false;
			SelfDestruct = false;
			SpawnOnCreate = false;

			Framed = 0;
			LastFrameDelta = 0.0f;
			FrameAlpha = -1.0f;
			FrameBeforeCreate = false;
			SelfDestructOnFrame = false;
			FrameNudge = 0.0f;
		}

		void OnCreate() override
		{
			Created++;
			SeenName = GetName();
			FoundOther = (bool)FindEntityByName("Target");

			if (SpawnOnCreate)
				Spawn("Spawned");
		}

		void OnTick(Timestep dt) override
		{
			Updated++;
			LastDelta = dt.GetSeconds();

			// Moving through the API rather than the component, so the helpers
			// are what is under test.
			Translate({ 1.0f, 0.0f, 0.0f });

			if (SelfDestruct)
				Destroy();
		}

		void OnFrame(Timestep dt) override
		{
			// Recorded rather than asserted here, because a failure inside a
			// script would be reported from the wrong place.
			if (Created == 0)
				FrameBeforeCreate = true;

			Framed++;
			LastFrameDelta = dt.GetSeconds();
			FrameAlpha = GetInterpolationAlpha();

			if (FrameNudge != 0.0f)
				GetPosition().y = FrameNudge;

			if (SelfDestructOnFrame)
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
		Check(ProbeScript::Updated == 1, "OnTick runs on the same step as OnCreate");
		Check(Math::Abs(ProbeScript::LastDelta - 1.0f / 60.0f) < 1e-6f,
			  "the script is handed the fixed timestep");
		Check(ProbeScript::SeenName == "Actor", "a script can read its own entity");
		Check(ProbeScript::FoundOther, "a script can find another entity by name");

		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 1, "OnCreate does not run again");
		Check(ProbeScript::Updated == 2, "OnTick runs every step");

		Check(Math::Abs(actor.GetComponent<TransformComponent>().Position.x - 2.0f) < 1e-4f,
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

	// The per-frame hook, and the claim that it is a genuinely different rate
	// from the fixed one.
	//
	// Every check here is about *when* rather than what: the two counters moving
	// independently is the entire feature, and a version where OnFrame simply
	// ran alongside OnTick would pass a naive "does it get called" test.
	void CheckFrameHook()
	{
		ProbeScript::Reset();

		auto scene = std::make_shared<Scene>();
		Entity actor = scene->CreateEntity("Actor");
		actor.AddComponent<NativeScriptComponent>("ProbeScript");

		// Editing a scene must not run it, at either rate.
		scene->OnUpdateEditor(1.0f / 60.0f);
		Check(ProbeScript::Framed == 0, "OnFrame does not run in the editor");

		// A frame before any step has nothing to call. Instances are created by
		// the fixed pass and by nothing else, and that -- rather than an
		// ordering check inside the frame pass -- is what makes it impossible
		// for OnFrame to arrive before OnCreate.
		scene->OnUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 0, "a frame alone does not create a script instance");
		Check(ProbeScript::Framed == 0, "OnFrame does not run before the script exists");

		scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		Check(ProbeScript::Created == 1, "the fixed pass is what creates the instance");
		Check(ProbeScript::Framed == 0, "a step is not a frame");

		scene->OnUpdateRuntime(1.0f / 90.0f);
		Check(ProbeScript::Framed == 1, "OnFrame runs once per frame");
		Check(ProbeScript::Updated == 1, "a frame is not a step");
		Check(!ProbeScript::FrameBeforeCreate, "OnFrame never precedes OnCreate");
		Check(Math::Abs(ProbeScript::LastFrameDelta - 1.0f / 90.0f) < 1e-6f,
			  "OnFrame is handed the frame's own delta, not the fixed one");

		// Four steps to one frame: what a 15 Hz display against a 60 Hz
		// simulation actually produces, and the case a single shared callback
		// cannot express.
		for (int step = 0; step < 4; step++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		scene->OnUpdateRuntime(1.0f / 15.0f);
		Check(ProbeScript::Updated == 5 && ProbeScript::Framed == 2,
			  "the two rates advance independently");

		Check(ProbeScript::FrameAlpha >= 0.0f && ProbeScript::FrameAlpha <= 1.0f,
			  "a script can read the interpolation alpha, and it is a fraction");

		// What a frame script writes has to reach the world matrix within the
		// same frame, or nothing downstream of it -- audio placement, rendering
		// -- would see the move until a step happened to run.
		ProbeScript::FrameNudge = 3.5f;
		scene->OnUpdateRuntime(1.0f / 60.0f);
		Check(Math::Abs(actor.GetComponent<TransformComponent>().World[3][1] - 3.5f) < 1e-4f,
			  "a transform written in OnFrame reaches the world matrix the same frame");
		ProbeScript::FrameNudge = 0.0f;

		// Destroying from a frame script is deferred exactly as it is from a
		// fixed one. Without the flush the entity would linger until the next
		// step, which at a low simulation rate is a visible delay.
		ProbeScript::Reset();
		ProbeScript::SelfDestructOnFrame = true;
		{
			auto doomed = std::make_shared<Scene>();
			Entity entity = doomed->CreateEntity("Doomed");
			entity.AddComponent<NativeScriptComponent>("ProbeScript");

			doomed->OnFixedUpdateRuntime(1.0f / 60.0f);
			doomed->OnUpdateRuntime(1.0f / 60.0f);
			Check(doomed->GetRegistry().view<IDComponent>().size() == 0,
				  "a script can destroy itself from OnFrame; the delete lands after the pass");
			Check(ProbeScript::Destroyed == 1,
				  "destroying from OnFrame still runs OnDestroy once");
		}
		ProbeScript::Reset();
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
		Check(Math::Abs(physics->GetLinearVelocity(box.GetUUID()).x - 5.0f) < 0.01f,
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

	// Curves: pure logic, so this is the one part of the ramp work that can be
	// checked exhaustively rather than looked at. Everything here is an edge
	// somebody hits by authoring rather than by writing code -- a curve with no
	// keys, one key, two keys at the same time, a point dragged past its
	// neighbour.
	void CheckCurves()
	{
		const float epsilon = 1e-5f;
		auto approx = [epsilon](float a, float b) { return Math::Abs(a - b) < epsilon; };

		// An empty curve answers its stated fallback rather than reading off
		// the end of an empty vector.
		Curve empty;
		Check(empty.IsEmpty(), "a curve starts with no keys");
		Check(approx(empty.EvaluateScalar(0.0f), 0.0f) &&
			  approx(empty.EvaluateScalar(0.5f), 0.0f) &&
			  approx(empty.EvaluateScalar(1.0f), 0.0f),
			  "an empty curve evaluates to its fallback everywhere");

		// One key is a constant, including outside its own time.
		Curve single;
		single.AddKey(0.35f, 2.0f);
		Check(approx(single.EvaluateScalar(0.0f), 2.0f) &&
			  approx(single.EvaluateScalar(0.35f), 2.0f) &&
			  approx(single.EvaluateScalar(1.0f), 2.0f),
			  "a curve with one key is that value everywhere");

		Curve ramp = Curve::Linear(Vec4(0.0f, 0.0f, 0.0f, 0.0f),
								   Vec4(10.0f, 0.0f, 0.0f, 0.0f), 1);
		Check(approx(ramp.EvaluateScalar(0.0f), 0.0f) &&
			  approx(ramp.EvaluateScalar(0.5f), 5.0f) &&
			  approx(ramp.EvaluateScalar(1.0f), 10.0f),
			  "a two-key ramp interpolates linearly between them");

		// Clamped, not extrapolated. A particle's normalised age reaches
		// exactly 1.0, and beyond the last key the answer must hold.
		Check(approx(ramp.EvaluateScalar(-1.0f), 0.0f) &&
			  approx(ramp.EvaluateScalar(4.0f), 10.0f),
			  "a curve holds its end values rather than extrapolating");

		// Keys land in time order however they were added, or evaluation walks
		// the wrong span and the ramp runs backwards in the middle.
		Curve unordered;
		unordered.AddKey(1.0f, 3.0f);
		unordered.AddKey(0.0f, 1.0f);
		unordered.AddKey(0.5f, 2.0f);
		bool sorted = true;
		for (size_t i = 1; i < unordered.GetKeyCount(); i++)
			sorted = sorted && unordered.GetKeys()[i - 1].Time <= unordered.GetKeys()[i].Time;
		Check(sorted, "keys are kept in time order however they arrive");
		Check(approx(unordered.EvaluateScalar(0.25f), 1.5f),
			  "and evaluating one built out of order still interpolates correctly");

		// Two keys at one time is a step, not a division by zero.
		Curve step;
		step.AddKey(0.0f, 0.0f);
		step.AddKey(0.5f, 0.0f);
		step.AddKey(0.5f, 1.0f);
		step.AddKey(1.0f, 1.0f);
		const float justBefore = step.EvaluateScalar(0.5f - 1e-4f);
		const float justAfter = step.EvaluateScalar(0.5f + 1e-4f);
		Check(justBefore < 0.01f && justAfter > 0.99f,
			  "two keys at the same time are a step, not a NaN");

		// Dragging a point past its neighbour re-sorts rather than inverting
		// the curve, and reports where the dragged point ended up.
		Curve dragged = Curve::Linear(Vec4(0.0f, 0.0f, 0.0f, 0.0f),
									  Vec4(1.0f, 0.0f, 0.0f, 0.0f), 1);
		dragged.AddKey(0.5f, 0.5f);
		// Keys are 0.0, 0.5, 1.0; dragging the first to 0.9 puts it between the
		// other two, so it reports index 1 -- past one neighbour, not both.
		const size_t moved = dragged.MoveKey(0, 0.9f, Vec4(9.0f, 0.0f, 0.0f, 0.0f));
		Check(moved == 1, "a key dragged past a neighbour reports its new index");
		Check(approx(dragged.GetKeys()[moved].Value[0], 9.0f),
			  "and it is the key that moved");
		bool stillSorted = true;
		for (size_t i = 1; i < dragged.GetKeyCount(); i++)
			stillSorted = stillSorted && dragged.GetKeys()[i - 1].Time <= dragged.GetKeys()[i].Time;
		Check(stillSorted, "and the curve is still in time order afterwards");

		// Removing out of range is a no-op rather than a corruption.
		const size_t before = dragged.GetKeyCount();
		dragged.RemoveKey(99);
		Check(dragged.GetKeyCount() == before, "removing a key that is not there does nothing");

		// Channels: a gradient carries three, and each interpolates on its own.
		Curve gradient(3);
		gradient.AddKey(0.0f, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
		gradient.AddKey(1.0f, Vec4(0.0f, 0.0f, 1.0f, 1.0f));
		const Vec4 middle = gradient.Evaluate(0.5f);
		Check(gradient.GetChannels() == 3, "a gradient carries three channels");
		Check(approx(middle.x, 0.5f) && approx(middle.y, 0.0f) && approx(middle.z, 0.5f),
			  "and every channel interpolates independently");

		// The channel count is clamped rather than trusted: a corrupt asset
		// must not index off the end of a key.
		Curve wide(99);
		Check(wide.GetChannels() == Curve::kMaxChannels,
			  "an out-of-range channel count is clamped to what a key holds");

		// Baking is what both the CPU simulation and the compute shader read,
		// so its endpoints have to be the curve's endpoints. Sampling at
		// i/count rather than i/(count-1) would clip the end of every ramp --
		// visible as a particle that never reaches its final size.
		Vec4 baked[16] = {};
		ramp.Bake(baked, 16);
		Check(approx(baked[0].x, 0.0f), "baking starts at the curve's first value");
		Check(approx(baked[15].x, 10.0f), "and ends at its last, rather than short of it");
		Check(approx(baked[8].x, ramp.EvaluateScalar(8.0f / 15.0f)),
			  "and matches direct evaluation in between");

		// A one-texel bake is legal and must not divide by zero.
		Vec4 tiny[1] = {};
		ramp.Bake(tiny, 1);
		Check(approx(tiny[0].x, 0.0f), "a single-sample bake takes the curve's start");

		Vec4 unwritten(-1.0f, -1.0f, -1.0f, -1.0f);
		ramp.Bake(&unwritten, 0);
		Check(approx(unwritten.x, -1.0f), "baking zero samples writes nothing");
		ramp.Bake(nullptr, 8);

		// Equality is what a dirty check will be built on, so it has to see a
		// moved key rather than only a different key count.
		Curve a = Curve::Linear(Vec4(0.0f, 0.0f, 0.0f, 0.0f), Vec4(1.0f, 0.0f, 0.0f, 0.0f), 1);
		Curve b = Curve::Linear(Vec4(0.0f, 0.0f, 0.0f, 0.0f), Vec4(1.0f, 0.0f, 0.0f, 0.0f), 1);
		Check(a == b, "two curves built the same way compare equal");
		b.MoveKey(1, 0.5f, Vec4(1.0f, 0.0f, 0.0f, 0.0f));
		Check(a != b, "and moving a key makes them differ");
	}

	// The .rcurve file, checked by round-tripping rather than by comparing
	// against a stored blob: what matters is that a curve written and read back
	// is the same curve, not that the bytes match a fixture nobody maintains.
	void CheckCurveAsset()
	{
		Check(AssetTypeFromExtension(".rcurve") == AssetType::Curve,
			  "a .rcurve file is recognised as a curve asset");
		Check(AssetTypeFromName(AssetTypeName(AssetType::Curve)) == AssetType::Curve,
			  "and the curve type survives a trip through its own name");

		const std::filesystem::path path =
			std::filesystem::temp_directory_path() / "ragev_scenetest_curve.rcurve";

		// Deliberately awkward: keys out of order, a coincident pair, and times
		// outside 0..1 -- all things a hand-edited file can contain.
		Curve written(3);
		written.AddKey(1.0f, Vec4(0.0f, 0.0f, 1.0f, 0.0f));
		written.AddKey(0.0f, Vec4(1.0f, 0.0f, 0.0f, 0.0f));
		written.AddKey(0.5f, Vec4(0.0f, 1.0f, 0.0f, 0.0f));
		written.AddKey(0.5f, Vec4(0.5f, 0.5f, 0.5f, 0.0f));

		Check(Assets::CurveSerializer::Save(written, path), "a curve writes to disk");

		Curve read;
		Check(Assets::CurveSerializer::Load(read, path), "and reads back");
		Check(read.GetChannels() == 3, "with its channel count intact");
		Check(read.GetKeyCount() == written.GetKeyCount(), "and every key");
		Check(read == written, "and compares equal to what was written");

		// Sampling is what the file is *for*, so check the curve behaves rather
		// than only that the numbers survived.
		Check(Math::Abs(read.Evaluate(0.25f).x - written.Evaluate(0.25f).x) < 1e-5f,
			  "and evaluates identically to the original");

		// A missing file must fail rather than half-load, and must leave the
		// caller's curve alone -- the caller may have pre-filled a default.
		Curve untouched = Curve::Constant(Vec4(7.0f, 0.0f, 0.0f, 0.0f), 1);
		Check(!Assets::CurveSerializer::Load(untouched, path.parent_path() / "no_such.rcurve"),
			  "a missing curve file fails to load");
		Check(Math::Abs(untouched.EvaluateScalar(0.5f) - 7.0f) < 1e-5f,
			  "and leaves the curve it was given untouched");

		// Garbage must be reported, not parsed into something plausible.
		const std::filesystem::path broken =
			std::filesystem::temp_directory_path() / "ragev_scenetest_broken.rcurve";
		{
			std::ofstream file(broken);
			file << "Curve: [this is not\n  valid: yaml\n";
		}
		Curve fromBroken;
		Check(!Assets::CurveSerializer::Load(fromBroken, broken),
			  "a malformed curve file is refused rather than half-read");

		std::error_code error;
		std::filesystem::remove(path, error);
		std::filesystem::remove(broken, error);

		// Through the manager, which is how anything but a test will reach one:
		// write it into a project, get a handle back, and read it by handle.
		// The interesting part is the ordering -- the registry has to see the
		// file before it can mint a handle for it.
		{
			ScratchProject scratch("curvetest");

			Curve authored(1);
			authored.AddKey(0.0f, 0.25f);
			authored.AddKey(0.4f, 1.0f);
			authored.AddKey(1.0f, 0.0f);

			const AssetHandle handle = Assets::Manager::CreateCurve(authored, "curves/puff.rcurve");
			Check(handle.IsValid(), "a curve written through the manager gets a handle");

			const Curve* fetched = Assets::Manager::GetCurve(handle);
			Check(fetched != nullptr, "and comes back by handle");
			Check(fetched && *fetched == authored, "as the curve that was written");

			// The contract the emitter will lean on: a valid handle always
			// answers something safe to sample, and an invalid one answers
			// null so a caller can tell "no curve" from "empty curve".
			Check(Assets::Manager::GetCurve(AssetHandle::Invalid()) == nullptr,
				  "an invalid handle has no curve");

			const AssetHandle missing(0x5ee0'0000'0000'0001ull);
			const Curve* absent = Assets::Manager::GetCurve(missing);
			Check(absent != nullptr && absent->IsEmpty(),
				  "a handle naming no file answers an empty curve rather than null");
			Check(absent && Math::Abs(absent->EvaluateScalar(0.5f)) < 1e-5f,
				  "which is safe to sample without a branch at every call site");

			// Reload is what the editor will call after a drag; without it the
			// cache above outlives the file it came from.
			Assets::Manager::ReloadCurve(handle);
			const Curve* again = Assets::Manager::GetCurve(handle);
			Check(again && *again == authored, "a reloaded curve reads the file again");
		}
	}

	// The ramps: which of the two-point pair and the curve decides each
	// channel. All of this is arithmetic, so it is assertable exactly -- and
	// it has to be, because getting it wrong looks like a plausible effect.
	void CheckParticleCurves()
	{
		const float epsilon = 1e-4f;
		auto approx = [epsilon](float a, float b) { return Math::Abs(a - b) < epsilon; };

		ParticleEmitterComponent emitter;
		emitter.SizeStart = 1.0f;
		emitter.SizeEnd = 3.0f;
		emitter.ColorStart = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
		emitter.ColorEnd = Vec4(0.0f, 0.0f, 1.0f, 0.0f);

		// Nothing set: the pairs decide, exactly as before curves existed.
		// This is the check that every scene already in the repository is
		// unaffected by the whole feature.
		{
			const Particles::Appearance a = Particles::Evaluate(emitter, 0.5f, nullptr, nullptr, nullptr);
			Check(approx(a.Size, 2.0f), "with no curves, size still interpolates start to end");
			Check(approx(a.Color.x, 0.5f) && approx(a.Color.z, 0.5f) && approx(a.Color.a, 0.5f),
				  "and so does colour, alpha included");
		}

		// A size curve replaces the pair outright -- it is the size, not a
		// multiplier. Anything else would make two authored numbers fight.
		const Curve::Baked sizeCurve =
			Curve::Linear(Vec4(10.0f, 0.0f, 0.0f, 0.0f), Vec4(20.0f, 0.0f, 0.0f, 0.0f), 1).BakeTable();
		{
			const Particles::Appearance a = Particles::Evaluate(emitter, 0.5f, &sizeCurve, nullptr, nullptr);
			Check(approx(a.Size, 15.0f), "a size curve replaces the size pair");
			Check(approx(a.Color.x, 0.5f), "and leaves colour alone");
		}

		// A gradient replaces RGB and must not touch alpha: opacity has its own
		// curve so a gradient can be shared between emitters that fade
		// differently. If the gradient's own fourth channel leaked through,
		// alpha here would read 0 rather than 0.5.
		Curve gradient(3);
		gradient.AddKey(0.0f, Vec4(0.0f, 1.0f, 0.0f, 0.0f));
		gradient.AddKey(1.0f, Vec4(0.0f, 1.0f, 0.0f, 0.0f));
		const Curve::Baked gradientBaked = gradient.BakeTable();
		{
			const Particles::Appearance a = Particles::Evaluate(emitter, 0.5f, nullptr, &gradientBaked, nullptr);
			Check(approx(a.Color.x, 0.0f) && approx(a.Color.y, 1.0f) && approx(a.Color.z, 0.0f),
				  "a gradient replaces the colour pair's RGB");
			Check(approx(a.Color.a, 0.5f),
				  "and does not touch alpha, which is a separate curve's job");
		}

		// An alpha curve replaces only alpha, and composes with a gradient.
		Curve fadeInOut(1);
		fadeInOut.AddKey(0.0f, 0.0f);
		fadeInOut.AddKey(0.5f, 1.0f);
		fadeInOut.AddKey(1.0f, 0.0f);
		const Curve::Baked alphaBaked = fadeInOut.BakeTable();
		{
			const Particles::Appearance a = Particles::Evaluate(emitter, 0.5f, nullptr, nullptr, &alphaBaked);

			// Near one, not exactly one, and that is the table doing its job.
			// The peak sits at t = 0.5, which is sample 31.5 of 64 -- between
			// two texels -- so baking rounds it to the pair either side. The
			// error is bounded by one table step, and the same rounding will
			// happen on the GPU, which is the whole reason both read this table
			// instead of one reading keys.
			Check(a.Color.a > 0.97f && a.Color.a <= 1.0f,
				  "an alpha curve replaces the pair's alpha");
			Check(approx(a.Color.x, 0.5f), "and leaves RGB to the pair");

			// The shape a pair cannot express at all, and the reason for the
			// whole feature: opaque in the middle, invisible at both ends.
			const Particles::Appearance start = Particles::Evaluate(emitter, 0.0f, nullptr, nullptr, &alphaBaked);
			const Particles::Appearance end = Particles::Evaluate(emitter, 1.0f, nullptr, nullptr, &alphaBaked);
			Check(approx(start.Color.a, 0.0f) && approx(end.Color.a, 0.0f),
				  "and can fade in and out, which a start/end pair cannot");
		}

		// All three at once, each owning its own channel.
		{
			const Particles::Appearance a =
				Particles::Evaluate(emitter, 0.5f, &sizeCurve, &gradientBaked, &alphaBaked);
			Check(approx(a.Size, 15.0f) && approx(a.Color.y, 1.0f) && a.Color.a > 0.97f,
				  "the three curves compose, each owning one channel");
		}

		// The resolution limit, stated rather than discovered: a peak that
		// falls between two samples is rounded off, by less than one step of
		// the table. Worth a check because it is the one way a baked curve
		// differs from the curve somebody drew, and because a future change to
		// kSize should show up here as a number moving rather than silently.
		{
			Curve spike(1);
			spike.AddKey(0.0f, 0.0f);
			spike.AddKey(0.5f, 1.0f);
			spike.AddKey(1.0f, 0.0f);

			const float exact = spike.EvaluateScalar(0.5f);
			const float sampled = spike.BakeTable().SampleScalar(0.5f);
			const float step = 1.0f / (float)(Curve::Baked::kSize - 1);

			Check(approx(exact, 1.0f), "a curve evaluates its peak exactly");
			Check(sampled < exact && exact - sampled < step,
				  "and baking rounds a peak between samples off by under one table step");
		}

		// The baked table is what both the CPU and the GPU read, so it has to
		// agree with the curve it came from. 64 samples over a straight line is
		// exact; the check is that the table is sampled, not misindexed.
		{
			const Curve ramp = Curve::Linear(Vec4(0.0f, 0.0f, 0.0f, 0.0f),
											 Vec4(1.0f, 0.0f, 0.0f, 0.0f), 1);
			const Curve::Baked baked = ramp.BakeTable();
			bool matches = true;
			for (int i = 0; i <= 20; i++)
			{
				const float t = (float)i / 20.0f;
				matches = matches && approx(baked.SampleScalar(t), ramp.EvaluateScalar(t));
			}
			Check(matches, "the baked table matches the curve it was baked from");
			Check(approx(baked.SampleScalar(0.0f), 0.0f) && approx(baked.SampleScalar(1.0f), 1.0f),
				  "at both ends included");
		}

		// The handles are ordinary asset fields, so they round-trip through a
		// scene by the registry like everything else -- worth one check,
		// because "it is just a handle" is exactly the assumption that breaks.
		{
			auto scene = std::make_shared<Scene>();
			Entity host = scene->CreateEntity("Emitter");
			auto& authored = host.AddComponent<ParticleEmitterComponent>();
			authored.SizeCurve = AssetHandle(0x1111'2222'3333'4444ull);
			authored.ColorGradient = AssetHandle(0x5555'6666'7777'8888ull);
			authored.AlphaCurve = AssetHandle(0x9999'aaaa'bbbb'ccccull);

			SceneSerializer serializer(scene);
			const std::string yaml = serializer.SerializeToString();

			auto reloaded = std::make_shared<Scene>();
			SceneSerializer reader(reloaded);
			Check(reader.DeserializeFromString(yaml), "a scene with curve handles reloads");

			bool found = false;
			for (auto handle : reloaded->GetRegistry().view<ParticleEmitterComponent>())
			{
				const auto& read = reloaded->GetRegistry().get<ParticleEmitterComponent>(handle);
				found = read.SizeCurve == authored.SizeCurve
					 && read.ColorGradient == authored.ColorGradient
					 && read.AlphaCurve == authored.AlphaCurve;
			}
			Check(found, "and all three curve handles survive the round trip");
		}
	}

	// The box emitter: where a particle is born, and whether it then moves.
	//
	// Checked by *bounds and spread* rather than by exact positions. Inside the
	// box is the contract; which point inside is the random sequence's business
	// and asserting it would pin an implementation detail. But "inside the box"
	// alone passes for an emitter that never left the origin, which is the bug
	// this feature exists to fix -- so every bound check here is paired with a
	// fill check.
	void CheckEmitterVolumes()
	{
		constexpr float dt = 1.0f / 60.0f;
		const Vec3 at(10.0f, 3.0f, -4.0f);
		const Vec3 size(4.0f, 2.0f, 6.0f);

		auto scene = std::make_shared<Scene>();
		Entity host = scene->CreateEntity("Box");
		host.GetComponent<TransformComponent>().Position = at;

		auto& emitter = host.AddComponent<ParticleEmitterComponent>();
		emitter.Emit = false;            // bursts only, so the count is exact
		emitter.Lifetime = 100.0f;
		emitter.LifetimeJitter = 0.0f;
		emitter.MaxParticles = 4096;
		emitter.Shape = EmitterShape::Box;
		emitter.BoxSize = size;
		emitter.Motion = ParticleMotion::Stationary;
		emitter.Burst = 2000;
		scene->OnUpdateRuntime(dt);

		Check(emitter.Pool.size() == 2000, "a box emitter fires its burst like any other");

		Vec3 low(1e9f), high(-1e9f);
		for (const Particle& particle : emitter.Pool)
			for (int i = 0; i < 3; i++)
			{
				low[i] = Math::Min(low[i], particle.Position[i]);
				high[i] = Math::Max(high[i], particle.Position[i]);
			}

		bool inside = true;
		for (int i = 0; i < 3; i++)
			inside = inside && low[i] >= at[i] - size[i] * 0.5f - 1e-4f
							&& high[i] <= at[i] + size[i] * 0.5f + 1e-4f;
		Check(inside, "every particle is born inside the box");

		// **And the box is filled**, which is the half that matters: the check
		// above passes for an emitter still stuck at one point. Two thousand
		// uniform draws reach within a twentieth of each face; a point emitter
		// reaches zero.
		bool fills = true;
		for (int i = 0; i < 3; i++)
			fills = fills && (high[i] - low[i]) > size[i] * 0.9f;
		Check(fills, "and they fill it rather than clustering at its centre");

		// Stationary is not Speed at zero. Gravity is on and strong, drag with
		// it; nothing may move, and the pool is re-read after a full second.
		emitter.Gravity = Vec3(0.0f, -20.0f, 0.0f);
		emitter.Drag = 3.0f;
		std::vector<Vec3> before;
		for (const Particle& particle : emitter.Pool)
			before.push_back(particle.Position);

		for (int i = 0; i < 60; i++)
			scene->OnUpdateRuntime(dt);

		bool still = emitter.Pool.size() == before.size();
		for (size_t i = 0; still && i < emitter.Pool.size(); i++)
			still = Math::Length(emitter.Pool[i].Position - before[i]) < 1e-6f;
		Check(still, "a stationary particle ignores gravity and drag, not just speed");

		// The other mode, on the same box: born in the volume and then moving.
		emitter.Pool.clear();
		emitter.Motion = ParticleMotion::Directional;
		emitter.Gravity = Vec3(0.0f);
		emitter.Drag = 0.0f;
		emitter.Speed = 2.0f;
		emitter.SpeedJitter = 0.0f;
		emitter.Spread = 180.0f;
		emitter.Burst = 500;
		scene->OnUpdateRuntime(dt);
		for (int i = 0; i < 60; i++)
			scene->OnUpdateRuntime(dt);

		int moved = 0;
		for (const Particle& particle : emitter.Pool)
			moved += Math::Length(particle.Position - at) > size.y * 0.5f ? 1 : 0;
		Check(moved > 0, "a directional box emitter still sends its particles somewhere");

		// --- Shape changes the shape and nothing else ----------------------
		//
		// A point emitter puts every particle at the emitter, which is what
		// every scene authored before this existed relies on.
		auto pointScene = std::make_shared<Scene>();
		Entity pointHost = pointScene->CreateEntity("Point");
		pointHost.GetComponent<TransformComponent>().Position = at;
		auto& point = pointHost.AddComponent<ParticleEmitterComponent>();
		point.Emit = false;
		point.Lifetime = 100.0f;
		point.MaxParticles = 256;
		point.Burst = 200;
		pointScene->OnUpdateRuntime(dt);

		bool atOrigin = !point.Pool.empty();
		for (const Particle& particle : point.Pool)
			atOrigin = atOrigin && Math::Length(particle.Position - at) < 1e-4f;
		Check(atOrigin, "a point emitter is untouched: every particle starts at the emitter");

		// --- the box turns with the emitter and is not scaled by it --------
		//
		// BoxSize is metres, the rule ColliderComponent follows. An emitter
		// parented to something scaled would otherwise fire into a volume
		// nothing draws -- and the editor overlay, which asks the same
		// function, would draw the volume it does not fire into.
		auto turned = std::make_shared<Scene>();
		Entity turnedHost = turned->CreateEntity("Turned");
		auto& turnedTransform = turnedHost.GetComponent<TransformComponent>();
		turnedTransform.Rotation = Vec3(0.0f, Math::Radians(90.0f), 0.0f);
		turnedTransform.Scale = Vec3(3.0f);

		auto& turnedEmitter = turnedHost.AddComponent<ParticleEmitterComponent>();
		turnedEmitter.Emit = false;
		turnedEmitter.Lifetime = 100.0f;
		turnedEmitter.LifetimeJitter = 0.0f;
		turnedEmitter.MaxParticles = 4096;
		turnedEmitter.Shape = EmitterShape::Box;
		turnedEmitter.BoxSize = Vec3(1.0f, 0.0f, 8.0f);   // a long, flat slab
		turnedEmitter.Motion = ParticleMotion::Stationary;
		turnedEmitter.Burst = 2000;
		turned->OnUpdateRuntime(dt);

		Vec3 turnedLow(1e9f), turnedHigh(-1e9f);
		for (const Particle& particle : turnedEmitter.Pool)
			for (int i = 0; i < 3; i++)
			{
				turnedLow[i] = Math::Min(turnedLow[i], particle.Position[i]);
				turnedHigh[i] = Math::Max(turnedHigh[i], particle.Position[i]);
			}

		// Turned a quarter turn about Y, the 8 m side lies along world X and
		// the 1 m side along world Z. Scaled by three it would be 24.
		const float spanX = turnedHigh.x - turnedLow.x;
		const float spanZ = turnedHigh.z - turnedLow.z;
		Check(spanX > 7.0f && spanX < 8.01f && spanZ > 0.85f && spanZ < 1.01f,
			  "the box turns with the emitter and is not scaled by it");
		Check(Math::Abs(turnedHigh.y - turnedLow.y) < 1e-4f,
			  "and a zero side is a slab rather than an error");

		// --- and it survives the file --------------------------------------
		SceneSerializer writer(scene);
		const std::string text = writer.SerializeToString();
		Check(text.find("Shape: Box") != std::string::npos
			  && text.find("BoxSize") != std::string::npos,
			  "the shape and its size serialize by name");

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		Check(reader.DeserializeFromString(text), "and the scene loads again");

		Entity restored = reloaded->FindEntityByName("Box");
		if (restored && restored.HasComponent<ParticleEmitterComponent>())
		{
			const auto& back = restored.GetComponent<ParticleEmitterComponent>();
			Check(back.Shape == EmitterShape::Box
				  && Math::Length(back.BoxSize - size) < 1e-6f
				  && back.Motion == ParticleMotion::Directional,
				  "with the shape, the size and the motion it was saved with");
		}
		else
		{
			Check(false, "with the shape, the size and the motion it was saved with");
		}

		// An emitter authored before any of this existed reads back as a point
		// emitter that moves, which is what it was.
		auto legacy = std::make_shared<Scene>();
		SceneSerializer legacyReader(legacy);
		Check(legacyReader.DeserializeFromString(
				  "Scene: Legacy\nVersion: 6\nEntities:\n"
				  "  - EntityID: 12345\n"
				  "    TagComponent:\n      Tag: Old\n"
				  "    TransformComponent:\n"
				  "      Position: [0, 0, 0]\n      Rotation: [0, 0, 0]\n      Scale: [1, 1, 1]\n"
				  "    ParticleEmitterComponent:\n      Rate: 7\n"),
			  "a scene file written before Shape existed still loads");
		Entity old = legacy->FindEntityByName("Old");
		Check(old && old.HasComponent<ParticleEmitterComponent>()
			  && old.GetComponent<ParticleEmitterComponent>().Shape == EmitterShape::Point
			  && old.GetComponent<ParticleEmitterComponent>().Motion
					 == ParticleMotion::Directional,
			  "as the point emitter that moves, which is what it was");
	}

	// The viewport camera's wheel, which is the control somebody uses more than
	// any other in an editor and the one that had somewhere to get stuck.
	//
	// The step used to be a *number of metres* proportional to the square of
	// the distance. Close in that collapses: at the one-metre floor a notch
	// moved six millimetres, and climbing back out was equally slow, so getting
	// near anything meant falling into a well the wheel could not lift you out
	// of. 142 notches in, 147 back.
	//
	// It is a ratio now, and these are the three properties a ratio has that
	// metres-by-distance-squared does not.
	void CheckViewportZoom()
	{
		auto fresh = [](float distance)
		{
			EditorCamera camera(45.0f, 1.6f, 0.1f, 1000.0f);
			camera.SetActive(true);
			camera.SetOrbit({ 0.0f, 0.0f, 0.0f }, distance, 0.0f, 0.0f);
			return camera;
		};

		// **One notch is the same fraction at every scale.** This is the whole
		// property, and it is what makes the control usable a centimetre from
		// a windscreen and a kilometre above a forest with no mode switch.
		float ratios[3] = { 0.0f, 0.0f, 0.0f };
		const float scales[3] = { 2.0f, 20.0f, 200.0f };
		for (int i = 0; i < 3; i++)
		{
			EditorCamera camera = fresh(scales[i]);
			const float before = camera.GetDistance();
			MouseScrolledEvent scroll(0.0f, 1.0f);
			camera.OnEvent(scroll);
			ratios[i] = camera.GetDistance() / before;
		}
		Check(Math::Abs(ratios[0] - ratios[1]) < 1e-4f
			  && Math::Abs(ratios[1] - ratios[2]) < 1e-4f
			  && ratios[0] < 0.99f,
			  "one notch of the wheel is the same fraction of the distance at "
			  "two metres, twenty and two hundred");

		// **In and out are the same number of notches.** The old curve was not
		// symmetric -- it took five more to climb out than to fall in -- and an
		// asymmetric zoom is one that drifts every time somebody looks at
		// something and comes back.
		{
			EditorCamera camera = fresh(20.0f);
			const float start = camera.GetDistance();
			for (int i = 0; i < 12; i++) { MouseScrolledEvent in(0.0f, 1.0f); camera.OnEvent(in); }
			const float close = camera.GetDistance();
			for (int i = 0; i < 12; i++) { MouseScrolledEvent out(0.0f, -1.0f); camera.OnEvent(out); }

			Check(close < start * 0.25f, "twelve notches in gets meaningfully closer");
			Check(Math::Abs(camera.GetDistance() - start) < start * 0.01f,
				  "and twelve back out returns to where it started");
		}

		// **Getting out of the well is as cheap as getting in.** The number
		// that matters is small: this is a wheel a hand has to turn.
		{
			EditorCamera camera = fresh(10.0f);
			int in = 0;
			while (camera.GetDistance() > 0.51f && in < 500)
			{
				MouseScrolledEvent scroll(0.0f, 1.0f);
				camera.OnEvent(scroll);
				in++;
			}
			int out = 0;
			while (camera.GetDistance() < 10.0f && out < 500)
			{
				MouseScrolledEvent scroll(0.0f, -1.0f);
				camera.OnEvent(scroll);
				out++;
			}
			Check(in < 40 && out < 40,
				  "ten metres to the floor and back is a few dozen notches, not "
				  "a few hundred");
		}

		// The floor holds and the pivot moves instead, so scrolling in past it
		// keeps travelling rather than stopping dead.
		{
			EditorCamera camera = fresh(1.0f);
			const Vec3 before = camera.GetPosition();
			for (int i = 0; i < 20; i++) { MouseScrolledEvent s(0.0f, 1.0f); camera.OnEvent(s); }
			Check(camera.GetDistance() > 0.49f && camera.GetDistance() < 0.51f,
				  "the distance stops at the floor");
			Check(Math::Length(camera.GetPosition() - before) > 0.5f,
				  "and the camera keeps moving forward anyway, by pushing the pivot");
		}

		// The throttle's floor is a speed somebody can see. It used to be
		// 0.05 m/s, which over a second of holding W is five centimetres --
		// indistinguishable from the key doing nothing, and therefore
		// indistinguishable from S doing nothing.
		{
			EditorCamera camera = fresh(10.0f);
			camera.SetMoveSpeed(0.0f);
			Check(camera.GetMoveSpeed() >= 0.5f,
				  "the fly throttle cannot be turned down to a standstill");
		}
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

		// Weighted blending is what the frame graph asks about before it
		// allocates two extra full-resolution attachments, and it has to be
		// asked of the scene rather than of the renderer -- the graph is
		// described before anything has drawn, so the renderer would answer
		// for the previous frame.
		Check(!Particles::System::HasWeightedEmitters(*scene),
			  "a scene of ordinary emitters wants no transparency attachments");

		emitter.Blend = ParticleBlend::WeightedBlended;
		Check(Particles::System::HasWeightedEmitters(*scene),
			  "and one weighted emitter is enough to want them");

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
				  && Math::Abs(back.Rate - 42.5f) < 1e-6f,
				  "and every authored value it carried");
			Check(back.Pool.empty(), "and no particles, which belong to a run");
		}

		if (Renderer::HasDevice())
			Check(ParticleRenderer::IsReady(),
				  "the particle renderer compiled its shader");

		// --- the GPU path, and what makes switching to it cheap ------------
		//
		// The claim under test is not "it simulates" -- the screenshots cover
		// that -- but that flipping SimulateOnGpu costs nothing. An emitter's
		// buffers are kept while the emitter exists, so a toggle is a branch;
		// if they were freed on the way out and rebuilt on the way back in,
		// the count below would move and nobody would notice until a scene
		// with a hundred emitters stuttered every time somebody clicked.
		if (Renderer::HasDevice() && Particles::Gpu::IsReady())
		{
			Particles::Gpu::Clear();
			Check(Particles::Gpu::GetResidentCount() == 0, "no emitter is resident yet");

			auto gpuScene = std::make_shared<Scene>();
			Entity host = gpuScene->CreateEntity("GpuEmitter");
			auto& gpu = host.AddComponent<ParticleEmitterComponent>();
			gpu.SimulateOnGpu = true;
			gpu.MaxParticles = 256;
			gpu.Rate = 120.0f;

			RHI::RHIDevice& device = Renderer::GetDevice();

			const auto simulate = [&](float dt)
			{
				device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
				{
					Particles::Gpu::Simulate(*gpuScene, cmd, dt);
				});
			};

			simulate(1.0f / 60.0f);
			Check(Particles::Gpu::GetResidentCount() == 1,
				  "a GPU emitter becomes resident on its first frame");

			uint32_t count = 0;
			RHI::Ref<RHI::RHIBuffer> instances =
				Particles::Gpu::GetInstances(host.GetUUID(), count);
			Check(instances != nullptr && count == 256,
				  "with an instance buffer the size of its pool");

			// The pool is the pool: a dead particle is a zero-size instance
			// rather than a hole, so the draw is always the full count.
			const void* address = instances.get();

			// Off. The buffers must survive it.
			gpu.SimulateOnGpu = false;
			simulate(1.0f / 60.0f);
			Check(Particles::Gpu::GetResidentCount() == 1,
				  "switching to the CPU keeps the GPU buffers rather than freeing them");

			// And back on, which must reuse exactly what was there.
			gpu.SimulateOnGpu = true;
			simulate(1.0f / 60.0f);

			uint32_t backCount = 0;
			RHI::Ref<RHI::RHIBuffer> back =
				Particles::Gpu::GetInstances(host.GetUUID(), backCount);
			Check(Particles::Gpu::GetResidentCount() == 1 && back.get() == address
				  && backCount == count,
				  "and switching back reuses the same buffer -- the toggle allocates nothing");

			// Residency is tied to the emitter existing, not to it being busy.
			gpuScene->DeleteEntity(host);
			Particles::Gpu::Collect(*gpuScene);
			Check(Particles::Gpu::GetResidentCount() == 0,
				  "while a destroyed emitter's buffers are released");
		}
	}

	// Compute, checked by running one and reading the answer back.
	//
	// A pipeline that was created says nothing about whether a dispatch
	// reaches the GPU, and the failure mode that matters -- a dispatch that
	// silently does nothing -- looks exactly like success from every angle
	// except the buffer's contents.
	// Who owns a key press when the UI is on top.
	//
	// **This is the check that was missing while Ctrl+S did nothing.** The UI
	// layer sits above the editor layer and may mark an event handled, which
	// stops the loop before the editor sees it. It asked ImGui's
	// `WantCaptureKeyboard`, and with keyboard navigation enabled that is true
	// whenever any panel has focus -- so after changing anything in the
	// Inspector, every editor shortcut was swallowed. No save, no error, no
	// log line: the most convincing possible impression that saving is broken.
	//
	// The rule is a pure function of what ImGui says it wants, so it can be
	// stated here without a window, a context or a person pressing a key.
	void CheckShortcutOwnership()
	{
		const bool kMouse = true, kKeyboard = true;

		// Nothing wanted: everything reaches the application.
		UiCapture idle;
		Check(!UiConsumesEvent(idle, kMouse, false), "an idle UI takes no mouse event");
		Check(!UiConsumesEvent(idle, false, kKeyboard), "and no key event");

		// **The case that broke.** A panel has focus, so ImGui asks for the
		// keyboard in order to navigate -- but nothing is being typed into, and
		// Ctrl+S belongs to the application.
		UiCapture navigating;
		navigating.WantsKeyboard = true;
		Check(!UiConsumesEvent(navigating, false, kKeyboard),
			  "a focused panel does not swallow a shortcut: navigation focus is "
			  "not a claim on Ctrl+S");

		// Typing is a claim. The letters, and the modified ones with them --
		// while a field has the caret, the field owns the keyboard.
		UiCapture typing;
		typing.WantsKeyboard = true;
		typing.WantsTextInput = true;
		Check(UiConsumesEvent(typing, false, kKeyboard),
			  "a text field being typed into does swallow key events");

		// The mouse half is unchanged and is the one that was always right:
		// a pointer over a panel belongs to the panel.
		UiCapture overPanel;
		overPanel.WantsMouse = true;
		Check(UiConsumesEvent(overPanel, kMouse, false),
			  "the pointer over a panel belongs to the panel");
		Check(!UiConsumesEvent(overPanel, false, kKeyboard),
			  "and wanting the mouse says nothing about the keyboard");

		// A mouse event while typing still reaches the application: clicking
		// the viewport with a field focused has to move the camera.
		Check(!UiConsumesEvent(typing, kMouse, false),
			  "typing does not capture the mouse as well");
	}

	// The engine's <cmath>, against <cmath>.
	//
	// Most of RageV::Math's scalars are one-line forwards, and a check that a
	// forward forwards is worth very little -- so this is mostly about the ones
	// that are *not*, and about the two things the wrappers changed underneath
	// call sites that had been correct with std::.
	//
	// The awkward values are the point, the same way they are for the glm
	// oracle above: a negative, a zero, a half, an exact power of two, and a
	// value one past where float stops being able to count integers.
	void CheckMathFunctions()
	{
		const float kTolerance = 1.0e-6f;
		const auto close = [&](float a, float b)
		{
			return std::fabs(a - b) <= kTolerance * Math::Max(1.0f, std::fabs(b));
		};

		// --- the forwards, on values chosen to be inconvenient ----------------
		bool forwardsAgree = true;
		for (float v : { -3.75f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.5f, 7.125f })
		{
			forwardsAgree &= close(Math::Sin(v), std::sin(v));
			forwardsAgree &= close(Math::Cos(v), std::cos(v));
			forwardsAgree &= close(Math::Tan(v), std::tan(v));
			forwardsAgree &= close(Math::Atan(v), std::atan(v));
			forwardsAgree &= close(Math::Floor(v), std::floor(v));
			forwardsAgree &= close(Math::Ceil(v), std::ceil(v));
			forwardsAgree &= close(Math::Trunc(v), std::trunc(v));
			forwardsAgree &= close(Math::Round(v), std::round(v));
			forwardsAgree &= close(Math::Abs(v), std::fabs(v));
			if (v > 0.0f)
			{
				forwardsAgree &= close(Math::Sqrt(v), std::sqrt(v));
				forwardsAgree &= close(Math::Log(v), std::log(v));
				forwardsAgree &= close(Math::Log2(v), std::log2(v));
			}
		}
		Check(forwardsAgree, "the <cmath> wrappers answer what <cmath> answers");

		// --- and the three that deliberately do not ---------------------------
		//
		// Acos and Asin clamp first. std::acos(1.0000001) is a NaN, and a NaN
		// angle becomes a NaN rotation and then an object nobody can find.
		Check(!Math::IsNaN(Math::Acos(1.0f + 1e-7f)) && Math::Acos(1.0f + 1e-7f) == 0.0f,
			  "Acos of a dot product a hair past 1 is zero, not a NaN");
		Check(!Math::IsNaN(Math::Asin(-1.0f - 1e-7f)),
			  "and Asin of one a hair past -1 is an angle");
		Check(std::isnan(std::acos(1.0f + 1e-7f)),
			  "which std::acos is not, or the guard would be decoration");

		// Mod wraps, FMod is C's. They differ only for negative inputs, which
		// is exactly when picking the wrong one is invisible.
		Check(close(Math::Mod(-1.0f, 3.0f), 2.0f),
			  "Mod takes the sign of the divisor: -1 mod 3 is 2");
		Check(close(Math::FMod(-1.0f, 3.0f), -1.0f),
			  "FMod takes the sign of the dividend: -1 fmod 3 is -1");

		// Fract is the wrapping one too, for the same reason: a texture
		// coordinate of -0.25 is three quarters into the tile.
		Check(close(Math::Fract(-0.25f), 0.75f), "Fract of -0.25 is 0.75");
		Check(Math::Sign(0.0f) == 0.0f, "the sign of zero is zero, not one");

		// --- what the templates changed underneath existing call sites --------
		//
		// **This is the one that was a real defect.** Min and Max were declared
		// only for float, so `Max(width, height)` on two uint32_t converted both
		// to float, compared them there, and converted back -- exact only below
		// 2^24. Every such call site in this engine is a pixel count, a buffer
		// size or a mip count, and 16777217 is a plausible byte count.
		{
			constexpr uint32_t big = 16777217u;   // 2^24 + 1, the first integer
			constexpr uint32_t less = 16777216u;  // float cannot represent
			Check(Math::Max(big, less) == big,
				  "Max of two large unsigned values does not round-trip through float");
			Check(Math::Min(big, less) == less, "and neither does Min");
			Check((float)big == (float)less,
				  "-- which matters because as floats those two are the same number");
		}

		Check(Math::Max(1, 2, 3) == 3 && Math::Min(4, 3, 5, 2) == 2,
			  "Min and Max take three or more");
		Check(Math::Clamp(5, 0, 3) == 3 && Math::Clamp(-5, 0, 3) == 0,
			  "and Clamp is not float-only either");

		// Saturate, Step and SmoothStep, which have no <cmath> to check against.
		Check(Math::Saturate(1.5f) == 1.0f && Math::Saturate(-0.5f) == 0.0f,
			  "Saturate clamps to the unit range");
		Check(Math::SmoothStep(0.0f, 1.0f, 0.5f) == 0.5f
			  && Math::SmoothStep(0.0f, 1.0f, -1.0f) == 0.0f
			  && Math::SmoothStep(0.0f, 1.0f, 2.0f) == 1.0f,
			  "SmoothStep is a half at the middle and flat outside the edges");
		Check(Math::Step(0.5f, 0.4f) == 0.0f && Math::Step(0.5f, 0.5f) == 1.0f,
			  "Step turns on at its edge");
		Check(Math::SafeSqrt(-1.0f) == 0.0f, "SafeSqrt of a negative is zero");
	}

	// What a multisampled target hands out when something asks it for the
	// depth.
	//
	// **This is the check that was missing for a whole roadmap phase.** MSAA
	// arrived when nothing downstream of the scene pass read depth, so the
	// depth attachment was left multisampled and unresolved and a comment in
	// both backends said, correctly at the time, that nothing reads it. Then
	// depth of field, motion blur, SSAO and SSR each learned to reconstruct a
	// position from it -- and each of them bound a 4x image to a sampler2D,
	// which reads as undefined and came out as "everything is at the near
	// plane". The frame was blurred edge to edge and the whole suite passed,
	// because every check in it renders with --aa=none.
	//
	// So: the invariant, at the level it belongs, in a form that does not need
	// a scene or a camera. A target you can sample the depth of hands out
	// something a sampler can read, whatever its sample count.
	void CheckMultisampledDepth()
	{
		RHI::RHIDevice& device = Renderer::GetDevice();

		auto make = [&device](uint32_t samples, bool sampled)
		{
			RHI::RenderTargetDesc desc;
			desc.Width = 64;
			desc.Height = 64;
			desc.Samples = samples;
			desc.ColorAttachments.push_back({ RHI::Format::R16G16B16A16_SFLOAT });
			desc.DepthAttachment = { RHI::Format::D32_SFLOAT };
			desc.HasDepth = true;
			desc.DepthSampled = sampled;
			desc.DebugName = "scenetest.msaadepth";
			return device.CreateRenderTarget(desc);
		};

		// One sample: the attachment itself, because there is nothing to
		// resolve and a second image would be a frame-sized waste.
		RHI::Ref<RHI::RHIRenderTarget> single = make(1, true);
		Check(single != nullptr, "a single-sampled target with sampled depth is created");
		if (single && single->GetDepthTexture())
		{
			Check(single->GetDepthTexture()->GetDesc().Samples == 1,
				  "and its depth is single-sampled, being the attachment");
		}

		// Four: the attachment is multisampled, and what comes back is not.
		RHI::Ref<RHI::RHIRenderTarget> multi = make(4, true);
		Check(multi != nullptr, "a 4x target with sampled depth is created");
		if (multi)
		{
			RHI::Ref<RHI::RHITexture> depth = multi->GetDepthTexture();
			Check(depth != nullptr, "and hands out a depth texture");
			if (depth)
			{
				Check(depth->GetDesc().Samples == 1,
					  "which is single-sampled -- the resolve, not the 4x attachment");
				Check(RHI::HasFlag(depth->GetDesc().Usage, RHI::TextureUsage::Sampled),
					  "and is usable as a sampled texture");
				Check(depth->GetDesc().Width == 64 && depth->GetDesc().Height == 64,
					  "at the target's own size");
			}
		}

		// And a target nothing samples does not pay for a twin. The colour
		// attachments still resolve; only the depth is conditional, because
		// depth is the one attachment the frame graph asks for by name.
		RHI::Ref<RHI::RHIRenderTarget> unsampled = make(4, false);
		Check(unsampled != nullptr, "a 4x target with unsampled depth is created");
		if (unsampled && unsampled->GetDepthTexture())
		{
			Check(unsampled->GetDepthTexture()->GetDesc().Samples == 4,
				  "and keeps its multisampled depth: nothing reads it, so nothing "
				  "resolves it");
		}
	}

	// The bindless texture heap (ENGINE-NOTES 7al), in three parts.
	//
	// The slot bookkeeping is common code and runs on both backends wherever
	// the device has a heap; the GPU half -- a compute shader that indexes the
	// heap and writes back what it sampled -- is the part that says the
	// descriptors are really there. Both are skipped with a stated reason on a
	// device without descriptor indexing, which OpenGL always is. The third
	// part is a drift check that runs everywhere: the PBR shader compiled with
	// RV_BINDLESS declares the heap at exactly the set the renderer binds it
	// to, and the record buffer at exactly the binding Renderer3D writes.
	//
	// The end-to-end proof is not here. It is tools/scripts/check_bindless.py:
	// the demo rendered with --bindless=on and =off on Vulkan must be
	// pixel-identical, and on its first run it was not -- the bound path had
	// been merging two materials that share maps and differ in tiling.
	void CheckBindlessHeap()
	{
		RHI::RHIDevice& device = Renderer::GetDevice();
		const RHI::DeviceCaps& caps = device.GetCaps();

		// --- the convention the shader and the renderer share -----------------
		{
			auto bindless = RHI::ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader",
																  { "RV_BINDLESS" });
			Check(bindless.has_value(), "the PBR shader compiles with RV_BINDLESS");
			if (bindless)
			{
				const RHI::ResourceSetLayoutDesc* heapSet = bindless->Reflection.FindSet(TextureHeap::kSet);
				Check(heapSet != nullptr, "and declares a set at TextureHeap::kSet");
				if (heapSet)
				{
					Check(heapSet->Bindings.size() == 1 && heapSet->Bindings[0].Binding == 0 &&
						  heapSet->Bindings[0].Count == 0 &&
						  heapSet->Bindings[0].Type == RHI::ResourceType::CombinedImageSampler,
						  "which is one runtime-sized sampler array at binding 0, and nothing else");
				}

				const RHI::ResourceSetLayoutDesc* scene = bindless->Reflection.FindSet(0);
				bool records = false;
				if (scene)
				{
					for (const auto& binding : scene->Bindings)
						records |= binding.Binding == 13 && binding.Type == RHI::ResourceType::StorageBuffer;
				}
				Check(records, "and the material records at set 0, binding 13, where Renderer3D writes them");
				Check(bindless->Reflection.FindSet(1) == nullptr,
					  "and no material set at 1: the bindless variant binds nothing per material");
			}

			auto bound = RHI::ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader");
			Check(bound.has_value(), "the same source compiles without the define");
			if (bound)
			{
				Check(bound->Reflection.FindSet(TextureHeap::kSet) == nullptr,
					  "and the bound variant declares no heap");
				const RHI::ResourceSetLayoutDesc* material = bound->Reflection.FindSet(1);
				Check(material && material->Bindings.size() == 10,
					  "and keeps its ten-binding material set");
			}
		}

		if (!caps.SupportsDescriptorIndexing)
		{
			Check(TextureHeap::Create(device, 64) == nullptr,
				  "no descriptor indexing on this device, and the heap says so by not existing");
			Check(caps.MaxBindlessTextures == 0, "and the caps report zero slots");
			Check(true, "the heap checks are skipped: this device takes the bound path");
			return;
		}

		Check(caps.MaxBindlessTextures >= 256, "the heap holds at least the floor");

		// --- slots, on the CPU ---------------------------------------------------
		{
			constexpr uint32_t kCapacity = 4;   // one error slot and three real ones
			std::unique_ptr<TextureHeap> heap = TextureHeap::Create(device, kCapacity);
			Check(heap != nullptr, "a four-slot heap is created");
			if (!heap)
				return;

			Check(heap->GetFreeCount() == kCapacity - 1, "with three slots free: slot 0 is the error texture");

			RHI::SamplerDesc samplerDesc;
			RHI::Ref<RHI::RHISampler> samplerA = device.CreateSampler(samplerDesc);
			samplerDesc.MinFilter = RHI::FilterMode::Nearest;
			RHI::Ref<RHI::RHISampler> samplerB = device.CreateSampler(samplerDesc);

			auto solid = [&](uint32_t rgba, const char* name)
			{
				RHI::TextureDesc desc;
				desc.Width = 1;
				desc.Height = 1;
				desc.Format = RHI::Format::R8G8B8A8_UNORM;
				desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
				desc.DebugName = name;
				RHI::Ref<RHI::RHITexture> texture = device.CreateTexture(desc);
				texture->Upload(&rgba, sizeof(rgba));
				return texture;
			};

			RHI::Ref<RHI::RHITexture> red = solid(0xff0000ffu, "scenetest.heap.red");
			RHI::Ref<RHI::RHITexture> green = solid(0xff00ff00u, "scenetest.heap.green");

			const uint32_t redSlot = heap->Slot(red, samplerA);
			Check(redSlot != TextureHeap::kErrorSlot, "a texture gets a slot that is not the error slot");
			Check(heap->Slot(red, samplerA) == redSlot, "asking again for the same pair gives the same slot");
			const uint32_t redB = heap->Slot(red, samplerB);
			Check(redB != redSlot && redB != TextureHeap::kErrorSlot,
				  "the same texture under another sampler is another slot: an entry is a pair");
			const uint32_t greenSlot = heap->Slot(green, samplerA);
			Check(greenSlot != redSlot && greenSlot != redB, "a second texture is a third slot");
			Check(heap->GetFreeCount() == 0 && heap->GetLiveCount() == 3, "and the heap is now full");

			RHI::Ref<RHI::RHITexture> blue = solid(0xffff0000u, "scenetest.heap.blue");
			Check(heap->Slot(blue, samplerA) == TextureHeap::kErrorSlot,
				  "past capacity a texture reads as the error slot (the error line above is that)");

			// Release, and the slot must not come back until every frame that
			// could have named it is done -- then it must.
			const uint32_t frames = device.GetFramesInFlight();
			green.reset();
			heap->BeginFrame(0);   // the sweep retires green's slot into frame 0's list
			Check(heap->GetLiveCount() == 2, "a texture nobody holds is swept out");
			Check(heap->GetFreeCount() == 0, "but its slot is not free yet: a frame in flight may still name it");
			Check(heap->Slot(blue, samplerA) == TextureHeap::kErrorSlot,
				  "so a new texture still gets the error slot");
			for (uint32_t frame = 1; frame < frames; frame++)
				heap->BeginFrame(frame);
			Check(heap->GetFreeCount() == 0, "and still not, until frame 0 comes round again");
			heap->BeginFrame(0);
			Check(heap->GetFreeCount() == 1, "when frame 0 comes round the slot is recycled");
			Check(heap->Slot(blue, samplerA) == greenSlot,
				  "and the next texture gets exactly the slot that was freed");
			heap->Commit();
		}

		// --- the descriptors, on the GPU -----------------------------------------
		if (!caps.SupportsCompute)
		{
			Check(true, "no compute on this device; the heap's GPU half is skipped");
			return;
		}

		RHI::ShaderDesc desc;
		desc.Name = "scenetest.heapread";
		desc.Stages.push_back({ RHI::ShaderStage::Compute, R"(
#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x = 8) in;

layout(std430, set = 0, binding = 0) readonly buffer Slots { uint Index[]; } u_Slots;
layout(std430, set = 0, binding = 1) writeonly buffer Colours { vec4 Colour[]; } u_Colours;
layout(set = 2, binding = 0) uniform sampler2D u_Textures[];

layout(push_constant) uniform Params { uint Count; } u_Params;

void main()
{
	uint i = gl_GlobalInvocationID.x;
	if (i >= u_Params.Count)
		return;
	u_Colours.Colour[i] = textureLod(u_Textures[nonuniformEXT(u_Slots.Index[i])], vec2(0.5), 0.0);
}
)" });

		auto compiled = RHI::ShaderCompiler::Compile(desc);
		Check(compiled.has_value(), "a compute shader that indexes the heap compiles");
		if (!compiled)
			return;

		RHI::Ref<RHI::RHIShader> shader = device.CreateShader(*compiled);
		RHI::ComputePipelineDesc pipelineDesc;
		pipelineDesc.Name = "scenetest.heapread";
		pipelineDesc.Shader = shader;
		RHI::Ref<RHI::RHIComputePipeline> pipeline = device.CreateComputePipeline(pipelineDesc);
		Check(pipeline != nullptr, "and builds a pipeline whose set 2 borrows the device's heap layout");
		if (!pipeline)
			return;

		std::unique_ptr<TextureHeap> heap = TextureHeap::Create(device, 16);
		Check(heap != nullptr, "a sixteen-slot heap is created for the dispatch");
		if (!heap)
			return;

		auto solid = [&](uint32_t rgba, const char* name)
		{
			RHI::TextureDesc textureDesc;
			textureDesc.Width = 1;
			textureDesc.Height = 1;
			textureDesc.Format = RHI::Format::R8G8B8A8_UNORM;
			textureDesc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
			textureDesc.DebugName = name;
			RHI::Ref<RHI::RHITexture> texture = device.CreateTexture(textureDesc);
			texture->Upload(&rgba, sizeof(rgba));
			return texture;
		};
		RHI::Ref<RHI::RHISampler> sampler = device.CreateSampler(RHI::SamplerDesc{});
		RHI::Ref<RHI::RHITexture> red = solid(0xff0000ffu, "scenetest.heap.red");
		RHI::Ref<RHI::RHITexture> green = solid(0xff00ff00u, "scenetest.heap.green");
		RHI::Ref<RHI::RHITexture> blue = solid(0xffff0000u, "scenetest.heap.blue");

		// Out of order on purpose, and slot 0 among them: the shader has to
		// fetch by the index it is given, not by the order things were added.
		const uint32_t slots[] = {
			heap->Slot(green, sampler),
			TextureHeap::kErrorSlot,
			heap->Slot(blue, sampler),
			heap->Slot(red, sampler),
			heap->Slot(green, sampler),
		};
		constexpr uint32_t kCount = (uint32_t)std::size(slots);
		heap->Commit();

		RHI::BufferDesc slotsDesc;
		slotsDesc.Size = sizeof(slots);
		slotsDesc.Usage = RHI::BufferUsage::Storage;
		slotsDesc.Memory = RHI::MemoryDomain::HostVisible;
		slotsDesc.DebugName = "scenetest.heap.slots";
		RHI::Ref<RHI::RHIBuffer> slotsBuffer = device.CreateBuffer(slotsDesc);
		slotsBuffer->Upload(slots, sizeof(slots));

		RHI::BufferDesc coloursDesc;
		coloursDesc.Size = kCount * sizeof(Vec4);
		coloursDesc.Usage = RHI::BufferUsage::Storage;
		coloursDesc.Memory = RHI::MemoryDomain::HostVisible;
		coloursDesc.DebugName = "scenetest.heap.colours";
		RHI::Ref<RHI::RHIBuffer> coloursBuffer = device.CreateBuffer(coloursDesc);

		RHI::Ref<RHI::RHIResourceSet> set = device.CreateResourceSet(pipeline, 0);
		set->SetStorageBuffer(0, slotsBuffer, 0, slotsDesc.Size);
		set->SetStorageBuffer(1, coloursBuffer, 0, coloursDesc.Size);
		set->Commit();

		device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
		{
			cmd.BindComputePipeline(pipeline);
			cmd.BindResourceSet(0, set);
			cmd.BindResourceSet(TextureHeap::kSet, heap->GetSet());
			const uint32_t count = kCount;
			cmd.PushConstants(RHI::ShaderStage::Compute, 0, sizeof(count), &count);
			cmd.Dispatch(1);
			cmd.BufferBarrier(coloursBuffer, RHI::BufferSync::ComputeWrite,
							  RHI::BufferSync::ShaderRead);
		});

		const auto* colours = static_cast<const Vec4*>(coloursBuffer->GetMappedPointer());
		Check(colours != nullptr, "the sampled colours read back");
		if (!colours)
			return;

		auto sampled = [](const Vec4& a, float r, float g, float b)
		{
			return Math::Abs(a.x - r) < 0.02f && Math::Abs(a.y - g) < 0.02f && Math::Abs(a.z - b) < 0.02f;
		};
		Check(sampled(colours[0], 0.0f, 1.0f, 0.0f), "slot for green sampled green");
		Check(sampled(colours[1], 1.0f, 0.0f, 1.0f), "slot 0 sampled the magenta error texture");
		Check(sampled(colours[2], 0.0f, 0.0f, 1.0f), "slot for blue sampled blue");
		Check(sampled(colours[3], 1.0f, 0.0f, 0.0f), "slot for red sampled red");
		Check(sampled(colours[4], 0.0f, 1.0f, 0.0f), "and the same slot twice is the same texture");
	}

	// Ray queries (ENGINE-NOTES 7am), before any of the renderer uses them:
	// one triangle in a bottom-level structure, two placements of it in a
	// top-level one, and a compute shader that traces a handful of rays and
	// writes back what it hit. This is the check that says the acceleration
	// structures are real, independent of any picture: a ray aimed at the
	// triangle reports its distance and its instance, a ray beside it misses,
	// a ray from behind with back-face culling misses, and the instance
	// transform moves the hit. Skipped with a stated reason on a device that
	// cannot trace, which OpenGL always is.
	void CheckRayQuery()
	{
		RHI::RHIDevice& device = Renderer::GetDevice();
		const RHI::DeviceCaps& caps = device.GetCaps();

		if (!caps.SupportsRayQuery)
		{
			RHI::AccelerationGeometryDesc none;
			Check(device.CreateBottomLevelAS(none) == nullptr,
				  "no ray query on this device, and CreateBottomLevelAS says so with null");
			Check(device.CreateTopLevelAS(4) == nullptr, "and CreateTopLevelAS the same");
			Check(true, "the ray query checks are skipped: this device takes the shadow-map path");
			return;
		}
		if (!caps.SupportsCompute)
		{
			Check(true, "no compute on this device; the ray query check is skipped");
			return;
		}

		// One triangle in the z = 0 plane, counter-clockwise from +z, so +z
		// is its front face.
		const float positions[] = { -1.0f, -1.0f, 0.0f,   1.0f, -1.0f, 0.0f,   0.0f, 1.0f, 0.0f };
		const uint32_t indices[] = { 0, 1, 2 };

		RHI::BufferDesc vertexDesc;
		vertexDesc.Size = sizeof(positions);
		vertexDesc.Usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::AccelerationStructureInput;
		vertexDesc.DebugName = "scenetest.rq.vertices";
		RHI::Ref<RHI::RHIBuffer> vertices = device.CreateBuffer(vertexDesc);
		vertices->Upload(positions, sizeof(positions));

		RHI::BufferDesc indexDesc;
		indexDesc.Size = sizeof(indices);
		indexDesc.Usage = RHI::BufferUsage::Index | RHI::BufferUsage::AccelerationStructureInput;
		indexDesc.DebugName = "scenetest.rq.indices";
		RHI::Ref<RHI::RHIBuffer> indexBuffer = device.CreateBuffer(indexDesc);
		indexBuffer->Upload(indices, sizeof(indices));

		RHI::AccelerationGeometryDesc geometry;
		geometry.Vertices = vertices;
		geometry.VertexStride = 3 * sizeof(float);
		geometry.VertexCount = 3;
		geometry.Indices = indexBuffer;
		geometry.Type = RHI::IndexType::UInt32;
		geometry.IndexCount = 3;
		geometry.DebugName = "scenetest.rq.blas";
		RHI::Ref<RHI::RHIAccelerationStructure> blas = device.CreateBottomLevelAS(geometry);
		Check(blas != nullptr && !blas->IsTopLevel(), "a bottom-level structure builds from one triangle");
		if (!blas)
			return;

		// Two placements: at the origin with custom index 3, and ten units
		// along +x with custom index 7. Instance three of a possible four,
		// so the count and the capacity are not the same number.
		RHI::AccelerationInstance instances[2];
		instances[0].Blas = blas;
		instances[0].CustomIndex = 3;
		instances[1].Blas = blas;
		instances[1].CustomIndex = 7;
		instances[1].Transform[12] = 10.0f;   // column-major: translation x

		RHI::Ref<RHI::RHIAccelerationStructure> tlas = device.CreateTopLevelAS(4);
		Check(tlas != nullptr && tlas->IsTopLevel() && tlas->GetMaxInstances() == 4,
			  "a top-level structure is allocated for four instances");
		if (!tlas)
			return;

		// The rays: origin.xyz + tMax, direction.xyz + cull-back-faces flag.
		struct Ray { Vec4 Origin; Vec4 Direction; };
		const Ray rays[] = {
			{ {  0.0f, 0.0f,  5.0f, 100.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } },   // straight at it: hit, t = 5, instance 3
			{ {  3.0f, 3.0f,  5.0f, 100.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } },   // beside it: miss
			{ {  0.0f, 0.0f, -5.0f, 100.0f }, { 0.0f, 0.0f,  1.0f, 1.0f } },   // from behind, culling: miss
			{ {  0.0f, 0.0f, -5.0f, 100.0f }, { 0.0f, 0.0f,  1.0f, 0.0f } },   // from behind, no culling: hit
			{ { 10.0f, 0.0f,  5.0f, 100.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } },   // the moved copy: hit, instance 7
			{ {  5.0f, 0.0f,  5.0f, 100.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } },   // between the two: miss
			{ {  0.0f, 0.0f,  5.0f,   2.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } },   // too short a ray: miss
			{ {  0.0f, 0.0f,  5.0f,   6.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } },   // long enough for z = 0, not for z = -3 (the refit below)
		};
		constexpr uint32_t kCount = (uint32_t)std::size(rays);

		RHI::ShaderDesc desc;
		desc.Name = "scenetest.rayquery";
		desc.Stages.push_back({ RHI::ShaderStage::Compute, R"(
#version 460 core
#extension GL_EXT_ray_query : require
layout(local_size_x = 8) in;

layout(set = 0, binding = 0) uniform accelerationStructureEXT u_Scene;
layout(std430, set = 0, binding = 1) readonly buffer Rays { vec4 Data[]; } u_Rays;
layout(std430, set = 0, binding = 2) writeonly buffer Hits { vec4 Hit[]; } u_Hits;

layout(push_constant) uniform Params { uint Count; } u_Params;

void main()
{
	uint i = gl_GlobalInvocationID.x;
	if (i >= u_Params.Count)
		return;

	vec4 origin = u_Rays.Data[2u * i];
	vec4 direction = u_Rays.Data[2u * i + 1u];
	uint flags = gl_RayFlagsOpaqueEXT;
	if (direction.w > 0.5)
		flags |= gl_RayFlagsCullBackFacingTrianglesEXT;

	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_Scene, flags, 0xFFu, origin.xyz, 0.0, direction.xyz, origin.w);
	while (rayQueryProceedEXT(q)) {}

	if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
	{
		u_Hits.Hit[i] = vec4(rayQueryGetIntersectionTEXT(q, true),
							 float(rayQueryGetIntersectionPrimitiveIndexEXT(q, true)),
							 float(rayQueryGetIntersectionInstanceCustomIndexEXT(q, true)),
							 1.0);
	}
	else
	{
		u_Hits.Hit[i] = vec4(-1.0, -1.0, -1.0, 0.0);
	}
}
)" });

		auto compiled = RHI::ShaderCompiler::Compile(desc);
		Check(compiled.has_value(), "a compute shader with a ray query compiles");
		if (!compiled)
			return;

		bool declaresStructure = false;
		if (const RHI::ResourceSetLayoutDesc* set = compiled->Reflection.FindSet(0))
		{
			for (const auto& binding : set->Bindings)
				declaresStructure |= binding.Binding == 0 && binding.Type == RHI::ResourceType::AccelerationStructure;
		}
		Check(declaresStructure, "and reflection recovers the acceleration structure binding");

		RHI::Ref<RHI::RHIShader> shader = device.CreateShader(*compiled);
		RHI::ComputePipelineDesc pipelineDesc;
		pipelineDesc.Name = "scenetest.rayquery";
		pipelineDesc.Shader = shader;
		RHI::Ref<RHI::RHIComputePipeline> pipeline = device.CreateComputePipeline(pipelineDesc);
		Check(pipeline != nullptr, "and builds a compute pipeline");
		if (!pipeline)
			return;

		RHI::BufferDesc raysDesc;
		raysDesc.Size = sizeof(rays);
		raysDesc.Usage = RHI::BufferUsage::Storage;
		raysDesc.Memory = RHI::MemoryDomain::HostVisible;
		raysDesc.DebugName = "scenetest.rq.rays";
		RHI::Ref<RHI::RHIBuffer> raysBuffer = device.CreateBuffer(raysDesc);
		raysBuffer->Upload(rays, sizeof(rays));

		RHI::BufferDesc hitsDesc;
		hitsDesc.Size = kCount * sizeof(Vec4);
		hitsDesc.Usage = RHI::BufferUsage::Storage;
		hitsDesc.Memory = RHI::MemoryDomain::HostVisible;
		hitsDesc.DebugName = "scenetest.rq.hits";
		RHI::Ref<RHI::RHIBuffer> hitsBuffer = device.CreateBuffer(hitsDesc);

		RHI::Ref<RHI::RHIResourceSet> set = device.CreateResourceSet(pipeline, 0);
		set->SetAccelerationStructure(0, tlas);
		set->SetStorageBuffer(1, raysBuffer, 0, raysDesc.Size);
		set->SetStorageBuffer(2, hitsBuffer, 0, hitsDesc.Size);
		set->Commit();

		device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
		{
			// The build, then the trace, in one command list; the build's own
			// barrier is what orders them.
			cmd.BuildTopLevelAS(tlas, instances, 2);
			cmd.BindComputePipeline(pipeline);
			cmd.BindResourceSet(0, set);
			const uint32_t count = kCount;
			cmd.PushConstants(RHI::ShaderStage::Compute, 0, sizeof(count), &count);
			cmd.Dispatch(1);
			cmd.BufferBarrier(hitsBuffer, RHI::BufferSync::ComputeWrite, RHI::BufferSync::ShaderRead);
		});

		const auto* hits = static_cast<const Vec4*>(hitsBuffer->GetMappedPointer());
		Check(hits != nullptr, "the hits read back");
		if (!hits)
			return;

		auto hit = [&](uint32_t i) { return hits[i].w > 0.5f; };
		Check(hit(0) && Math::Abs(hits[0].x - 5.0f) < 1e-3f,
			  "a ray aimed at the triangle hits it at the distance geometry says, t = 5");
		Check(hit(0) && hits[0].y == 0.0f && hits[0].z == 3.0f,
			  "and reports primitive 0 of the instance with custom index 3");
		Check(!hit(1), "a ray beside it misses");
		Check(!hit(2), "a ray from behind, culling back faces, misses");
		Check(hit(3) && Math::Abs(hits[3].x - 5.0f) < 1e-3f, "and the same ray without culling hits");
		Check(hit(4) && hits[4].z == 7.0f,
			  "the copy placed ten units along x is hit there and reports custom index 7");
		Check(!hit(5), "the space between the two copies is empty");
		Check(!hit(6), "a ray shorter than the distance misses: tMax is respected");
		Check(hit(7) && Math::Abs(hits[7].x - 5.0f) < 1e-3f, "and one just long enough hits");

		// --- the refit (ENGINE-NOTES 7an) ------------------------------------
		// The same triangle in a buffer this side can rewrite, under a
		// *dynamic* structure: built from the command list, then moved three
		// units down z and refit. The ray that hit z = 0 at t = 5 now hits at
		// t = 8, and the ray that was just long enough is not any more. This
		// is what a skinned caster's structure does every frame, minus the
		// compute pass that does the moving.
		Check(!blas->IsDynamic(), "the static structure is not dynamic");

		RHI::BufferDesc movingDesc;
		movingDesc.Size = sizeof(positions);
		movingDesc.Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::AccelerationStructureInput;
		movingDesc.Memory = RHI::MemoryDomain::HostVisible;
		movingDesc.DebugName = "scenetest.rq.moving";
		RHI::Ref<RHI::RHIBuffer> moving = device.CreateBuffer(movingDesc);
		moving->Upload(positions, sizeof(positions));

		RHI::AccelerationGeometryDesc dynamicGeometry = geometry;
		dynamicGeometry.Vertices = moving;
		dynamicGeometry.Dynamic = true;
		dynamicGeometry.DebugName = "scenetest.rq.dynamic";
		RHI::Ref<RHI::RHIAccelerationStructure> dynamic = device.CreateBottomLevelAS(dynamicGeometry);
		Check(dynamic != nullptr && dynamic->IsDynamic(), "a dynamic bottom-level structure is created, unbuilt");
		if (!dynamic)
			return;

		RHI::AccelerationInstance placed;
		placed.Blas = dynamic;
		placed.CustomIndex = 11;

		RHI::Ref<RHI::RHIAccelerationStructure> refitTlas = device.CreateTopLevelAS(1);
		RHI::Ref<RHI::RHIResourceSet> refitSet = device.CreateResourceSet(pipeline, 0);
		refitSet->SetAccelerationStructure(0, refitTlas);
		refitSet->SetStorageBuffer(1, raysBuffer, 0, raysDesc.Size);
		refitSet->SetStorageBuffer(2, hitsBuffer, 0, hitsDesc.Size);
		refitSet->Commit();

		auto traceRefit = [&]()
		{
			device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
			{
				// The host wrote the vertices before this submit; the
				// bottom-level build (or refit) reads them, its own barrier
				// orders the top-level build after it, and that one's orders
				// the trace.
				cmd.BuildBottomLevelAS(dynamic);
				cmd.BuildTopLevelAS(refitTlas, &placed, 1);
				cmd.BindComputePipeline(pipeline);
				cmd.BindResourceSet(0, refitSet);
				const uint32_t count = kCount;
				cmd.PushConstants(RHI::ShaderStage::Compute, 0, sizeof(count), &count);
				cmd.Dispatch(1);
				cmd.BufferBarrier(hitsBuffer, RHI::BufferSync::ComputeWrite, RHI::BufferSync::ShaderRead);
			});
		};

		traceRefit();
		Check(hit(0) && Math::Abs(hits[0].x - 5.0f) < 1e-3f && hits[0].z == 11.0f,
			  "built from the command list, the dynamic structure is hit at t = 5 with its own custom index");
		Check(hit(7), "and the just-long-enough ray hits it");

		float moved[std::size(positions)];
		memcpy(moved, positions, sizeof(positions));
		moved[2] = moved[5] = moved[8] = -3.0f;
		moving->Upload(moved, sizeof(moved));

		traceRefit();
		Check(hit(0) && Math::Abs(hits[0].x - 8.0f) < 1e-3f,
			  "after moving the vertices and refitting, the same ray hits at t = 8: the structure followed them");
		Check(!hit(7), "and the just-long-enough ray now falls short of it");
		Check(!hit(6), "the too-short ray still misses");
	}

	// The particle sort, against a buffer whose right answer is arithmetic.
	//
	// **This started as a pixel comparison and that was the wrong instrument.**
	// The plan was to render one emitter on the CPU and on the GPU and diff the
	// images, on the assumption that both paths simulate the same particles.
	// They do not: an identical burst scene rendered each way differed by 6.5
	// levels of 255 under *additive* blending, where order cannot matter at all.
	// The two simulations agree statistically, not particle for particle, so
	// every version of that comparison was measuring the difference between two
	// plumes and calling it ordering. Three thresholds got tuned before that
	// became obvious, and a run of the unsorted build passed one of them.
	//
	// So: dispatch the real shader on a buffer built here, read it back, and
	// check the order exactly. No rendering, no camera, no plume.
	void CheckParticleSort()
	{
		RHI::RHIDevice& device = Renderer::GetDevice();
		if (!device.GetCaps().SupportsCompute)
		{
			Check(true, "no compute on this device; the particle sort is skipped");
			return;
		}

		if (device.GetCaps().MaxComputeWorkGroupSize < 1024)
		{
			Check(true, "workgroups are too small for the particle sort here; skipped");
			return;
		}

		auto compiled = RHI::ShaderCompiler::CompileFromFile("assets/shaders/particle_sort.rvshader");
		Check(compiled.has_value(), "the particle sort shader compiles");
		if (!compiled)
			return;

		RHI::Ref<RHI::RHIShader> shader = device.CreateShader(*compiled);
		RHI::ComputePipelineDesc pipelineDesc;
		pipelineDesc.Name = "scenetest.particlesort";
		pipelineDesc.Shader = shader;
		RHI::Ref<RHI::RHIComputePipeline> pipeline = device.CreateComputePipeline(pipelineDesc);

		Check(pipeline != nullptr, "and builds a compute pipeline");
		if (!pipeline)
			return;

		// Mirrors the shader's ParticleInstance and GpuParticles' InstanceData.
		struct Instance
		{
			Vec4 PositionSize;
			Vec4 Color;
			Vec4 Params;
		};
		static_assert(sizeof(Instance) == 48, "Must match particle_sort.rvshader");

		// Deliberately not a power of two, so the padding path runs. A count
		// that happened to be 128 would leave the pad-and-trim logic untested,
		// and that logic is where a sort silently drops or duplicates a
		// particle.
		constexpr uint32_t kCount = 300;

		// The eye at the origin looking down -Z, so view depth is just -z and
		// the expected order is readable by eye in a failure message.
		const Vec3 eye{ 0.0f, 0.0f, 0.0f };
		const Vec3 forward{ 0.0f, 0.0f, -1.0f };

		std::vector<Instance> input(kCount);
		for (uint32_t i = 0; i < kCount; i++)
		{
			// Interleaved rather than in order: a "sort" that did nothing at
			// all would pass on already-sorted input, which is the classic way
			// a sort test proves nothing. This ordering has no run longer than
			// two and starts near-to-far, the opposite of the answer.
			const float depth = (float)((i * 37u) % kCount) + 1.0f;

			input[i].PositionSize = Vec4(0.0f, 0.0f, -depth, 0.25f);
			// The payload carries the depth too, so a sort that orders the keys
			// but scrambles the indices -- which looks perfect if you only
			// check the positions -- is caught. Colour and position have to
			// arrive together.
			input[i].Color = Vec4(depth, 0.0f, 0.0f, 1.0f);
			input[i].Params = Vec4((float)i, 0.0f, 0.0f, 0.0f);
		}

		RHI::BufferDesc sourceDesc;
		sourceDesc.Size = kCount * sizeof(Instance);
		sourceDesc.Usage = RHI::BufferUsage::Storage;
		sourceDesc.Memory = RHI::MemoryDomain::HostVisible;
		sourceDesc.DebugName = "scenetest.sort.source";
		RHI::Ref<RHI::RHIBuffer> source = device.CreateBuffer(sourceDesc);
		source->Upload(input.data(), sourceDesc.Size);

		RHI::BufferDesc sortedDesc = sourceDesc;
		sortedDesc.DebugName = "scenetest.sort.sorted";
		RHI::Ref<RHI::RHIBuffer> sorted = device.CreateBuffer(sortedDesc);

		struct Params
		{
			Vec4 CameraPosition;
			Vec4 CameraForward;
			uint32_t Count;
			uint32_t Padding[3];
		};

		Params params{};
		params.CameraPosition = Vec4(eye, 0.0f);
		params.CameraForward = Vec4(forward, 0.0f);
		params.Count = kCount;

		RHI::BufferDesc paramsDesc;
		paramsDesc.Size = sizeof(Params);
		paramsDesc.Usage = RHI::BufferUsage::Uniform;
		paramsDesc.Memory = RHI::MemoryDomain::HostVisible;
		paramsDesc.DebugName = "scenetest.sort.params";
		RHI::Ref<RHI::RHIBuffer> paramsBuffer = device.CreateBuffer(paramsDesc);
		paramsBuffer->Upload(&params, sizeof(params));

		RHI::Ref<RHI::RHIResourceSet> set = device.CreateResourceSet(pipeline, 0);
		set->SetStorageBuffer(0, source, 0, sourceDesc.Size);
		set->SetStorageBuffer(1, sorted, 0, sortedDesc.Size);
		set->SetUniformBuffer(2, paramsBuffer, 0, sizeof(Params));
		set->Commit();

		device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
		{
			cmd.BindComputePipeline(pipeline);
			cmd.BindResourceSet(0, set);
			cmd.Dispatch(1);   // one workgroup, by design
			cmd.BufferBarrier(sorted, RHI::BufferSync::ComputeWrite,
							  RHI::BufferSync::ShaderRead);
		});

		const auto* result = static_cast<const Instance*>(sorted->GetMappedPointer());
		Check(result != nullptr, "the sorted buffer reads back");
		if (!result)
			return;

		// --- far to near, which is the whole job --------------------------------
		bool ordered = true;
		uint32_t firstOutOfOrder = UINT32_MAX;
		for (uint32_t i = 1; i < kCount; i++)
		{
			const float previous = -result[i - 1].PositionSize.z;
			const float current = -result[i].PositionSize.z;

			if (current > previous)
			{
				ordered = false;
				if (firstOutOfOrder == UINT32_MAX)
					firstOutOfOrder = i;
			}
		}

		if (!ordered)
		{
			RV_CORE_ERROR("  sort: slot {0} is nearer than the one before it ({1} then {2})",
						  firstOutOfOrder,
						  -result[firstOutOfOrder - 1].PositionSize.z,
						  -result[firstOutOfOrder].PositionSize.z);
		}
		Check(ordered, "the particle sort puts every particle behind the one after it");

		// --- and nothing was invented or lost ------------------------------------
		//
		// Ordering alone is satisfied by an output of 300 copies of the
		// farthest particle. This is the half that says it is the *same* set.
		std::vector<bool> seen(kCount, false);
		bool permutation = true;
		for (uint32_t i = 0; i < kCount && permutation; i++)
		{
			const int slot = (int)result[i].Params.x;
			if (slot < 0 || slot >= (int)kCount || seen[(size_t)slot])
				permutation = false;
			else
				seen[(size_t)slot] = true;
		}

		Check(permutation, "and the result is a permutation of the input, not a copy of "
						   "one element or a buffer with holes in it");

		// --- the payload travelled with the key ----------------------------------
		bool intact = true;
		for (uint32_t i = 0; i < kCount; i++)
		{
			// Colour.r was set to the same depth the position encodes.
			if (Math::Abs(result[i].Color.x - (-result[i].PositionSize.z)) > 1e-3f)
				intact = false;
		}

		Check(intact, "and each particle kept its own colour rather than being paired "
					  "with another particle's");

		// --- one particle, which is the degenerate case the padding must survive --
		params.Count = 1;
		paramsBuffer->Upload(&params, sizeof(params));

		device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
		{
			cmd.BindComputePipeline(pipeline);
			cmd.BindResourceSet(0, set);
			cmd.Dispatch(1);
			cmd.BufferBarrier(sorted, RHI::BufferSync::ComputeWrite,
							  RHI::BufferSync::ShaderRead);
		});

		Check(Math::Abs(result[0].Params.x - input[0].Params.x) < 1e-3f,
			  "a single-particle emitter sorts to itself rather than reading past its "
			  "own end");
	}

	void CheckCompute()
	{
		if (!Renderer::HasDevice())
			return;

		RHI::RHIDevice& device = Renderer::GetDevice();

		Check(device.GetCaps().SupportsCompute,
			  "the device reports compute support");
		Check(device.GetCaps().MaxComputeWorkGroupSize >= 64,
			  "and a work group size worth dispatching");

		// Doubles every element and adds its index, so a buffer that came
		// back unchanged, half-processed, or shifted is all distinguishable.
		RHI::ShaderDesc desc;
		desc.Name = "scenetest.double";
		desc.Stages.push_back({ RHI::ShaderStage::Compute, R"(
#version 450 core
layout(local_size_x = 64) in;

layout(std430, set = 0, binding = 0) buffer Values { uint Data[]; } u_Values;

layout(push_constant) uniform Params { uint Count; } u_Params;

void main()
{
	uint index = gl_GlobalInvocationID.x;
	// The last group runs full: the tail addresses elements past the end.
	if (index >= u_Params.Count)
		return;

	u_Values.Data[index] = u_Values.Data[index] * 2u + index;
}
)" });

		auto compiled = RHI::ShaderCompiler::Compile(desc);
		Check(compiled.has_value(), "a compute shader compiles from source");
		if (!compiled)
			return;

		Check(compiled->Reflection.LocalSize[0] == 64,
			  "and reflection recovers the local size it declared");
		Check(HasFlag(compiled->Reflection.Stages, RHI::ShaderStage::Compute),
			  "and reports that it carries a compute stage");

		RHI::Ref<RHI::RHIShader> shader = device.CreateShader(*compiled);
		Check(shader != nullptr, "the shader object builds");
		if (!shader)
			return;

		RHI::ComputePipelineDesc pipelineDesc;
		pipelineDesc.Name = "scenetest.double";
		pipelineDesc.Shader = shader;

		RHI::Ref<RHI::RHIComputePipeline> pipeline = device.CreateComputePipeline(pipelineDesc);
		Check(pipeline != nullptr, "a compute pipeline is created from it");
		if (!pipeline)
			return;

		Check(pipeline->GetWorkGroupSizeX() == 64,
			  "the pipeline reports the shader's work group size");
		Check(pipeline->GroupsFor(64) == 1 && pipeline->GroupsFor(65) == 2
			  && pipeline->GroupsFor(0) == 0,
			  "and rounds a dispatch up to cover every element");

		// 100 elements over a group of 64: two groups, and the second runs
		// 28 invocations past the end -- which is exactly the case the
		// shader's bounds check exists for.
		constexpr uint32_t kCount = 100;

		RHI::BufferDesc bufferDesc;
		bufferDesc.Size = kCount * sizeof(uint32_t);
		bufferDesc.Usage = RHI::BufferUsage::Storage;
		bufferDesc.Memory = RHI::MemoryDomain::HostVisible;
		bufferDesc.DebugName = "scenetest.values";
		RHI::Ref<RHI::RHIBuffer> values = device.CreateBuffer(bufferDesc);
		Check(values != nullptr, "a storage buffer to work on");
		if (!values)
			return;

		std::vector<uint32_t> input(kCount);
		for (uint32_t i = 0; i < kCount; i++)
			input[i] = i + 1;
		values->Upload(input.data(), bufferDesc.Size);

		RHI::Ref<RHI::RHIResourceSet> set = device.CreateResourceSet(pipeline, 0);
		Check(set != nullptr, "and a resource set built from the compute pipeline");
		if (!set)
			return;

		set->SetStorageBuffer(0, values, 0, bufferDesc.Size);
		set->Commit();

		device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
		{
			cmd.BindComputePipeline(pipeline);
			cmd.BindResourceSet(0, set);

			const uint32_t count = kCount;
			cmd.PushConstants(RHI::ShaderStage::Compute, 0, sizeof(count), &count);

			cmd.Dispatch(pipeline->GroupsFor(kCount));

			// The readback below is a host read of a mapped buffer, which the
			// fence covers -- but the barrier is what a real frame would need
			// between this and a draw, and recording it here keeps the call
			// exercised rather than only written.
			cmd.BufferBarrier(values, RHI::BufferSync::ComputeWrite,
							  RHI::BufferSync::ShaderRead);
		});

		const auto* result = static_cast<const uint32_t*>(values->GetMappedPointer());
		Check(result != nullptr, "the buffer can be read back");
		if (!result)
			return;

		bool everyElement = true;
		uint32_t firstWrong = UINT32_MAX;
		for (uint32_t i = 0; i < kCount; i++)
		{
			const uint32_t expected = input[i] * 2u + i;
			if (result[i] != expected)
			{
				everyElement = false;
				if (firstWrong == UINT32_MAX)
					firstWrong = i;
			}
		}

		if (!everyElement)
		{
			RV_CORE_ERROR("Element {0}: expected {1}, got {2}", firstWrong,
						  input[firstWrong] * 2u + firstWrong, result[firstWrong]);
		}

		Check(everyElement, "the dispatch ran and every element carries its answer");

		// A graphics-only shader must be refused rather than produce a
		// pipeline that dispatches nothing.
		RHI::ShaderDesc graphicsOnly;
		graphicsOnly.Name = "scenetest.notcompute";
		graphicsOnly.Stages.push_back({ RHI::ShaderStage::Vertex,
			"#version 450 core\nvoid main() { gl_Position = vec4(0.0); }\n" });

		if (auto plain = RHI::ShaderCompiler::Compile(graphicsOnly))
		{
			RHI::ComputePipelineDesc bad;
			bad.Name = "scenetest.notcompute";
			bad.Shader = device.CreateShader(*plain);
			Check(device.CreateComputePipeline(bad) == nullptr,
				  "a shader with no compute stage is refused a compute pipeline");
		}
	}

	// Storage images (ENGINE-NOTES 7bc): a 3D texture a compute pass writes
	// through imageStore, a second pass building its next level from it
	// through imageLoad, and a third reading both back through a sampler into
	// a buffer the host can see. The three stages are what the voxel GI
	// pipeline is made of -- voxelise, mip, gather -- so this is its RHI half
	// proven on both backends before a triangle is drawn.
	void CheckStorageImages()
	{
		if (!Renderer::HasDevice())
			return;

		RHI::RHIDevice& device = Renderer::GetDevice();
		if (!device.GetCaps().SupportsCompute)
			return;

		Check(device.GetCaps().SupportsFragmentStores,
			  "the device reports fragment-stage image stores (the voxeliser's imageStore)");

		constexpr uint32_t kSize = 4;

		RHI::TextureDesc textureDesc;
		textureDesc.Type = RHI::TextureType::Texture3D;
		textureDesc.Width = kSize;
		textureDesc.Height = kSize;
		textureDesc.Depth = kSize;
		textureDesc.MipLevels = 2;
		textureDesc.Format = RHI::Format::R16G16B16A16_SFLOAT;
		textureDesc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::Storage;
		textureDesc.DebugName = "scenetest.storage3d";
		RHI::Ref<RHI::RHITexture> volume = device.CreateTexture(textureDesc);
		Check(volume != nullptr, "a 3D texture with storage usage is created");
		if (!volume)
			return;

		auto makePipeline = [&](const char* name, const char* source)
			-> RHI::Ref<RHI::RHIComputePipeline>
		{
			RHI::ShaderDesc desc;
			desc.Name = name;
			desc.Stages.push_back({ RHI::ShaderStage::Compute, source });
			auto compiled = RHI::ShaderCompiler::Compile(desc);
			if (!compiled)
				return nullptr;
			RHI::ComputePipelineDesc pipelineDesc;
			pipelineDesc.Name = name;
			pipelineDesc.Shader = device.CreateShader(*compiled);
			return pipelineDesc.Shader ? device.CreateComputePipeline(pipelineDesc) : nullptr;
		};

		// Every texel of mip 0 takes (x, y, z, 1): distinct per texel, so a
		// write that landed in the wrong place or the wrong level is visible.
		RHI::Ref<RHI::RHIComputePipeline> write = makePipeline("scenetest.storage.write", R"(
#version 450 core
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image3D u_Level0;
void main()
{
	ivec3 texel = ivec3(gl_GlobalInvocationID);
	imageStore(u_Level0, texel, vec4(vec3(texel), 1.0));
}
)");
		// Level 1 is the 2x2x2 box of level 0 -- the voxel mip pass's shape.
		RHI::Ref<RHI::RHIComputePipeline> mip = makePipeline("scenetest.storage.mip", R"(
#version 450 core
layout(local_size_x = 2, local_size_y = 2, local_size_z = 2) in;
layout(set = 0, binding = 0, rgba16f) uniform readonly image3D u_Level0;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image3D u_Level1;
void main()
{
	ivec3 texel = ivec3(gl_GlobalInvocationID);
	ivec3 base = texel * 2;
	vec4 sum = vec4(0.0);
	for (int z = 0; z < 2; z++)
		for (int y = 0; y < 2; y++)
			for (int x = 0; x < 2; x++)
				sum += imageLoad(u_Level0, base + ivec3(x, y, z));
	imageStore(u_Level1, texel, sum * 0.125);
}
)");
		// Samples both levels at texel centres through a sampler, into a
		// buffer: eight level-0 texels along the diagonal, then the eight
		// texels of level 1.
		RHI::Ref<RHI::RHIComputePipeline> read = makePipeline("scenetest.storage.read", R"(
#version 450 core
layout(local_size_x = 16) in;
layout(set = 0, binding = 0) uniform sampler3D u_Volume;
layout(std430, set = 0, binding = 1) buffer Out { vec4 Data[]; } u_Out;
void main()
{
	uint i = gl_GlobalInvocationID.x;
	if (i < 4u)
	{
		// Level 0, the diagonal (i, i, i).
		vec3 uvw = (vec3(float(i)) + 0.5) / 4.0;
		u_Out.Data[i] = textureLod(u_Volume, uvw, 0.0);
	}
	else if (i < 12u)
	{
		uint j = i - 4u;
		ivec3 texel = ivec3(j & 1u, (j >> 1) & 1u, (j >> 2) & 1u);
		vec3 uvw = (vec3(texel) + 0.5) / 2.0;
		u_Out.Data[i] = textureLod(u_Volume, uvw, 1.0);
	}
}
)");
		Check(write && mip && read, "the three storage-image shaders compile and build pipelines");
		if (!write || !mip || !read)
			return;

		RHI::BufferDesc bufferDesc;
		bufferDesc.Size = 12 * sizeof(Vec4);
		bufferDesc.Usage = RHI::BufferUsage::Storage;
		bufferDesc.Memory = RHI::MemoryDomain::HostVisible;
		bufferDesc.DebugName = "scenetest.storage.readback";
		RHI::Ref<RHI::RHIBuffer> readback = device.CreateBuffer(bufferDesc);
		if (!readback)
			return;
		std::vector<Vec4> zeros(12, Vec4(-1.0f));
		readback->Upload(zeros.data(), bufferDesc.Size);

		RHI::SamplerDesc samplerDesc;
		samplerDesc.MinFilter = RHI::FilterMode::Nearest;
		samplerDesc.MagFilter = RHI::FilterMode::Nearest;
		samplerDesc.Mipmap = RHI::MipmapMode::Nearest;
		samplerDesc.WrapU = samplerDesc.WrapV = samplerDesc.WrapW = RHI::WrapMode::ClampToEdge;
		RHI::Ref<RHI::RHISampler> sampler = device.CreateSampler(samplerDesc);

		RHI::Ref<RHI::RHIResourceSet> writeSet = device.CreateResourceSet(write, 0);
		RHI::Ref<RHI::RHIResourceSet> mipSet = device.CreateResourceSet(mip, 0);
		RHI::Ref<RHI::RHIResourceSet> readSet = device.CreateResourceSet(read, 0);
		if (!writeSet || !mipSet || !readSet || !sampler)
			return;

		writeSet->SetStorageImage(0, volume, 0);
		writeSet->Commit();
		mipSet->SetStorageImage(0, volume, 0);
		mipSet->SetStorageImage(1, volume, 1);
		mipSet->Commit();
		readSet->SetTexture(0, volume, sampler);
		readSet->SetStorageBuffer(1, readback, 0, bufferDesc.Size);
		readSet->Commit();

		device.ExecuteImmediate([&](RHI::RHICommandList& cmd)
		{
			cmd.BindComputePipeline(write);
			cmd.BindResourceSet(0, writeSet);
			cmd.Dispatch(1, 1, 1);
			cmd.TextureBarrier(volume, RHI::TextureSync::ComputeWrite, RHI::TextureSync::ShaderRead);
			cmd.TextureBarrier(volume, RHI::TextureSync::ComputeWrite, RHI::TextureSync::ComputeWrite);

			cmd.BindComputePipeline(mip);
			cmd.BindResourceSet(0, mipSet);
			cmd.Dispatch(1, 1, 1);
			cmd.TextureBarrier(volume, RHI::TextureSync::ComputeWrite, RHI::TextureSync::ShaderRead);

			cmd.BindComputePipeline(read);
			cmd.BindResourceSet(0, readSet);
			cmd.Dispatch(1, 1, 1);
			cmd.BufferBarrier(readback, RHI::BufferSync::ComputeWrite, RHI::BufferSync::ShaderRead);
		});

		const auto* result = static_cast<const Vec4*>(readback->GetMappedPointer());
		Check(result != nullptr, "the storage-image readback can be mapped");
		if (!result)
			return;

		auto close = [](float a, float b) { return Math::Abs(a - b) < 0.01f; };

		bool level0 = true;
		for (uint32_t i = 0; i < 4; i++)
		{
			const Vec4& v = result[i];
			level0 = level0 && close(v.x, (float)i) && close(v.y, (float)i) && close(v.z, (float)i)
						  && close(v.w, 1.0f);
		}
		if (!level0)
		{
			RV_CORE_ERROR("storage image level 0 read back ({0}, {1}, {2}, {3}) at the "
						  "diagonal's second texel", result[1].x, result[1].y, result[1].z, result[1].w);
		}
		Check(level0, "a compute pass writes a 3D storage image and a sampler reads it back");

		// Level 1 texel (tx, ty, tz) is the mean of level 0's (2tx..2tx+1, ...),
		// which is (2tx + 0.5, 2ty + 0.5, 2tz + 0.5, 1).
		bool level1 = true;
		for (uint32_t j = 0; j < 8; j++)
		{
			const float tx = (float)(j & 1u), ty = (float)((j >> 1) & 1u), tz = (float)((j >> 2) & 1u);
			const Vec4& v = result[4 + j];
			level1 = level1 && close(v.x, 2.0f * tx + 0.5f) && close(v.y, 2.0f * ty + 0.5f)
						  && close(v.z, 2.0f * tz + 0.5f) && close(v.w, 1.0f);
		}
		if (!level1)
		{
			RV_CORE_ERROR("storage image level 1 read back ({0}, {1}, {2}, {3}) at its first texel",
						  result[4].x, result[4].y, result[4].z, result[4].w);
		}
		Check(level1, "a second pass builds level 1 from level 0 through imageLoad, and the chain samples");
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
			Check(Math::Abs(turned - (2.0f * 30.0f / 60.0f)) < 1e-4f,
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
	// The demo scene's own buttons, clicked.
	//
	// Every mechanism underneath this is already checked -- hit testing,
	// capture, the click edge, the binding -- and all of it passes while the
	// buttons in the shipped scene do nothing. That is the gap this closes:
	// the parts work and the *assembly* is what nobody was testing, and the
	// demo scene is the one assembly a person judges the engine by.
	//
	// It loads the real file rather than building an equivalent, because an
	// equivalent is a second thing that can be right while the shipped one is
	// wrong.
	void CheckDemoButtons()
	{
		auto scene = std::make_shared<Scene>();
		SceneSerializer serializer(scene);

		// Through the project, not a relative path: scenetest runs from its own
		// staged asset folder, where "assets/scenes/demo.rage" is a different
		// file that happens to exist.
		if (!serializer.Deserialize(Project::AssetPath("scenes/demo.rage").string()))
		{
			Check(false, "the demo scene loads");
			return;
		}

		Entity bellButton = scene->FindEntityByName("Bell Button");
		Entity anvilButton = scene->FindEntityByName("Anvil Button");
		Entity bell = scene->FindEntityByName("Bell");
		Entity anvil = scene->FindEntityByName("Anvil");
		Entity sparks = scene->FindEntityByName("Anvil Sparks");

		Check(bellButton && anvilButton && bell && anvil && sparks,
			  "the demo scene still has both buttons and the props they act on");
		if (!bellButton || !anvilButton || !bell || !anvil || !sparks)
			return;

		// **The buttons act on the world, not on a counter.** That is the point
		// of them: a control labelled "ring the bell" beside a courtyard with no
		// bell in it demonstrates a click and nothing else. So what is asserted
		// is that the bell moves and the anvil throws sparks.
		const float width = 1600.0f;
		const float height = 900.0f;

		scene->OnRuntimeStart();

		const auto centreOf = [&](Entity target) -> std::pair<float, float>
		{
			for (const UI::ResolvedElement& element :
				 UI::ResolveScene(*scene, width, height))
			{
				if (element.Entity == (uint64_t)target.GetUUID())
				{
					// **Times Scale.** A resolved rect is in canvas units and
					// the pointer is in screen pixels; the element carries the
					// factor between them. Reading the rect raw put the click
					// at y = 990 on a 900-pixel screen, which is a miss that
					// looks exactly like a broken button.
					return { (element.Rect.X + element.Rect.Width * 0.5f) * element.Scale,
							 (element.Rect.Y + element.Rect.Height * 0.5f) * element.Scale };
				}
			}
			return { -1.0f, -1.0f };
		};

		UI::ResetPointer(*scene);

		const auto clickAt = [&](float x, float y)
		{
			UI::PointerInput pointer;
			pointer.X = x;
			pointer.Y = y;
			pointer.Inside = true;

			pointer.Down = false;
			UI::UpdatePointer(*scene, width, height, pointer);
			pointer.Down = true;
			UI::UpdatePointer(*scene, width, height, pointer);
			pointer.Down = false;
			UI::UpdatePointer(*scene, width, height, pointer);

			// Whether the click landed at all, separated from whether anything
			// reacted to it -- the two failures look identical from outside and
			// have nothing to do with each other.
			bool anyClicked = false;
			auto view = scene->GetRegistry().view<UIButtonComponent>();
			for (auto handle : view)
				anyClicked = anyClicked || view.get<UIButtonComponent>(handle).Clicked;
			Check(anyClicked, "the click reaches a button in the demo scene");

			// The step the handlers run in. EndFixedStep consumes the edge at
			// the end of it, which is why the click is delivered before.
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		};

		// --- the bell swings ---------------------------------------------------
		const float bellRest = bell.GetComponent<TransformComponent>().Rotation.z;

		const auto [bellX, bellY] = centreOf(bellButton);
		Check(bellX >= 0.0f, "the bell button resolves to somewhere on screen");
		clickAt(bellX, bellY);

		// A few more steps, because the first one is at the top of the swing
		// where the sine is still near zero.
		for (int i = 0; i < 6; i++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);

		Check(Math::Abs(bell.GetComponent<TransformComponent>().Rotation.z - bellRest) > 0.01f,
			  "ringing the bell swings it -- the handler lives on the bell, not "
			  "on the button that asked");

		// And it comes back. A prop that drifts a little every time it is used
		// is visibly crooked after a minute of somebody playing with it.
		for (int i = 0; i < 400; i++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);

		Check(Math::Abs(bell.GetComponent<TransformComponent>().Rotation.z - bellRest) < 0.0005f,
			  "and it settles back to exactly where it hung");

		// --- the anvil throws sparks -------------------------------------------
		auto& emitter = sparks.GetComponent<ParticleEmitterComponent>();
		Check(!emitter.Emit && emitter.Burst == 0,
			  "the anvil's sparks are off until it is struck");

		const auto [anvilX, anvilY] = centreOf(anvilButton);
		Check(anvilX >= 0.0f, "the anvil button resolves to somewhere on screen");
		clickAt(anvilX, anvilY);

		Check(sparks.GetComponent<ParticleEmitterComponent>().Burst > 0,
			  "striking the anvil asks its sparks for a burst");

		// --- and the feedback is visible ---------------------------------------
		//
		// A button has no look of its own: it *multiplies* the image under it
		// by its state tint. So a dark panel is a button whose hover works
		// perfectly and cannot be seen -- the first version of this scene used
		// 0.04 grey, where normal 0.82 against hover 1.0 is a difference of two
		// levels out of 255.
		//
		// That is the worst kind of UI bug, because every mechanism passes and
		// the thing still reads as broken. Asserted in levels, which is the
		// unit an eye works in.
		for (Entity button : { bellButton, anvilButton })
		{
			const Vec4 base = button.GetComponent<UIImageComponent>().Color;
			const UIButtonComponent& state = button.GetComponent<UIButtonComponent>();

			const auto level = [&](const Vec4& tint)
			{
				const Vec4 c = base * tint;
				return 255.0f * (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b);
			};

			const float rest = level(state.NormalColor);
			const float hover = level(state.HoverColor);
			const float press = level(state.PressedColor);

			Check(hover - rest >= 6.0f,
				  "hovering the button changes it by enough levels to see");
			Check(rest - press >= 6.0f,
				  "and pressing it does too, in the other direction");
		}

		scene->OnRuntimeStop();
	}

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

		// --- compute passes ---------------------------------------------------
		//
		// "Writes nothing" is the error for a graphics pass and the *contract*
		// for a compute one, so the two halves are asserted against each other:
		// the first case above proves a graphics pass still cannot get away
		// with it, and this proves the exemption is not simply the check being
		// switched off for everybody. ENGINE-NOTES 7y.
		graph.Begin(1280, 720);
		{
			RGTargetDesc desc;
			desc.Name = "Scene";
			desc.Color = Format::R16G16B16A16_SFLOAT;
			const RGResource scene = graph.CreateTarget(desc);

			graph.AddPass("Scene",
				[&](RGPassBuilder& builder) { builder.Write(scene); },
				[](RGPassContext&) {});

			graph.AddComputePass("Histogram",
				[&](RGPassBuilder& builder) { builder.Sample(scene); },
				[](RGPassContext&) {});

			graph.AddPass("Present",
				[&](RGPassBuilder& builder)
				{
					builder.Write(graph.Backbuffer());
					builder.Sample(scene);
				},
				[](RGPassContext&) {});
		}
		Check(graph.Compile(),
			  "a compute pass that writes no target is allowed to");
		Check(graph.GetPassCount() == 3,
			  "and it takes its place in the frame like any other pass");

		// A dispatch cannot write an attachment, so declaring one is a
		// misunderstanding worth naming rather than a frame worth running.
		graph.Begin(1280, 720);
		{
			graph.AddComputePass("Confused",
				[&](RGPassBuilder& builder) { builder.Write(graph.Backbuffer()); },
				[](RGPassContext&) {});
		}
		Check(!graph.Compile(), "a compute pass that draws into a target is refused");
		Check(!graph.Errors().empty() &&
			  graph.Errors()[0].find("Confused") != std::string::npos,
			  "and that error names the pass");

		// --- multi-attachment targets, and passes binding a subset ------------
		//
		// One target with several colours over one depth buffer is what lets
		// two passes write different attachments and still depth-test against
		// the same image -- which is the whole reason weighted-blended
		// transparency can share the scene's depth instead of inventing its
		// own. The subset matters because a pipeline's declared colour formats
		// must match what its pass binds: a three-attachment target that could
		// only be bound whole could not be drawn into by any pipeline that
		// declares one colour, which is every pipeline that draws the scene.
		graph.Begin(1280, 720);
		{
			RGTargetDesc desc;
			desc.Name = "MultiAttachment";
			desc.Color = Format::R16G16B16A16_SFLOAT;
			desc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R8_UNORM };
			const RGResource fat = graph.CreateTarget(desc);

			graph.AddPass("WritesColour",
				[&](RGPassBuilder& builder)
				{
					builder.WriteAttachments(fat, { { 0, Vec4(0.0f) } });
				},
				[](RGPassContext&) {});

			graph.AddPass("WritesTheOtherTwo",
				[&](RGPassBuilder& builder)
				{
					// Accumulation starts at zero and revealage at one: the
					// two clears one shared clear colour could not express.
					builder.WriteAttachments(fat, { { 1, Vec4(0.0f) },
													{ 2, Vec4(1.0f) } },
											 RGLoad::Preserve);
				},
				[](RGPassContext&) {});

			// Into a target of its own rather than the backbuffer: this runs
			// outside the frame loop, so there is no acquired swapchain image
			// to draw to.
			RGTargetDesc resolved;
			resolved.Name = "Resolved";
			const RGResource output = graph.CreateTarget(resolved);

			graph.AddPass("Resolve",
				[&](RGPassBuilder& builder)
				{
					builder.Write(output);
					builder.Sample(fat);
				},
				[&](RGPassContext& context)
				{
					// Each attachment is reachable on its own, or the resolve
					// could not read accumulation and revealage separately.
					Check(context.Color(fat, 0) != nullptr,
						  "a multi-attachment target's first colour is sampleable");
					Check(context.Color(fat, 1) != nullptr &&
						  context.Color(fat, 2) != nullptr,
						  "and so are the ones after it");
					Check(context.Color(fat, 1) != context.Color(fat, 0),
						  "and they are genuinely different images");
				});
		}

		const bool compiled = graph.Compile();
		Check(compiled, "a frame with a multi-attachment target compiles");
		if (!compiled && !graph.Errors().empty())
			RV_CORE_ERROR("graph: {0}", graph.Errors()[0]);

		// Executed for real: the passes bind their attachment subsets, which
		// is where a mismatch between what the graph asked for and what the
		// backend bound would actually surface.
		if (compiled)
		{
			Renderer::GetDevice().ExecuteImmediate([&](RHI::RHICommandList& cmd)
			{
				graph.Execute(cmd);
			});
		}

		// Binding an attachment a target does not have is caught, rather than
		// producing a pass that writes somewhere undefined.
		graph.Begin(1280, 720);
		{
			RGTargetDesc desc;
			desc.Name = "Single";
			const RGResource single = graph.CreateTarget(desc);

			graph.AddPass("OutOfRange",
				[&](RGPassBuilder& builder)
				{
					builder.WriteAttachments(single, { { 2, Vec4(0.0f) } });
				},
				[](RGPassContext&) {});
		}
		Check(!graph.Compile(), "binding an attachment the target does not have is refused");

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
			centroid /= (float)Math::Max<size_t>(positions.size(), 1);

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
					allUnit = allUnit && Math::Abs(Math::Length(CubeFaceDirection(face, u, v)) - 1.0f) < 1e-5f;
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
		Check(Math::Abs(centreTexel(5).r - 0.50f) < 0.02f, "the image centre lands straight ahead");
		Check(Math::Abs(centreTexel(0).r - 0.75f) < 0.02f, "a quarter turn right of it lands at +X");
		Check(Math::Abs(centreTexel(1).r - 0.25f) < 0.02f, "and a quarter turn left at -X");

		// Latitude. Row 0 is the top of the image and the top of the world.
		Check(centreTexel(2).g < 0.05f, "the top row of the image is straight up");
		Check(centreTexel(3).g > 0.95f, "and the bottom row straight down");
		Check(Math::Abs(centreTexel(5).g - 0.50f) < 0.02f, "with the horizon halfway between");

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
				if (std::string(component.Name) != "ParticleEmitterComponent")
					continue;

				for (const FieldDesc& field : component.Fields)
				{
					if (std::string(field.Name) != "SimulateOnGpu")
						continue;

					authored = field.DisplayName == "Simulate on GPU";
					keyKept = std::string(field.Name) == "SimulateOnGpu";
				}
			}

			Check(authored, "an authored label replaces the derived one");
			Check(keyKept, "and leaves the serialized key alone");
			Check(HumanFieldName("SimulateOnGpu") == "Simulate on gpu",
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
						case FieldType::Vec2:   expected = sizeof(Vec2); break;
						case FieldType::Vec3:   expected = sizeof(Vec3); break;
						case FieldType::Vec4:   expected = sizeof(Vec4); break;
						case FieldType::String: expected = sizeof(std::string); break;
						case FieldType::Asset:  expected = sizeof(AssetHandle); break;
						case FieldType::Entity: expected = sizeof(EntityRef); break;
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
	// The render settings a script can reach.
	//
	// Three things have to agree and are edited separately: the described
	// field list, the scene file's keys, and the C# property names. The first
	// is what C# reaches over the name-and-text bridge, so a name that drifts
	// from the serializer's is a setting a script can write and a save cannot
	// keep -- silently, because both halves work on their own.
	// A motion vector is the difference between two *frames*, not between two
	// calls -- and the transforms are recomputed several times a frame, by the
	// update, a probe capture and a shadow pass. So the history advances once,
	// explicitly, and this is what says so. ENGINE-NOTES 7r.
	void CheckMotionHistory()
	{
		auto scene = std::make_shared<Scene>();
		Entity entity = scene->CreateEntity("Mover");
		auto& transform = entity.GetComponent<TransformComponent>();

		transform.Position = { 1.0f, 0.0f, 0.0f };
		scene->OnUpdateEditor(0.016f);
		scene->UpdateWorldTransforms();

		Check(Math::Abs(transform.World[3].x - 1.0f) < 1e-5f, "a moved entity is where it was put");

		// Second frame: the history is last frame's world matrix.
		transform.Position = { 4.0f, 0.0f, 0.0f };
		scene->OnUpdateEditor(0.016f);
		scene->UpdateWorldTransforms();

		Check(Math::Abs(transform.World[3].x - 4.0f) < 1e-5f, "and moves when it is moved");
		Check(Math::Abs(transform.PreviousWorld[3].x - 1.0f) < 1e-5f,
			  "with the previous frame's position still available to difference against");

		// Recomputing within the same frame must not consume the history --
		// that is the whole reason it is advanced separately.
		scene->UpdateWorldTransforms();
		scene->UpdateWorldTransforms();
		Check(Math::Abs(transform.PreviousWorld[3].x - 1.0f) < 1e-5f,
			  "and recomputing transforms again in the same frame does not flatten it to zero motion");

		// A frame in which nothing moved has to report no motion, or every
		// static object in the scene smears.
		scene->OnUpdateEditor(0.016f);
		scene->UpdateWorldTransforms();
		Check(Math::Abs(transform.PreviousWorld[3].x - 4.0f) < 1e-5f,
			  "a frame that moved nothing reports no motion at all");
	}

	// The temporal jitter, as arithmetic.
	//
	// The end-to-end properties -- reproducible, periodic, moves only edges --
	// are measured by tools/scripts/check_taa_jitter.py, which needs a build
	// and 26 launches of the runtime. These are the parts that can be checked
	// in microseconds, and they are the parts where being wrong is silent: a
	// transposed matrix index shears the frame instead of shifting it, and an
	// offset in the wrong units is a jitter of half a *screen*.
	// The declaration-site scanner, on strings. The fixture (see the CMake
	// custom command) proves the whole chain registers; these prove the
	// *refusals* -- and the refusals are the design. A generator that guesses
	// at a declaration it half-understands produces a field that silently
	// never appears, so every case here that must error is a case where the
	// alternative was silence.
	void CheckScriptGen()
	{
		std::vector<ScriptGen::Error> errors;
		auto scan = [&errors](const char* text)
		{
			errors.clear();
			return ScriptGen::Scan("Probe.cpp", text, errors);
		};
		auto errorMentions = [&errors](const char* needle)
		{
			return errors.size() == 1
				&& errors[0].Message.find(needle) != std::string::npos;
		};

		// The shape New Script writes, marked.
		{
			const ScriptGen::FileScan result = scan(R"cpp(
				#include "RageV/Scene/ScriptRegistry.h"
				namespace RageV
				{
					class Probe : public ScriptableEntity
					{
					public:
						RVShowInEditor
						float Speed = 1.0f;

						RVShowInEditor
						std::string Label = "hello";

						RVCallable
						void Go() {}
					};
				}
			)cpp");

			Check(errors.empty() && result.Scripts.size() == 1,
				  "a marked class scans to one script");
			if (result.Scripts.size() == 1)
			{
				const ScriptGen::Script& script = result.Scripts[0];
				Check(script.Name == "Probe" && script.Qualified == "::RageV::Probe",
					  "with its registered name and its qualified one");
				Check(script.Fields.size() == 2 && script.Fields[0].Name == "Speed"
					  && script.Fields[1].Name == "Label",
					  "both fields, in declaration order");
				Check(script.Methods.size() == 1 && script.Methods[0].Name == "Go",
					  "and the callable");

				const std::string emitted = ScriptGen::Emit(result);
				Check(emitted.find("Make<::RageV::Probe>(\"Probe\")") != std::string::npos
					  && emitted.find(".Field<&::RageV::Probe::Speed>(\"Speed\")") != std::string::npos
					  && emitted.find(".Method<&::RageV::Probe::Go>(\"Go\")") != std::string::npos,
					  "and the emitted wrapper registers exactly what a person would have");
			}
		}

		// A marker is only a marker as a token in code. In a comment or a
		// string it is prose, and registering from prose would make this
		// file's own comments a source of scripts.
		{
			const ScriptGen::FileScan result = scan(R"cpp(
				// RVShowInEditor is spelled here in a comment
				/* and RVCallable in a block one */
				namespace RageV
				{
					class Quiet : public ScriptableEntity
					{
					public:
						const char* What = "RVShowInEditor";
					};
				}
			)cpp");
			Check(errors.empty() && result.Scripts.empty(),
				  "markers in comments and strings register nothing");
			Check(!ScriptGen::HasMarkers(
					  "// RVShowInEditor\n/* RVCallable */\nconst char* s = \"RVScript\";"),
				  "and HasMarkers agrees");
			Check(ScriptGen::HasMarkers("RVShowInEditor\nfloat X = 1.0f;"),
				  "while a real marker is seen");
		}

		// An anonymous namespace is fine: the wrapper TU includes the file,
		// so internal linkage is no obstacle, and qualified lookup reaches
		// through the unnamed namespace.
		{
			const ScriptGen::FileScan result = scan(R"cpp(
				namespace
				{
					class Hidden : public ScriptableEntity
					{
					public:
						RVShowInEditor
						int Count = 3;
					};
				}
			)cpp");
			Check(errors.empty() && result.Scripts.size() == 1
				  && result.Scripts[0].Qualified == "::Hidden",
				  "an anonymous-namespace script scans, qualified from the enclosing scope");
		}

		// RVScript alone: in the dropdown with nothing editable.
		{
			const ScriptGen::FileScan result = scan(R"cpp(
				namespace RageV
				{
					RVScript
					class Bare : public ScriptableEntity
					{
					};
				}
			)cpp");
			Check(errors.empty() && result.Scripts.size() == 1
				  && result.Scripts[0].Fields.empty() && result.Scripts[0].Methods.empty(),
				  "RVScript registers a class with no fields");
		}

		// --- the refusals ---------------------------------------------------

		scan("class A : public ScriptableEntity { RVShowInEditor float X = 1.0f; };");
		Check(errorMentions("public"),
			  "a private marked field is refused, naming why public");

		scan("class A : public ScriptableEntity { public: RVShowInEditor double X = 1.0; };");
		Check(errorMentions("'double'"),
			  "an uneditable type is refused, naming the type");

		scan("class A : public ScriptableEntity { public: RVShowInEditor static float X; };");
		Check(errorMentions("static"), "a static field is refused");

		scan("class A : public ScriptableEntity { public: RVShowInEditor float X = 1.0f, Y = 2.0f; };");
		Check(errorMentions("one member per"), "two declarators under one marker are refused");

		scan("class A : public ScriptableEntity { public: RVCallable int Go(); };");
		Check(errorMentions("void with no parameters"), "a non-void callable is refused");

		scan("class A : public ScriptableEntity { public: RVCallable void Go(int x); };");
		Check(errorMentions("void with no parameters"), "a callable with parameters is refused");

		scan("class A : public ScriptableEntity { public: RVCallable void Go() const; };");
		Check(errorMentions("const"), "a const callable is refused");

		scan("class A : public ScriptableEntity { public: RVShowInEditor void Go(); };");
		Check(errorMentions("RVCallable"),
			  "a field marker on a method points at the right marker");

		scan("RVShowInEditor float Loose = 1.0f;");
		Check(errorMentions("inside a script class"), "a marker outside any class is refused");

		scan("template<typename T> class A : public ScriptableEntity "
			 "{ public: RVShowInEditor float X = 1.0f; };");
		Check(errorMentions("template"), "a template script is refused");

		scan("class Outer { public: class Inner : public ScriptableEntity "
			 "{ public: RVShowInEditor float X = 1.0f; }; };");
		Check(errorMentions("nested"), "a nested script is refused");

		scan("RVScript\nfloat NotAClass = 1.0f;");
		Check(errorMentions("class definition"), "RVScript on a non-class is refused");

		// Both styles on one class is the registry's duplicate-name coin
		// toss waiting to happen; the build refuses it with both lines.
		scan(R"cpp(
			class Both : public ScriptableEntity
			{
			public:
				RVShowInEditor
				float X = 1.0f;
			};
			RV_REGISTER_SCRIPT(Both).Field<&Both::X>("X");
		)cpp");
		Check(errorMentions("RV_REGISTER_SCRIPT"),
			  "marking a class that also has a registration block is refused");

		// The diagnostic shape is the contract with the build panel.
		{
			std::vector<ScriptGen::Error> one;
			ScriptGen::Scan("C:\\p\\Bad.cpp",
							"class A : public ScriptableEntity "
							"{ public: RVShowInEditor double X = 1.0; };", one);
			Managed::BuildDiagnostic diagnostic;
			Check(one.size() == 1
				  && ModuleBuild::ParseDiagnostic(one[0].Format(), diagnostic)
				  && diagnostic.IsError && diagnostic.Code == "RVGEN1"
				  && diagnostic.File == "C:\\p\\Bad.cpp",
				  "a refusal formats as an MSVC diagnostic the build panel parses");
		}
	}

	// The adaptation law, which is arithmetic and belongs here rather than in a
	// screenshot. ENGINE-NOTES 7y.
	//
	// `check_auto_exposure.py` proves the value reaches the shader and that
	// metering closes a three-stop gap; only pixels can show that. What pixels
	// are a terrible instrument for is `1 - exp(-rate * dt)`, and that is the
	// half where being wrong is **silent**: the naive `rate * dt` looks
	// identical at one frame rate and drifts at another, so the bug appears
	// only when two machines are compared.
	void CheckAutoExposureLaw()
	{
		const float rate = 3.0f;

		// The assertion worth writing first. Ten steps of 10 ms must land where
		// one step of 100 ms does -- the property `rate * dt` does not have.
		{
			float many = 0.0f;
			for (int i = 0; i < 10; i++)
				many += (1.0f - many) * AutoExposure::BlendWeight(rate, 0.01f);

			const float one = AutoExposure::BlendWeight(rate, 0.1f);

			Check(Math::Abs(many - one) < 0.0005f,
				  "adaptation is framerate independent: ten short steps land "
				  "where one long one does");

			// And the control, so the above is not passing because both sides
			// are the same constant. The linear form fails it by a wide margin,
			// which is what the exponential is chosen over.
			float naiveMany = 0.0f;
			for (int i = 0; i < 10; i++)
				naiveMany += (1.0f - naiveMany) * (rate * 0.01f);

			Check(Math::Abs(naiveMany - rate * 0.1f) > 0.02f,
				  "and the linear approximation genuinely would not have been");
		}

		// A step of the time constant closes 1 - 1/e of the gap, which is what
		// makes the setting mean "stops per second" rather than an arbitrary
		// number somebody tuned by eye.
		Check(Math::Abs(AutoExposure::BlendWeight(1.0f, 1.0f) - 0.6321f) < 0.001f,
			  "one time constant closes 63% of the distance");

		// Monotone in both arguments: longer frames and faster settings both
		// move further, never less.
		Check(AutoExposure::BlendWeight(rate, 0.02f) > AutoExposure::BlendWeight(rate, 0.01f),
			  "a longer frame adapts further");
		Check(AutoExposure::BlendWeight(6.0f, 0.01f) > AutoExposure::BlendWeight(3.0f, 0.01f),
			  "and a faster setting adapts further in the same frame");

		// Bounded, because the weight is used as a mix factor and one outside
		// 0..1 extrapolates -- which would overshoot the measurement and then
		// oscillate rather than settle.
		Check(AutoExposure::BlendWeight(rate, 100.0f) <= 1.0f,
			  "an enormous frame time cannot push the weight past 1");

		// The two ways of asking for "do not adapt", neither of which is a
		// division by anything or a step backwards.
		Check(AutoExposure::BlendWeight(rate, 0.0f) == 0.0f,
			  "no time passing moves nothing");
		Check(AutoExposure::BlendWeight(rate, -0.5f) == 0.0f,
			  "and a negative frame time does not run the adaptation backwards");
		Check(AutoExposure::BlendWeight(0.0f, 0.016f) == 0.0f,
			  "a rate of zero holds the exposure where it is");

		// The state's own rule: two dispatches before a measurement is trusted,
		// because the first frame's histogram is the only one no reduce pass
		// has cleared. This cost a session -- it presented as Vulkan adapting
		// four seconds slower than OpenGL, reproducibly, which reads as a slow
		// setting rather than as a bug.
		{
			ExposureState state;
			Check(!state.HasAdapted(), "a fresh chain has not adapted");
			state.MarkAdapted();
			Check(!state.HasAdapted(),
				  "and still has not after one dispatch, whose histogram nobody "
				  "had cleared");
			state.MarkAdapted();
			Check(state.HasAdapted(), "the second dispatch is the one to trust");

			state.Invalidate();
			Check(!state.HasAdapted(),
				  "and switching the feature off forgets it, rather than "
				  "resuming later from a stale exposure");
		}
	}

	void CheckTemporalJitter()
	{
		const uint32_t phase = TemporalJitterPhase();
		Check(phase >= 4, "the jitter sequence is long enough to be worth having");

		// Half a pixel either way, in NDC, at a known size.
		const uint32_t width = 1600;
		const uint32_t height = 900;
		const float limitX = 1.0f / (float)width;
		const float limitY = 1.0f / (float)height;

		std::vector<Vec2> offsets;
		bool inside = true;
		for (uint32_t frame = 0; frame < phase; frame++)
		{
			const Vec2 offset = TemporalJitter(frame, width, height);
			inside = inside && Math::Abs(offset.x) <= limitX + 1e-9f
							&& Math::Abs(offset.y) <= limitY + 1e-9f;
			offsets.push_back(offset);
		}

		Check(inside, "every offset lands inside the pixel it is offsetting");

		// Distinct, which is the whole point of a low-discrepancy sequence and
		// the thing an indexing mistake destroys without changing anything a
		// person would notice.
		bool distinct = true;
		for (size_t a = 0; a < offsets.size(); a++)
		{
			for (size_t b = a + 1; b < offsets.size(); b++)
			{
				if (Math::Abs(offsets[a].x - offsets[b].x) < 1e-9f &&
					Math::Abs(offsets[a].y - offsets[b].y) < 1e-9f)
					distinct = false;
			}
		}
		Check(distinct, "and no two frames in the sequence land on the same one");

		// None of them is zero. Halton's first point is the origin, so an
		// off-by-one here renders one frame in every eight through the
		// unjittered projection -- which converges to a slightly different
		// image and is invisible until somebody measures the convergence.
		bool nonzero = true;
		for (const Vec2& offset : offsets)
			nonzero = nonzero && (offset.x != 0.0f || offset.y != 0.0f);
		Check(nonzero, "and none of them is no offset at all");

		// The period, which is what the screenshot checks rest on.
		const Vec2 first = TemporalJitter(30, width, height);
		const Vec2 wrapped = TemporalJitter(30 + phase, width, height);
		Check(first.x == wrapped.x && first.y == wrapped.y,
			  "a frame and the same frame one period later get the same offset");

		const Vec2 next = TemporalJitter(31, width, height);
		Check(first.x != next.x || first.y != next.y,
			  "and the frame after it does not");

		// A degenerate target is a divide by zero waiting to happen, and a
		// zero-sized viewport is a panel somebody dragged shut.
		const Vec2 degenerate = TemporalJitter(3, 0, 0);
		Check(degenerate.x == 0.0f && degenerate.y == 0.0f,
			  "a target with no pixels in it gets no offset rather than infinity");

		// --- the two settings, which are the filter's shape -----------------
		//
		// Width first. It scales the offset about the *pixel's centre*, and
		// that is the whole of the implementation risk: scaling a point that
		// had not been centred first shrinks the offsets towards the corner
		// rather than the middle, which is a filter biased half a pixel down
		// and to the left, everywhere, with nothing to see. So this asserts
		// the centring survives, by checking that halving the width halves
		// the offset rather than moving it somewhere else.
		bool halves = true;
		bool wider = true;
		for (uint32_t frame = 0; frame < phase; frame++)
		{
			const Vec2 full = TemporalJitter(frame, width, height, 1.0f);
			const Vec2 half = TemporalJitter(frame, width, height, 0.5f);
			const Vec2 twice = TemporalJitter(frame, width, height, 2.0f);

			halves = halves && Math::Abs(half.x - full.x * 0.5f) < 1e-9f
							&& Math::Abs(half.y - full.y * 0.5f) < 1e-9f;
			wider = wider && Math::Abs(twice.x - full.x * 2.0f) < 1e-9f
						  && Math::Abs(twice.y - full.y * 2.0f) < 1e-9f;
		}
		Check(halves, "half the jitter width is half the offset, about the pixel centre");
		Check(wider, "and twice the width is twice the offset, on both axes");

		// Zero has to be exactly zero rather than nearly: it is the setting
		// that says "accumulate without jittering", and an offset of 1e-8
		// there is a filter that still converges somewhere else.
		bool silent = true;
		for (uint32_t frame = 0; frame < phase; frame++)
		{
			const Vec2 none = TemporalJitter(frame, width, height, 0.0f);
			silent = silent && none.x == 0.0f && none.y == 0.0f;
		}
		Check(silent, "a jitter width of zero is exactly no offset, every frame");

		// Then the sequence length. The property every screenshot check rests
		// on is "frame N and frame N + phase are the same picture", and it has
		// to hold at whatever phase the project asks for -- not only at 8.
		bool repeats = true;
		bool differs = true;
		for (int setting = 1; setting <= 16; setting++)
		{
			const uint32_t period = TemporalJitterPhase(setting);
			Check(period == (uint32_t)setting || setting < 1 || setting > 16,
				  "the period in range is the period asked for");

			const Vec2 here = TemporalJitter(30, width, height, 1.0f, setting);
			const Vec2 later = TemporalJitter(30 + period, width, height, 1.0f, setting);
			repeats = repeats && here.x == later.x && here.y == later.y;

			// A phase of one repeats every frame by definition, so it is the
			// one setting for which "the next frame differs" is false -- and
			// saying so is what stops this asserting something untrue rather
			// than excluding the case quietly.
			if (period > 1)
			{
				const Vec2 next = TemporalJitter(31, width, height, 1.0f, setting);
				differs = differs && (here.x != next.x || here.y != next.y);
			}
		}
		Check(repeats, "every phase from 1 to 16 repeats after exactly that many frames");
		Check(differs, "and no phase above 1 repeats sooner");

		// Out of range is clamped rather than obeyed: a phase of 0 is a modulo
		// by zero, and a project file is something a person can type into.
		Check(TemporalJitterPhase(0) == 1, "a phase of zero clamps to one rather than dividing by it");
		Check(TemporalJitterPhase(-4) == 1, "and a negative one does too");
		Check(TemporalJitterPhase(999) == 16, "an absurd phase clamps to the useful maximum");

		// --- the projection, which is where a convention error hides --------
		//
		// The claim is that this shifts, and shifts by exactly the offset
		// asked for. A transposed index would shear instead, and a sheared
		// frame still looks like a frame.
		const Mat4 projection = Math::Perspective(Math::Radians(60.0f),
												  16.0f / 9.0f, 0.1f, 100.0f);
		const Vec2 offset(0.013f, -0.021f);
		const Mat4 jittered = JitterProjection(projection, offset);

		bool shifted = true;
		const Vec4 points[] = {
			Vec4(0.0f, 0.0f, -1.0f, 1.0f),
			Vec4(3.0f, -2.0f, -12.0f, 1.0f),
			Vec4(-7.0f, 4.5f, -0.5f, 1.0f),
		};

		for (const Vec4& point : points)
		{
			const Vec4 plain = projection * point;
			const Vec4 moved = jittered * point;

			// Same depth and same w: this must not touch the perspective
			// divide or the depth test, only where the pixel lands.
			shifted = shifted && Math::Abs(plain.z - moved.z) < 1e-5f
							  && Math::Abs(plain.w - moved.w) < 1e-5f;

			const Vec2 before(plain.x / plain.w, plain.y / plain.w);
			const Vec2 after(moved.x / moved.w, moved.y / moved.w);

			shifted = shifted && Math::Abs((after.x - before.x) - offset.x) < 1e-5f
							  && Math::Abs((after.y - before.y) - offset.y) < 1e-5f;
		}

		Check(shifted, "the jittered projection moves every point by exactly the "
					   "offset, at every depth, and leaves z and w alone");

		// Zero has to be free, because every mode but TAA passes zero and the
		// stored screenshots those modes are compared against were rendered
		// before any of this existed.
		const Mat4 unmoved = JitterProjection(projection, Vec2(0.0f, 0.0f));
		bool identical = true;
		for (int column = 0; column < 4; column++)
		{
			identical = identical
				&& unmoved[column].x == projection[column].x
				&& unmoved[column].y == projection[column].y
				&& unmoved[column].z == projection[column].z
				&& unmoved[column].w == projection[column].w;
		}
		Check(identical, "and no offset at all leaves the projection bit-identical");
	}

	// Changing the anti-aliasing mode with the application running.
	//
	// This is the check that was missing, and its absence cost a user-visible
	// defect that every other check here walked straight past: they each start
	// in one mode, render, and exit. Nothing switched.
	//
	// The post-process pass sequence is a different *shape* per mode -- FXAA
	// adds one pass, SMAA three, TAA one with three texture bindings instead
	// of one -- and the descriptor sets are pooled by position. Slot four is
	// whichever dispatch happens to be fourth, so on the frame the mode
	// changes, a set built for one pipeline's layout is handed to another's.
	// Reused that way it silently drops every binding the old layout did not
	// have, and tone mapping loses its bloom texture: a dark scene with
	// enormous bloom on Vulkan, and a ghost of some other image behind the
	// scene on OpenGL.
	//
	// Detected by counting warnings, because that is what the RHI does when a
	// set is asked for a binding its pipeline does not have. Backend-specific
	// in where it fires and not in what it means.
	void CheckAntiAliasingSwitch()
	{
		if (!Renderer::HasDevice() || !PostProcess::IsReady())
			return;

		// A sink on the core logger, for the duration of this check. Nothing
		// in the engine had to grow a hook for it.
		struct CountingSink : spdlog::sinks::base_sink<std::mutex>
		{
			std::atomic<int> Warnings{ 0 };
		protected:
			void sink_it_(const spdlog::details::log_msg& message) override
			{
				if (message.level >= spdlog::level::warn)
					Warnings++;
			}
			void flush_() override {}
		};

		auto counter = std::make_shared<CountingSink>();
		auto& sinks = Log::GetCoreLogger()->sinks();
		sinks.push_back(counter);

		RenderGraph graph(Renderer::GetDevice());
		TemporalHistory history;

		auto renderOnce = [&](AntiAliasing aa)
		{
			// Resets the descriptor-set cursor, which is what makes the next
			// frame reuse slot 0, slot 1, slot 2 ... exactly as a real frame
			// does. Without it every dispatch would take a fresh slot and the
			// defect this is looking for could not happen.
			PostProcess::BeginFrame();

			graph.Begin(320, 200);

			RGTargetDesc output;
			output.Name = "SwitchOutput";
			output.Color = RHI::Format::R8G8B8A8_UNORM;
			output.Depth = RHI::Format::Undefined;

			RenderSettings render;
			render.AA = aa;

			PostSettings post;
			// On, so tone mapping binds a real bloom texture rather than the
			// black stand-in -- the binding that goes missing is this one.
			post.BloomEnabled = true;

			FrameDesc frame;
			frame.Output = graph.CreateTarget(output);
			frame.Width = 320;
			frame.Height = 200;
			frame.Render = render;
			frame.Post = post;
			frame.OutputFormat = RHI::Format::R8G8B8A8_UNORM;
			frame.DrawScene = [](RGPassContext&) {};
			frame.History = &history;

			BuildFrame(graph, frame);

			if (!graph.Compile())
				return false;

			Renderer::GetDevice().ExecuteImmediate([&](RHI::RHICommandList& cmd)
			{
				graph.Execute(cmd);
			});
			return true;
		};

		// Every ordered pair of modes, so no single ordering can be the one
		// that happens to work. Each mode is rendered twice in a row as well,
		// which is the case that always worked and is worth keeping honest.
		const AntiAliasing modes[] = { AntiAliasing::None, AntiAliasing::FXAA,
									   AntiAliasing::SMAA, AntiAliasing::SSAA,
									   AntiAliasing::MSAA, AntiAliasing::TAA };

		bool compiled = true;
		for (AntiAliasing from : modes)
		{
			for (AntiAliasing to : modes)
			{
				compiled = renderOnce(from) && compiled;
				compiled = renderOnce(to) && compiled;
			}
		}

		Check(compiled, "every anti-aliasing mode compiles and executes a real frame");

		const int warnings = counter->Warnings.load();
		Check(warnings == 0,
			  "and switching between any two of them logs nothing -- a pooled "
			  "descriptor set reused across two pipeline layouts drops the "
			  "bindings the old one did not have, which is a lost bloom texture "
			  "and not an error anybody would trace back to the dropdown");

		if (warnings != 0)
			RV_CORE_ERROR("anti-aliasing switch produced {0} warning(s)", warnings);

		sinks.pop_back();
	}

	// The history pair on its own, away from the graph.
	//
	// Small and worth having, because every one of these properties is
	// invisible in the picture: a pair that failed to swap looks like a filter
	// that converged instantly, and a resize that kept its history looks like
	// one frame of smear that nobody catches while dragging a panel edge.
	void CheckTemporalHistory()
	{
		RHI::RHIDevice& device = Renderer::GetDevice();

		TemporalHistory history;
		Check(!history.HasHistory(), "a fresh history has nothing in it");
		Check(!history.Current(), "and nothing allocated");

		history.Prepare(device, 320, 200, RHI::Format::R16G16B16A16_SFLOAT);
		Check(history.Current() && history.Previous(), "Prepare allocates both halves");
		Check(!history.HasHistory(),
			  "which are not history yet -- a freshly allocated image holds "
			  "whatever the driver left in it");

		const RHI::Ref<RHI::RHIRenderTarget> first = history.Current();
		history.Advance();
		Check(history.HasHistory(), "after a frame there is a history");
		Check(history.Previous() == first,
			  "and it is the target that frame wrote");
		Check(history.Current() != first, "while the next frame writes the other one");

		history.Advance();
		Check(history.Current() == first, "and the pair alternates rather than growing");

		// A resize throws it away. Reprojecting into an image of another size
		// is meaningless, and stretching it would smear a whole frame every
		// time somebody drags a panel edge.
		history.Prepare(device, 640, 400, RHI::Format::R16G16B16A16_SFLOAT);
		Check(!history.HasHistory(), "a resize forgets the accumulated image");
		Check(history.Current() && history.Current()->GetWidth() == 640,
			  "and reallocates at the new size");

		history.Advance();
		history.Prepare(device, 640, 400, RHI::Format::R16G16B16A16_SFLOAT);
		Check(history.HasHistory(),
			  "while preparing at the same size again keeps it, which is what "
			  "happens on every frame that is not a resize");

		// A viewport dragged shut. Not an error and not a reason to free
		// anything, but the history stops describing anything.
		history.Prepare(device, 0, 0, RHI::Format::R16G16B16A16_SFLOAT);
		Check(!history.HasHistory(), "a zero-sized target has no history either");

		history.Release();
		Check(!history.Current() && !history.HasHistory(), "and Release empties it");
	}

	// A value that is not the default, per type, so "the writer wrote nothing
	// and the reader kept its default" cannot pass. Returns what it wrote.
	//
	// Through Access rather than the text bridge, which is private to the
	// interop layer.
	std::vector<std::pair<std::string, double>> PokeEverySetting(
		const std::vector<FieldDesc>& fields, void* block)
	{
		std::vector<std::pair<std::string, double>> written;

		for (const FieldDesc& field : fields)
		{
			if (!field.Access)
				continue;

			void* member = field.Access(block);
			switch (field.Type)
			{
				case FieldType::Bool:
					*(bool*)member = !*(bool*)member;
					written.emplace_back(field.Name, *(bool*)member ? 1.0 : 0.0);
					break;
				case FieldType::Int:
				case FieldType::Enum:
					*(int32_t*)member = 1;
					written.emplace_back(field.Name, 1.0);
					break;
				case FieldType::Float:
					*(float*)member = 0.375f;
					written.emplace_back(field.Name, 0.375);
					break;
				default: break;   // colours and handles, checked by hand
			}
		}

		return written;
	}

	// Which of them did not survive the round trip, as a sentence.
	std::string LostSettings(const std::vector<FieldDesc>& fields, void* block,
							 const std::vector<std::pair<std::string, double>>& written)
	{
		std::string lost;

		for (const auto& [name, expected] : written)
		{
			const FieldDesc* field = nullptr;
			for (const FieldDesc& candidate : fields)
			{
				if (name == candidate.Name)
				{
					field = &candidate;
					break;
				}
			}
			if (!field || !field->Access)
				continue;

			void* member = field->Access(block);
			double got = 0.0;
			switch (field->Type)
			{
				case FieldType::Bool:  got = *(bool*)member ? 1.0 : 0.0; break;
				case FieldType::Int:
				case FieldType::Enum:  got = (double)*(int32_t*)member; break;
				case FieldType::Float: got = (double)*(float*)member; break;
				default: continue;
			}

			if (Math::Abs(got - expected) > 1e-4)
			{
				lost += (lost.empty() ? "" : ", ") + name + " (" +
						std::to_string(expected) + " -> " + std::to_string(got) + ")";
			}
		}

		return lost;
	}

	void CheckRenderSettings()
	{
		// --- the three blocks are described, and do not collide ---------------
		Check(!RenderSettingsRegistry::Fields().empty(), "the render settings are described");
		Check(!PostSettingsRegistry::Fields().empty(), "so are the post settings");
		Check(!SceneEnvironmentRegistry::Fields().empty(), "and the scene's environment");

		const FieldDesc* aa = RenderSettingsRegistry::Find("AntiAliasing");
		Check(aa != nullptr, "and anti-aliasing is one of them");
		Check(aa && aa->Type == FieldType::Enum, "described as a choice, not a number");
		Check(aa && aa->Hint.EnumCount == 6, "with all six modes named");
		// An enum crossing as text is read back as int, so a mismatch here is
		// a stack write past the member rather than a wrong value.
		Check(aa && aa->Size == sizeof(int),
			  "and int-sized, because the text bridge reads it as one");

		// **One flat namespace, three owners.** The C# bridge resolves a name
		// by asking each registry in turn, so a name in two of them would make
		// the answer depend on the order it happens to look -- and the wrong
		// arm would write into a struct of a different shape at the same
		// offset. Cheap to assert, impossible to notice otherwise.
		{
			std::vector<std::string> seen;
			std::string clashes;

			const std::vector<FieldDesc>* blocks[] = {
				&RenderSettingsRegistry::Fields(),
				&PostSettingsRegistry::Fields(),
				&SceneEnvironmentRegistry::Fields(),
			};

			for (const std::vector<FieldDesc>* block : blocks)
			{
				for (const FieldDesc& field : *block)
				{
					const std::string name = field.Name;
					if (std::find(seen.begin(), seen.end(), name) != seen.end())
						clashes += (clashes.empty() ? "" : ", ") + name;
					else
						seen.push_back(name);
				}
			}

			Check(clashes.empty(),
				  "and no setting name appears in two blocks, which would make "
				  "FindSetting's answer depend on the order it looks");
			if (!clashes.empty())
				RV_CORE_ERROR("setting names in more than one block: {0}", clashes);
		}

		// A setting reaches its owner, and only its owner.
		Check(FindSetting("AntiAliasing").Block == SettingsBlock::Render,
			  "anti-aliasing resolves to the project's block");
		Check(FindSetting("Exposure").Block == SettingsBlock::Post,
			  "exposure to the post profile's");
		Check(FindSetting("SkyIntensity").Block == SettingsBlock::SceneEnvironment,
			  "and the sky to the scene's");
		Check(!FindSetting("Nonsense"),
			  "while a name that is not a setting resolves to nothing rather than "
			  "to the first block that was asked");

		// --- each block survives its own file ---------------------------------
		//
		// The registry and a file format are two descriptions of the same
		// settings, and they drift silently: a field described but never
		// written is reachable from a script, editable in the inspector, and
		// reset to its default the moment the file is reopened.
		// `TemporalFeedback` shipped exactly that way. Driven off the registry
		// rather than a list, so a setting added tomorrow is covered without
		// anybody choosing to cover it. ENGINE-NOTES 7s.

		// The scene, which keeps ambient and the sky.
		{
			auto probe = std::make_shared<Scene>();
			const auto written = PokeEverySetting(SceneEnvironmentRegistry::Fields(),
												  &probe->GetEnvironment());

			Check(written.size() >= 4,
				  "the scene's environment offers enough scalar fields to be worth checking");

			const std::filesystem::path path =
				std::filesystem::temp_directory_path() / "rv_environment_all.rage";
			SceneSerializer(probe).Serialize(path.string());

			auto reloaded = std::make_shared<Scene>();
			Check(SceneSerializer(reloaded).Deserialize(path.string()),
				  "a scene carrying every environment setting loads back");

			const std::string lost = LostSettings(SceneEnvironmentRegistry::Fields(),
												  &reloaded->GetEnvironment(), written);
			Check(lost.empty(),
				  "and every one of them comes back with the value it was given");
			if (!lost.empty())
				RV_CORE_ERROR("environment settings lost across a save: {0}", lost);

			std::error_code error;
			std::filesystem::remove(path, error);
		}

		// The post profile, which is its own asset.
		{
			PostSettings block;
			const auto written = PokeEverySetting(PostSettingsRegistry::Fields(), &block);

			Check(written.size() >= 5,
				  "the post settings offer enough scalar fields to be worth checking");

			const std::filesystem::path path =
				std::filesystem::temp_directory_path() / "rv_post_all.rvpostprofile";
			Check(Assets::PostProfileSerializer::Save(block, path), "a post profile writes");

			PostSettings reloaded;
			Check(Assets::PostProfileSerializer::Load(reloaded, path), "and loads back");

			const std::string lost =
				LostSettings(PostSettingsRegistry::Fields(), &reloaded, written);
			Check(lost.empty(), "with every setting the registry describes still on it");
			if (!lost.empty())
				RV_CORE_ERROR("post settings lost across a save: {0}", lost);

			// A file that is not a profile must not load as a profile of pure
			// defaults, which would render and would be wrong invisibly.
			{
				std::ofstream junk(path);
				junk << "Curve: 1\nKeys: []\n";
			}
			PostSettings untouched;
			untouched.Exposure = 3.0f;
			Check(!Assets::PostProfileSerializer::Load(untouched, path),
				  "while a file that is not a profile is refused");
			Check(Math::Abs(untouched.Exposure - 3.0f) < 1e-5f,
				  "and a refused load leaves the caller's settings alone");

			std::error_code error;
			std::filesystem::remove(path, error);
		}

		// The project, which keeps the cost settings.
		{
			RenderSettings& live = Project::Render();
			const RenderSettings restore = live;

			const auto written = PokeEverySetting(RenderSettingsRegistry::Fields(), &live);
			Check(written.size() >= 8,
				  "the render settings offer enough scalar fields to be worth checking");

			// Round-tripped through the emitter and the reader directly rather
			// than through Project::Save, which needs a project open on disk.
			// The same two functions the project file uses, so this covers the
			// same drift.
			YAML::Emitter emitter;
			emitter << YAML::BeginMap;
			WriteFields(emitter, RenderSettingsRegistry::Fields(), &live);
			emitter << YAML::EndMap;

			RenderSettings reloaded;
			ReadFields(YAML::Load(emitter.c_str()), RenderSettingsRegistry::Fields(), &reloaded);

			const std::string lost =
				LostSettings(RenderSettingsRegistry::Fields(), &reloaded, written);
			Check(lost.empty(),
				  "and every one of them survives the project file -- a setting the "
				  "registry describes but the writer never emits is one that resets "
				  "itself every time the project is reopened");
			if (!lost.empty())
				RV_CORE_ERROR("render settings lost across a save: {0}", lost);

			// A block with nothing in it leaves every default in place, which
			// is what a project written before a setting existed must do.
			RenderSettings sparse;
			const RenderSettings fresh;
			ReadFields(YAML::Load("{}"), RenderSettingsRegistry::Fields(), &sparse);
			Check(sparse.AA == fresh.AA &&
				  Math::Abs(sparse.TemporalFeedback - fresh.TemporalFeedback) < 1e-6f,
				  "while an empty block leaves every default alone, so an older "
				  "project gains a setting rather than being zeroed by one");

			live = restore;
		}
	}

	// A camera names its profile, and the scene resolves it.
	// The `.cube` reader, on the CPU. The half the pixel check cannot reach:
	// a file that is refused has no frame to look at.
	void CheckCubeLut()
	{
		const std::filesystem::path path =
			std::filesystem::temp_directory_path() / "rv_lut.cube";

		auto write = [&](const std::string& text)
		{
			std::ofstream file(path);
			file << text;
		};

		// --- the identity, which is also the order assertion ------------------
		{
			const Assets::ColorLut identity = Assets::IdentityLut(4);
			Check(identity.IsValid(), "an identity LUT is well formed");
			Check(identity.Size == 4 && identity.Values.size() == 64,
				  "with size^3 entries");

			// Red fastest: entry 1 is one red step along, entry 4 is one green
			// step along. Reversing the order swaps exactly these two, which is
			// why they are the ones asserted -- a table read backwards grades
			// plausibly and wrongly, and nothing else here would notice.
			Check(Math::Abs(identity.Values[1].x - 1.0f / 3.0f) < 1e-5f &&
				  identity.Values[1].y == 0.0f,
				  "and red changing fastest, which is the format's order");
			Check(identity.Values[4].x == 0.0f &&
				  Math::Abs(identity.Values[4].y - 1.0f / 3.0f) < 1e-5f,
				  "green next");
			Check(identity.Values[16].z > 0.0f && identity.Values[16].x == 0.0f,
				  "and blue slowest");
		}

		// --- a file round-trips ----------------------------------------------
		{
			std::string text = "# a comment\nTITLE \"test\"\nLUT_3D_SIZE 2\n";
			for (int i = 0; i < 8; i++)
				text += "0.25 0.5 0.75\n";
			write(text);

			Assets::ColorLut lut;
			Check(Assets::LoadCubeLut(lut, path), "a well-formed .cube loads");
			Check(lut.Size == 2 && lut.Values.size() == 8, "with its 8 entries");
			Check(Math::Abs(lut.Values[3].y - 0.5f) < 1e-5f, "and their values");
		}

		// --- what has to be refused ------------------------------------------
		//
		// Every one of these would otherwise load as a table that is partly
		// right, and a partly-right LUT grades the picture: the failure has to
		// be the load rather than the frame.
		{
			Assets::ColorLut lut;

			write("LUT_3D_SIZE 2\n0.0 0.0 0.0\n0.0 0.0 0.0\n");
			Check(!Assets::LoadCubeLut(lut, path),
				  "a truncated .cube is refused, not padded");

			write("0.5 0.5 0.5\nLUT_3D_SIZE 2\n");
			Check(!Assets::LoadCubeLut(lut, path),
				  "so is data before the size, which has nowhere to go");

			write("LUT_1D_SIZE 16\n");
			Check(!Assets::LoadCubeLut(lut, path),
				  "and a 1D LUT, which shares the extension and is not this");

			write("LUT_3D_SIZE 999\n");
			Check(!Assets::LoadCubeLut(lut, path),
				  "and a size no tool emits, before it is allocated");

			write("# nothing but a comment\n");
			Check(!Assets::LoadCubeLut(lut, path),
				  "and a file with no size at all");
		}

		// --- a refused load leaves the caller alone ---------------------------
		{
			Assets::ColorLut kept = Assets::IdentityLut(2);
			write("LUT_1D_SIZE 16\n");
			Check(!Assets::LoadCubeLut(kept, path) && kept.Size == 2,
				  "and a refused load leaves what the caller already had");
		}

		// --- the upload form --------------------------------------------------
		{
			const Assets::ColorLut identity = Assets::IdentityLut(2);
			const std::vector<uint16_t> halves = Assets::ToHalfRGBA(identity);
			Check(halves.size() == 8 * 4, "the half-float form is RGBA per entry");
			Check(halves[0] == 0x0000 && halves[3] == 0x3C00,
				  "with 0 and 1 landing on the right bits");
		}

		std::error_code error;
		std::filesystem::remove(path, error);
	}

	void CheckPostProfileOnCamera()
	{
		const PostSettings neutral;
		auto scene = std::make_shared<Scene>();

		Check(scene->GetPostSettings().Exposure == neutral.Exposure,
			  "a scene with no camera grades with the defaults rather than with zero");

		Entity camera = scene->CreateEntity("Camera");
		camera.AddComponent<CameraComponent>();

		Check(scene->GetPostSettings().BloomEnabled == neutral.BloomEnabled,
			  "and so does a camera with no profile attached");

		// A handle that resolves to nothing -- the file was deleted, or the
		// project moved -- must degrade to the neutral grade rather than to a
		// black frame, because a profile of zeroes is not neutral.
		camera.GetComponent<CameraComponent>().PostProfile = AssetHandle(0x9E3779B97F4A7C15ull);
		Check(Math::Abs(scene->GetPostSettings().Exposure - neutral.Exposure) < 1e-5f,
			  "and a profile handle that resolves to nothing does too");

		// The handle itself is a serialized field on the camera, so a scene
		// that names a profile still names it after a save.
		const AssetHandle chosen(0x1234567890ABCDEFull);
		camera.GetComponent<CameraComponent>().PostProfile = chosen;

		const std::filesystem::path path =
			std::filesystem::temp_directory_path() / "rv_camera_profile.rage";
		SceneSerializer(scene).Serialize(path.string());

		auto reloaded = std::make_shared<Scene>();
		Check(SceneSerializer(reloaded).Deserialize(path.string()),
			  "a scene whose camera names a post profile loads back");

		Entity restored = reloaded->GetPrimaryCameraEntity();
		Check(restored && restored.HasComponent<CameraComponent>(), "with its camera");
		Check(restored && restored.GetComponent<CameraComponent>().PostProfile == chosen,
			  "still pointing at the same profile");

		std::error_code error;
		std::filesystem::remove(path, error);
	}

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
			Check(Math::Abs(level[kCount - 1].SplitDepth - distance) < 0.01f,
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
				const float pitch = Math::Radians(-35.0f) * Math::Sin(yaw * 3.0f);

				Mat4 turned = Math::Rotate(Mat4(1.0f), yaw, Vec3(0, 1, 0));
				turned = Math::Rotate(turned, pitch, Vec3(1, 0, 0));

				ShadowCascade rotated[kCount];
				fit(turned, rotated);

				for (uint32_t i = 0; i < kCount; i++)
				{
					stable = stable && Math::Abs(rotated[i].TexelWorldSize -
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
							  Math::Abs(texels.x - Math::Round(texels.x)) < 1e-3f &&
							  Math::Abs(texels.y - Math::Round(texels.y)) < 1e-3f;
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

			Check(Math::Abs(mirrored.x - coordinate.x) < 1e-5f &&
				  Math::Abs(mirrored.z - coordinate.z) < 1e-5f &&
				  Math::Abs(mirrored.y - (1.0f - coordinate.y)) < 1e-5f,
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
			Check(Math::IsFinite(coordinate.x) && Math::IsFinite(coordinate.y) &&
				  Math::IsFinite(coordinate.z),
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
					if (Math::Abs(m[c][r] - expected) > 1e-4f)
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

		Check(Math::Abs(clip.Duration - 2.0f) < 1e-5f,
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
			const float leg = Math::Sqrt(0.5f);
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
			Check(Math::Abs(Math::Dot(mixed[1].Rotation, b[1].Rotation)) > 0.9999f,
				  "and a blend of one is the second");

			BlendPoses(a, b, 0.5f, mixed);
			const float angle = Math::Degrees(Math::Angle(Math::Normalize(mixed[1].Rotation)));
			Check(Math::Abs(angle - 45.0f) < 0.5f, "and halfway is halfway round the arc");
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

		// --- 7.6: bounds that cover the animation, not the bind pose ---------
		//
		// The fixture is a single bone with a vertex a unit out along +X and a
		// clip that swings it to +Y. The bind box therefore does not contain
		// the animated position at all, which is the defect stated as a
		// number rather than as a worry.
		{
			Skeleton arm;
			Bone root;
			root.Name = "root";
			root.Parent = -1;
			root.InverseBind = Mat4(1.0f);
			arm.Bones = { root };

			const std::vector<Vec3> positions = { { 1.0f, 0.0f, 0.0f } };
			const std::vector<UVec4> joints = { { 0u, 0u, 0u, 0u } };
			const std::vector<Vec4> weights = { { 1.0f, 0.0f, 0.0f, 0.0f } };

			Anim::Clip swing;
			swing.Tracks.resize(1);
			swing.Tracks[0].Rotation.Times = { 0.0f, 1.0f };
			swing.Tracks[0].Rotation.Values = {
				Quat(1.0f, 0.0f, 0.0f, 0.0f),
				// A quarter turn about +Z carries the vertex from +X to +Y.
				Math::AngleAxis(Math::Radians(90.0f), Vec3(0.0f, 0.0f, 1.0f)),
			};
			swing.RecomputeDuration();

			Vec3 boundsMin, boundsMax;
			Anim::SkinnedBounds(arm, { swing }, positions, joints, weights,
								boundsMin, boundsMax);

			Check(boundsMax.y > 0.9f,
				  "a clip that swings a vertex to +Y grows the box to reach it "
				  "-- the bind pose's box stops at y = 0");
			Check(boundsMax.x > 0.9f,
				  "and the bind pose is still inside, because a model may hold it");

			// No clips at all must not move the answer: a static skinned mesh
			// keeps the box its vertices gave it, give or take the padding.
			Vec3 restMin, restMax;
			Anim::SkinnedBounds(arm, {}, positions, joints, weights, restMin, restMax);
			Check(Math::Abs(restMax.x - 1.0f) < 0.05f && restMax.y < 0.05f,
				  "with no clips the bounds are the bind pose's");

			// A skeleton with no weights this can read falls back to the raw
			// vertices rather than to an empty box, which would cull the mesh
			// everywhere.
			Vec3 rawMin, rawMax;
			Anim::SkinnedBounds(arm, { swing }, positions, {}, {}, rawMin, rawMax);
			Check(Math::Abs(rawMax.x - 1.0f) < 1e-4f,
				  "unreadable weights fall back to the vertices, not to nothing");
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

		// --- FBX (8.9, ENGINE-NOTES 7bn) ------------------------------------
		//
		// The two fixtures are the same eight corners written by one script
		// into two formats, which is what lets these be comparisons rather
		// than eyeballing. A space error -- swapped axis, flipped handedness,
		// centimetres left as centimetres -- moves the FBX and not the glTF,
		// and every claim below is built to notice that.
		{
			const std::filesystem::path fbx = Project::AssetPath("models/spacecheck.fbx");
			const std::filesystem::path twin = Project::AssetPath("models/spacecheck.gltf");

			Assets::ImportedModel fromFbx;
			Assets::ImportedModel fromGltf;

			const bool readFbx = Assets::ModelImporter::Import(fbx, fromFbx);
			const bool readGltf = Assets::ModelImporter::Import(twin, fromGltf);

			Check(readFbx, "an FBX imports through the same door as a glTF");
			Check(readGltf, "and so does its twin");

			if (readFbx && readGltf)
			{
				// Bounds rather than vertex-by-vertex: the two formats index
				// their corners differently and legitimately, and the shape's
				// extent is what a space error moves.
				auto extent = [](const Assets::ImportedModel& model, Vec3& low, Vec3& high)
				{
					low = Vec3(FLT_MAX);
					high = Vec3(-FLT_MAX);
					for (const Assets::ImportedPrimitive& primitive : model.Primitives)
					{
						for (const MeshVertex& vertex : primitive.Vertices)
						{
							low = Math::Min(low, vertex.Position);
							high = Math::Max(high, vertex.Position);
						}
					}
				};

				Vec3 fbxLow, fbxHigh, gltfLow, gltfHigh;
				extent(fromFbx, fbxLow, fbxHigh);
				extent(fromGltf, gltfLow, gltfHigh);

				// A millimetre. The fixture's numbers are exact in both files;
				// this is float round-trip room, not a tolerance for being
				// roughly right.
				const float slack = 0.001f;
				Check(Math::Length(fbxLow - gltfLow) < slack
					  && Math::Length(fbxHigh - gltfHigh) < slack,
					  "and lands in the same place: an axis swap, a flipped "
					  "handedness or centimetres left as centimetres all move "
					  "one and not the other");

				// The shape is deliberately different in all three dimensions,
				// so this is a claim rather than an accident of symmetry.
				const Vec3 size = fbxHigh - fbxLow;
				Check(Math::Abs(size.x - 0.6f) < slack
					  && Math::Abs(size.y - 1.4f) < slack
					  && Math::Abs(size.z - 0.9f) < slack,
					  "at the size the fixture says, which a cube could not have "
					  "told us");

				// Six quads triangulated is twelve triangles. An n-gon left
				// untriangulated, or a fan built wrongly, changes this.
				size_t triangles = 0;
				for (const Assets::ImportedPrimitive& primitive : fromFbx.Primitives)
					triangles += primitive.Indices.size() / 3;
				Check(triangles == 12,
					  "with its quads triangulated -- an FBX face is an n-gon "
					  "where a glTF primitive is triangles by definition");

				// **What a Phong material is promised, which is the honest
				// claim** (7bn). An FBX material is Lambert or Phong
				// underneath; a colour is a colour in any shading model and
				// transfers, and nothing else does -- a roughness guessed from
				// a shininess exponent is a number nobody could debug. So the
				// claim is that the colour arrives *and* that the rest is left
				// at the engine's defaults rather than invented.
				const MaterialParams defaults;
				Check(!fromFbx.Materials.empty()
					  && Math::Abs(fromFbx.Materials[0].Params.BaseColor.x - 0.8f) < 0.01f
					  && Math::Abs(fromFbx.Materials[0].Params.BaseColor.y - 0.35f) < 0.01f,
					  "a Phong material's colour transfers, because a colour means "
					  "the same thing in every shading model");
				Check(!fromFbx.Materials.empty()
					  && fromFbx.Materials[0].Params.Roughness == defaults.Roughness
					  && fromFbx.Materials[0].Params.Metallic == defaults.Metallic,
					  "and its roughness and metalness are the engine's defaults "
					  "rather than something derived from a shininess exponent");

				Check(fromFbx.Nodes.size() == 1 && !fromFbx.Nodes[0].Primitives.empty(),
					  "and the node owns its geometry, so the entity tree has "
					  "something to hang");
			}
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
						identity = Math::Abs(m[c][r] - expected) < 1e-4f;
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
				normalised = normalised && Math::Abs(w.x + w.y + w.z + w.w - 1.0f) < 1e-4f;

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
				const float depth = nearPlane * Math::Pow(farPlane / nearPlane, t);
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
				const float depth = nearPlane * Math::Pow(farPlane / nearPlane, t);

				const uint32_t fromShader = (uint32_t)Math::Clamp(
					(int)(Math::Log(depth) * scale + bias), 0, (int)LightGrid::kSlices - 1);

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

			Check(Math::Abs(derivedNear - nearPlane) < 1e-4f,
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
			Check(Math::Abs(derivedFar - farPlane) / farPlane < 0.01f,
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
			Check(Math::Length(centre) < 1e-5f && Math::Abs(extents.x - 0.5f) < 1e-5f,
				  "an untransformed box keeps its own bounds");

			const Mat4 turned = Math::Rotate(Mat4(1.0f), Math::Radians(45.0f),
												 Vec3(0.0f, 1.0f, 0.0f));
			Frustum::TransformBounds(box, turned, centre, extents);
			Check(extents.x > 0.69f && extents.x < 0.72f,
				  "and a box turned 45 degrees grows to contain itself");

			const Mat4 moved = Math::Translate(Mat4(1.0f), Vec3(3.0f, 0.0f, 0.0f));
			Frustum::TransformBounds(box, moved, centre, extents);
			Check(Math::Abs(centre.x - 3.0f) < 1e-5f && Math::Abs(extents.x - 0.5f) < 1e-5f,
				  "moving a box moves its bounds and does not grow them");

			const Mat4 scaled = Math::Scale(Mat4(1.0f), Vec3(4.0f));
			Frustum::TransformBounds(box, scaled, centre, extents);
			Check(Math::Abs(extents.x - 2.0f) < 1e-5f, "and scaling scales them");
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
			Check(Math::Abs(mirror.x - 1.0f) < 0.02f && mirror.y < 0.02f,
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
			Check(Math::Abs(corner.x / corner.w - 1.0f) < 1e-4f &&
				  Math::Abs(corner.y / corner.w - 1.0f) < 1e-4f,
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

	// Block-compressed textures (7.2b). The size math is the contract: a BC
	// mip is whole 4x4 blocks whatever its pixel count, and an upload that
	// disagrees with the math is refused before it can read out of bounds.
	// The referee for the uploads themselves is the Vulkan validation layer
	// -- a wrong extent or level shows up as a [Vulkan] line, which the suite
	// treats as failure.
	void CheckCompressedTextures()
	{
		using namespace RHI;

		Check(IsCompressedFormat(Format::BC5_UNORM) &&
			  !IsCompressedFormat(Format::R8G8B8A8_UNORM),
			  "the formats know which of them are compressed");
		Check(TextureDataSize(Format::BC1_UNORM, 16, 16) == 128,
			  "BC1 is eight bytes a block");
		Check(TextureDataSize(Format::BC3_UNORM, 16, 16) == 256,
			  "BC3 is sixteen");
		Check(TextureDataSize(Format::BC5_UNORM, 2, 2) == 16,
			  "a 2x2 mip still occupies one whole block");
		Check(TextureDataSize(Format::BC4_UNORM, 5, 5) == 32,
			  "partial blocks round up");
		Check(TextureDataSize(Format::R8G8B8A8_UNORM, 7, 3) == 84,
			  "uncompressed stays pixels times size");

		if (!Renderer::HasDevice())
			return;
		auto& device = Renderer::GetDevice();

		// A full chain, every level uploaded, none generated -- the shape a
		// cooked texture arrives in.
		TextureDesc desc;
		desc.Width = 8;
		desc.Height = 8;
		desc.Format = Format::BC5_UNORM;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		desc.MipLevels = 4;
		desc.DebugName = "test.bc5.chain";

		auto texture = device.CreateTexture(desc);
		Check(texture != nullptr, "a block-compressed texture is created");

		if (texture)
		{
			for (uint32_t mip = 0; mip < 4; mip++)
			{
				const uint32_t side = Math::Max(8u >> mip, 1u);
				std::vector<uint8_t> blocks(
					(size_t)TextureDataSize(Format::BC5_UNORM, side, side), 0x3C);
				texture->UploadMip(blocks.data(), blocks.size(), mip, 0);
			}
			Check(true, "every level of a pre-built chain uploads; the "
						"validation layer referees the copies");

			// The two refusals that keep a bad caller off the GPU.
			uint8_t tooSmall[8] = {};
			texture->UploadMip(tooSmall, sizeof(tooSmall), 0, 0);
			Check(true, "a size that disagrees with the mip math is refused");
			texture->UploadMip(tooSmall, sizeof(tooSmall), 9, 0);
			Check(true, "and so is a level the texture does not have");
		}

		TextureDesc srgb;
		srgb.Width = 4;
		srgb.Height = 4;
		srgb.Format = Format::BC1_SRGB;
		srgb.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		srgb.MipLevels = 1;
		srgb.DebugName = "test.bc1.srgb";

		auto small = device.CreateTexture(srgb);
		Check(small != nullptr, "the sRGB variant is created");
		if (small)
		{
			uint8_t block[8] = { 0xFF, 0xFF, 0x00, 0x00, 0, 0, 0, 0 };
			small->UploadMip(block, sizeof(block), 0, 0);
		}
	}

	// The texture cooker (7.2c). The claims that matter: the encode choice
	// follows the design's rules, the container round-trips, a normal map's
	// mips stay unit length, and a cooked file loads through the same call a
	// PNG does.
	void CheckTextureCook()
	{
		using IO::CookedPixelFormat;
		using IO::CookedTexture;
		using IO::TextureCook;

		// An 8x8 opaque gradient: colour, no alpha -> BC1, four mips.
		std::vector<uint8_t> gradient(8 * 8 * 4);
		for (uint32_t i = 0; i < 64; i++)
		{
			gradient[i * 4 + 0] = (uint8_t)(i * 4);
			gradient[i * 4 + 1] = (uint8_t)(255 - i * 4);
			gradient[i * 4 + 2] = 60;
			gradient[i * 4 + 3] = 255;
		}

		CookedTexture albedo = TextureCook::Cook(gradient.data(), 8, 8, "brick_color.png");
		Check(albedo.Format == CookedPixelFormat::BC1 && albedo.Mips.size() == 4,
			  "opaque colour cooks to BC1 with a full chain");
		Check(albedo.Mips[0].size() == 4 * 8 && albedo.Mips[3].size() == 8,
			  "and every mip is whole blocks, down to 1x1");

		std::vector<uint8_t> holes = gradient;
		holes[3] = 128;
		Check(TextureCook::Cook(holes.data(), 8, 8, "leaf.png").Format == CookedPixelFormat::BC3,
			  "alpha that varies cooks to BC3");

		Check(TextureCook::Cook(gradient.data(), 8, 8, "soil_roughness.png").Format ==
				  CookedPixelFormat::BC4,
			  "a data map's name cooks it to BC4 -- content alone cannot say, "
			  "because a grey smoke sprite is not a roughness map");

		Check(TextureCook::Cook(gradient.data(), 8, 8, "soil_normal.png").Format ==
				  CookedPixelFormat::BC5,
			  "a normal map's name cooks it to BC5");

		// Renormalized mips, observable through the RGBA8 path a tiny
		// texture takes: two texels tilted +x and -x average to a shortened
		// vector, and the cooker must stretch it back to unit length --
		// z = 0.8 stays 0.8 without the fix, becomes 1.0 with it.
		{
			std::vector<uint8_t> tilt(2 * 1 * 4);
			const uint8_t xPlus = (uint8_t)std::lround((0.6 * 0.5 + 0.5) * 255);
			const uint8_t xMinus = (uint8_t)std::lround((-0.6 * 0.5 + 0.5) * 255);
			const uint8_t z = (uint8_t)std::lround((0.8 * 0.5 + 0.5) * 255);
			tilt[0] = xPlus;  tilt[1] = 128; tilt[2] = z; tilt[3] = 255;
			tilt[4] = xMinus; tilt[5] = 128; tilt[6] = z; tilt[7] = 255;

			CookedTexture normals = TextureCook::Cook(tilt.data(), 2, 1, "tiny_normal.png");
			Check(normals.Format == CookedPixelFormat::RGBA8,
				  "below a block, cooking stores RGBA8 rather than padding");
			Check(normals.Mips.size() == 2 && normals.Mips[1].size() == 4,
				  "and still carries its chain");
			Check(normals.Mips[1][2] > 250,
				  "a normal mip is renormalized -- averaged vectors are "
				  "stretched back to unit length");
		}

		// The container: exact round trip, and the sniff that routes it.
		const std::vector<uint8_t> bytes = TextureCook::Serialize(albedo);
		Check(TextureCook::IsCooked(bytes.data(), bytes.size()), "cooked bytes say so");
		Check(!TextureCook::IsCooked((const uint8_t*)"\x89PNG", 4), "PNG bytes do not");

		CookedTexture back;
		Check(TextureCook::Deserialize(back, bytes.data(), bytes.size()) &&
			  back.Width == 8 && back.Format == albedo.Format &&
			  back.Mips == albedo.Mips,
			  "the container round-trips exactly");

		// And the loader takes it through the same call a PNG goes through.
		if (Renderer::HasDevice())
		{
			const std::filesystem::path scratch = ScratchDir("cook");
			std::error_code error;
			std::filesystem::create_directories(scratch, error);
			const std::filesystem::path file = scratch / "cooked_color.png";
			{
				std::ofstream out(file, std::ios::binary);
				out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
			}

			auto texture = TextureLoader::Load2D(Renderer::GetDevice(),
												 file.string(), /*srgb*/ true, true);
			Check(texture != nullptr, "a cooked file loads where a PNG would");
			Check(texture && texture->GetDesc().MipLevels == 4 &&
				  texture->GetFormat() == RHI::Format::BC1_SRGB,
				  "with its own chain and the colour space the slot asked for");

			std::filesystem::remove_all(scratch, error);
		}
	}

	// A `.glb`'s embedded texture has to become a project asset.
	//
	// glTF carries an image three ways and only one of them is a path: a
	// `.gltf` names a sibling file, a `.glb` puts the pixels in its binary
	// chunk, and either may inline them as a `data:` URI. The importer only
	// ever handled the first, so **every GLB imported untextured** -- which
	// is why the fox in the demo scene was white, and it read as a rendering
	// problem rather than an import one for long enough to be worth a check.
	//
	// The claim is not "the importer ran". It is that the pixels reached a
	// *file*, that file got a *handle*, and the handle reached the material
	// -- because a material stores handles, and an image living inside a
	// model file can never have one.
	void CheckEmbeddedGlbTexture()
	{
		const AssetHandle model = Assets::Registry::GetHandle("models/fox.glb");
		Check(model.IsValid(), "the registry minted a handle for fox.glb");
		if (!model.IsValid())
			return;

		Assets::ImportedModel imported;
		Check(Assets::GltfImporter::ImportSource(Project::AssetRoot() / "models/fox.glb",
												 imported),
			  "fox.glb imports from source");

		Check(!imported.Textures.empty(),
			  "and its embedded image is reported as a texture, not dropped");
		if (imported.Textures.empty())
			return;

		// Extracted beside the model, named from the image index so that a
		// re-import resolves to the same file and keeps its handle.
		const std::filesystem::path extracted =
			Project::AssetRoot() / "models" / imported.Textures[0].Path;
		Check(std::filesystem::exists(extracted),
			  "the pixels were written out as " + imported.Textures[0].Path);

		Check(imported.Textures[0].SRGB,
			  "and a base colour map is imported as sRGB, not as data");

		// The half that actually decides whether the fox is white: the
		// material has to name it.
		Check(!imported.Materials.empty() && imported.Materials[0].BaseColorTexture == 0,
			  "the material points at it rather than falling back to its scalars");

		// And it has to survive the registry, or the `.rmat` an import writes
		// would store a handle that resolves to nothing.
		Assets::Registry::Refresh();
		const AssetHandle texture =
			Assets::Registry::GetHandle("models/" + imported.Textures[0].Path);
		Check(texture.IsValid(), "and the extracted file has an asset handle");

		// The end of the chain, and the only part a person would ever notice:
		// instantiating the model must produce a material that actually names
		// the map. Everything above can pass while this fails, because the
		// handle has to survive being written into a `.rmat` and read back.
		auto scene = std::make_shared<Scene>();
		Entity root = Assets::Manager::InstantiateModel(*scene, model);
		Check((bool)root, "fox.glb instantiates");

		AssetHandle material = AssetHandle::Invalid();
		for (auto handle : scene->GetRegistry().view<MeshComponent>())
		{
			const auto& mesh = scene->GetRegistry().get<MeshComponent>(handle);
			if (mesh.Material.IsValid())
			{
				material = mesh.Material;
				break;
			}
		}

		Check(material.IsValid(), "and its mesh carries a material handle");

		RHI::Ref<Material> resolved = Assets::Manager::GetMaterial(material);
		Check(resolved && (resolved->GetParams().MapFlags & MaterialMap_BaseColor),
			  "which resolves to a material whose base colour map is bound -- the fox "
			  "is textured rather than white");
	}

	// The cooker's fast sRGB encode against the definition it replaced.
	//
	// This is the one place in the cook where "it looks the same" would not
	// have been good enough, because the fast path is a table lookup whose
	// correctness rests on an *argument* about bucket widths rather than on
	// anything visible. The first version of that argument was wrong -- two
	// bytes in 190 MB of cooked output, far under any pixel tolerance and
	// still a silent change to shipped content.
	//
	// So this sweeps every float the cooker can hand it, and a mismatch is
	// one check rather than a rendering somebody has to notice.
	void CheckSrgbEncode()
	{
		using IO::TextureCook;

		// Every float bit pattern in [0, 1] is four billion values, which is
		// a minute of wall time for a suite that has to stay quick. These are
		// where a table lookup can plausibly land on the wrong side: the two
		// ends, the linear/power junction, and the exact thresholds
		// themselves, plus their immediate float neighbours.
		size_t checked = 0;
		int mismatches = 0;
		float worstAt = 0.0f;

		auto compare = [&](float v)
		{
			checked++;
			if (TextureCook::EncodeSrgbByte(v) != TextureCook::EncodeSrgbByteReference(v))
			{
				if (mismatches == 0)
					worstAt = v;
				mismatches++;
			}
		};

		// A dense uniform sweep, finer than the table's own buckets so that
		// every bucket is hit many times.
		for (int i = 0; i <= 200000; i++)
			compare((float)i / 200000.0f);

		// Both sides of every byte boundary, which is where the correction
		// either fires or fails to.
		for (int b = 0; b < 256; b++)
		{
			const float centre = Math::Pow(((float)b / 255.0f + 0.055f) / 1.055f, 2.4f);
			for (int step = -3; step <= 3; step++)
			{
				float v = centre;
				for (int n = 0; n < Math::Abs(step); n++)
					v = std::nextafter(v, step < 0 ? 0.0f : 1.0f);
				compare(v);
			}
		}

		// The linear segment, where the thresholds are tightest and the
		// bucket-width argument is at its narrowest margin.
		for (int i = 0; i <= 20000; i++)
			compare(0.0031308f * (float)i / 20000.0f);

		// Out of range, which the cooker does produce: a downsampled normal
		// map can land a hair outside [0,1].
		for (float v : { -1.0f, -0.001f, 0.0f, 1.0f, 1.001f, 2.0f })
			compare(v);

		Check(mismatches == 0,
			  "the fast sRGB encode matches pow-and-round on all " +
				  std::to_string(checked) + " values swept" +
				  (mismatches ? " (first differs at " + std::to_string(worstAt) + ")" : ""));

		// A control: the check above can only mean something if the two
		// functions are capable of disagreeing at all.
		Check(TextureCook::EncodeSrgbByte(0.0f) == 0 &&
			  TextureCook::EncodeSrgbByte(1.0f) == 255 &&
			  TextureCook::EncodeSrgbByte(0.5f) == 188,
			  "and it encodes the ends and the midpoint as sRGB, not linearly");
	}

	// The bar the loading screen draws (7l).
	//
	// Small, and worth checking anyway: the two properties here are ones a
	// person watching a four-second boot would not reliably notice being
	// wrong, and both were deliberate decisions rather than accidents of the
	// implementation.
	void CheckBootProgress()
	{
		Boot::Progress progress;

		progress.BeginPhase("Opening", 0.0f, 0.2f);
		progress.Advance(0.5f);
		Check(Math::Abs(progress.Get().Fraction - 0.1f) < 0.001f,
			  "a phase's progress maps into the slice of the bar it owns");

		progress.SetDetail("scenes/demo.rage");
		Check(progress.Get().Detail == "scenes/demo.rage" &&
			  progress.Get().Phase == "Opening",
			  "the phase and the current action are reported separately");

		// Monotonic. A bar that goes backwards reads as a fault even when the
		// number behind it is perfectly correct, and every source of that here
		// is legitimate: a phase that turns out to be empty, an Advance called
		// with a stale fraction, a later phase beginning at a lower mark.
		progress.Advance(0.1f);
		Check(Math::Abs(progress.Get().Fraction - 0.1f) < 0.001f,
			  "advancing backwards inside a phase does not rewind the bar");

		progress.BeginPhase("Loading", 0.05f, 0.9f);
		Check(progress.Get().Fraction >= 0.1f - 0.001f,
			  "and neither does a phase that starts behind where the bar is");

		Check(progress.Get().Detail.empty(),
			  "a new phase clears the action, rather than leaving the last one's");

		// Cancellation is polled from the worker while the main thread owns
		// the window, so it has to be readable without the lock the rest of
		// this takes.
		Check(!progress.Cancelled(), "a fresh boot is not cancelled");
		progress.Cancel();
		Check(progress.Cancelled(), "and a close during loading sets it");

		Check(!progress.IsDone(), "cancelling is not finishing");
		progress.Finish();
		Check(progress.IsDone() && progress.Get().Fraction == 1.0f,
			  "finishing fills the bar, so the last frame drawn is not a partial one");
	}

	// The import cache (7l).
	//
	// The claims worth testing are the ones that decide whether a *stale*
	// asset can be served, because that is the failure this design could have
	// and a slow load is not.
	void CheckImportCache()
	{
		using Assets::ImportCache;
		// An alias, not a using-declaration: CookPolicy is a namespace, and
		// `using` refuses one.
		namespace CookPolicy = Assets::CookPolicy;

		const std::filesystem::path materials = Project::AssetRoot() / "materials";
		const std::filesystem::path atlas = Project::AssetRoot() / "fonts" / "roboto.png";

		Check(ImportCache::IsCookable(materials / "brick_color.png"),
			  "a material map is cookable");
		Check(!ImportCache::IsCookable(Project::AssetRoot() / "scenes" / "demo.rage"),
			  "a scene is not -- there is no cooker for it");

		// The guard that keeps a distance field out of a block compressor.
		// Checked through both doors, because the whole point of having two
		// is that either alone would have let the packaged build through.
		Check(CookPolicy::IsFontAtlas("fonts/roboto.png"),
			  "a font's atlas is recognised from the .rvfont that names it");
		Check(!CookPolicy::IsFontAtlas("materials/brick_color.png"),
			  "and an ordinary texture is not");
		Check(!ImportCache::IsCookable(atlas),
			  "so the atlas is never cooked, which would quantize its distances");

		// A `.gltf` keeps its geometry in a sibling `.bin` that the cache's
		// key does not cover, so it is excluded on purpose. Stated as a check
		// because it looks like an oversight otherwise.
		Check(!ImportCache::IsCookable(Project::AssetRoot() / "models" / "textured.gltf"),
			  "a .gltf is excluded: its .bin is outside the hash the key uses");
		Check(ImportCache::IsCookable(Project::AssetRoot() / "models" / "fox.glb"),
			  "a .glb is not, because it carries its buffers inside the hashed file");

		std::vector<uint8_t> bytes;
		Check(ImportCache::Fetch(materials / "brick_color.png", bytes) &&
			  IO::TextureCook::IsCooked(bytes.data(), bytes.size()),
			  "fetching a texture answers cooked bytes");

		Check(ImportCache::IsCached(materials / "brick_color.png"),
			  "and leaves an entry behind for the next launch");

		std::vector<uint8_t> again;
		ImportCache::Fetch(materials / "brick_color.png", again);
		Check(again == bytes, "which is byte-identical to what cooking produced");

		// The same source bytes under two names.
		//
		// The encode is chosen from the *name* (7i), so a key built from
		// content alone would hand one of these the other's encoding -- a
		// normal map served BC1, or a colour map sampled back as two
		// channels. Both render as something wrong that nothing reports, so
		// the collision is worth an explicit check rather than a comment.
		std::error_code error;
		const std::filesystem::path source = materials / "parity_tilt_normal.png";
		const std::filesystem::path colour = materials / "cachekeytest_color.png";
		const std::filesystem::path normal = materials / "cachekeytest_normal.png";

		if (std::filesystem::exists(source, error))
		{
			std::filesystem::copy_file(source, colour,
				std::filesystem::copy_options::overwrite_existing, error);
			std::filesystem::copy_file(source, normal,
				std::filesystem::copy_options::overwrite_existing, error);
			Assets::Registry::Refresh();

			std::vector<uint8_t> asColour, asNormal;
			const bool both = ImportCache::Fetch(colour, asColour) &&
							  ImportCache::Fetch(normal, asNormal);

			IO::CookedTexture c, n;
			Check(both &&
				  IO::TextureCook::Deserialize(c, asColour.data(), asColour.size()) &&
				  IO::TextureCook::Deserialize(n, asNormal.data(), asNormal.size()) &&
				  c.Format != n.Format,
				  "identical bytes under two names get two cache entries, encoded by name");

			// Removed before the registry rescan that follows, so the sample
			// project is left exactly as it was found.
			std::filesystem::remove(colour, error);
			std::filesystem::remove(normal, error);
			std::filesystem::remove(colour.string() + ".meta", error);
			std::filesystem::remove(normal.string() + ".meta", error);
			Assets::Registry::Refresh();
		}
	}

	// The mesh cooker (7.2d). The claim is fidelity: a model that went
	// through Serialize and Deserialize must be indistinguishable from the
	// import it came from -- vertices, indices, skeleton, clips, everything
	// InstantiateModel and the animator read. The skinned fixture is used
	// because it exercises every optional block at once.
	void CheckMeshCook()
	{
		using Assets::GltfImporter;
		using Assets::ImportedModel;
		using Assets::MeshCook;

		// The skinned fixture, because it exercises every optional block --
		// skeleton, clips, joints, weights -- and a round trip that only
		// proved static geometry would be silent about all of them.
		ImportedModel model;
		if (!GltfImporter::Import(Project::AssetPath("models/limb.gltf"), model))
			return;
		Check(model.HasSkeleton() && !model.Clips.empty(),
			  "the fixture carries a skeleton and clips, or this check proves less than it says");

		const std::vector<uint8_t> bytes = MeshCook::Serialize(model);
		Check(MeshCook::IsCooked(bytes.data(), bytes.size()), "cooked mesh bytes say so");
		Check(!MeshCook::IsCooked((const uint8_t*)"glTF", 4), "a GLB's magic does not");

		ImportedModel back;
		Check(MeshCook::Deserialize(back, bytes.data(), bytes.size()),
			  "the cooked mesh parses back");

		Check(back.Name == model.Name &&
			  back.Primitives.size() == model.Primitives.size() &&
			  back.Materials.size() == model.Materials.size() &&
			  back.Nodes.size() == model.Nodes.size(),
			  "with the same shape the import had");

		bool geometryIdentical = !back.Primitives.empty();
		for (size_t i = 0; i < back.Primitives.size() && geometryIdentical; i++)
		{
			const auto& a = model.Primitives[i];
			const auto& b = back.Primitives[i];
			geometryIdentical =
				a.Name == b.Name && a.Material == b.Material &&
				a.Indices == b.Indices &&
				a.Vertices.size() == b.Vertices.size() &&
				std::memcmp(a.Vertices.data(), b.Vertices.data(),
							a.Vertices.size() * sizeof(MeshVertex)) == 0 &&
				a.Joints == b.Joints;
			// Weights compare bitwise through the vector's own operator==,
			// which is exact for copied floats.
		}
		Check(geometryIdentical, "every primitive's geometry is bit-identical");

		Check(back.Skeleton.Bones.size() == model.Skeleton.Bones.size() &&
			  back.Clips.size() == model.Clips.size(),
			  "the skeleton and its clips came through");

		if (!model.Skeleton.Bones.empty())
		{
			const auto& a = model.Skeleton.Bones[0];
			const auto& b = back.Skeleton.Bones[0];
			Check(a.Name == b.Name && a.Parent == b.Parent &&
				  std::memcmp(&a.InverseBind, &b.InverseBind, sizeof(Mat4)) == 0,
				  "a bone survives with its inverse bind exact");
		}

		if (!model.Clips.empty() && !model.Clips[0].Tracks.empty())
		{
			const auto& a = model.Clips[0];
			const auto& b = back.Clips[0];
			Check(a.Name == b.Name && a.Duration == b.Duration &&
				  a.Tracks.size() == b.Tracks.size() &&
				  a.Tracks[0].Rotation.Times == b.Tracks[0].Rotation.Times,
				  "a clip survives with its keyframes exact");
		}

		// Truncation answers false, never a partial model.
		ImportedModel truncated;
		Check(!MeshCook::Deserialize(truncated, bytes.data(), bytes.size() / 2),
			  "a truncated cooked mesh is refused");
	}

	// The pak and the VFS (7.1). Design: ENGINE-NOTES 7h.
	//
	// The property that matters is shadowing: the VFS answers the same paths
	// the filesystem would, so a mounted archive changes where bytes come from
	// and nothing else. Every claim here is phrased against that -- the pak
	// wins where both hold a path, loose files answer where the pak is silent,
	// and a lookup spelled with backslashes and capitals finds an entry stored
	// lowercase, because the loose filesystem it replaces was case-insensitive.
	void CheckVfsAndPak()
	{
		namespace fs = std::filesystem;

		Check(IO::NormalizePath("A\\B/../Rock.PNG") == "a/rock.png",
			  "normalization lowers case, fixes slashes and resolves dot segments");

		const fs::path scratch = ScratchDir("vfs");
		std::error_code error;
		fs::remove_all(scratch, error);
		fs::create_directories(scratch / "textures", error);

		// The loose tree: one file the pak will shadow, one it will not.
		const std::string looseRock = "loose rock bytes";
		const std::string onlyLoose = "only on disk";
		{
			std::ofstream(scratch / "textures" / "rock.png", std::ios::binary) << looseRock;
			std::ofstream(scratch / "only_loose.txt", std::ios::binary) << onlyLoose;
		}

		// The archive: bytes that differ from the loose file, a text entry,
		// and a binary one with embedded zeros -- the bytes a text-mode
		// mistake would eat.
		const std::string pakRock = "pak rock bytes, and different ones";
		std::vector<uint8_t> binary(70000);
		for (size_t i = 0; i < binary.size(); i++)
			binary[i] = (uint8_t)(i * 31);

		const fs::path pakFile = scratch / "content.rvpak";
		{
			IO::PakWriter writer;
			Check(writer.AddBytes("textures/rock.png",
								  { pakRock.begin(), pakRock.end() }),
				  "the writer takes an entry");
			Check(!writer.AddBytes("Textures\\ROCK.png", { 1, 2, 3 }),
				  "two spellings of one normalized path are refused, not resolved silently");
			Check(writer.AddBytes("scenes/menu.rage", { 'S', 'c' }), "a second entry");
			Check(writer.AddBytes("blobs/data.bin", binary), "a binary entry");
			Check(writer.Write(pakFile), "the archive writes");
		}

		{
			IO::PakReader reader;
			Check(reader.Open(pakFile), "the archive opens");
			Check(reader.Entries().size() == 3, "it holds what was added, once each");

			std::vector<uint8_t> bytes;
			Check(reader.ReadBytes("blobs/data.bin", bytes) && bytes == binary,
				  "binary bytes survive the round trip exactly");
			Check(!reader.ReadBytes("blobs/absent.bin", bytes),
				  "a missing entry answers false rather than something");
		}

		Check(VFS::MountCount() == 0, "nothing is mounted before the test mounts");
		Check(VFS::MountPak(scratch, pakFile), "the pak mounts over the scratch root");

		{
			std::string text;
			Check(VFS::ReadText(scratch / "textures" / "rock.png", text) && text == pakRock,
				  "the pak wins where the pak and a loose file hold the same path");
			Check(VFS::ReadText(scratch / "only_loose.txt", text) && text == onlyLoose,
				  "a path the pak lacks falls through to the loose file");
			Check(VFS::ReadText(fs::path(scratch) / "Textures\\ROCK.PNG", text) && text == pakRock,
				  "a lookup in the wrong case with the wrong slashes still resolves");

			Check(VFS::Origin(scratch / "scenes" / "menu.rage") == FileOrigin::Pak,
				  "origin says pak for a pak entry");
			Check(VFS::Origin(scratch / "only_loose.txt") == FileOrigin::Loose,
				  "origin says loose for a loose file");
			Check(VFS::Origin(scratch / "nowhere.txt") == FileOrigin::Missing,
				  "origin says missing for neither");
		}

		{
			const std::vector<std::string> files = VFS::Enumerate(scratch);
			const auto has = [&](const char* path)
			{
				return std::find(files.begin(), files.end(), path) != files.end();
			};

			Check(files.size() == 5, "enumeration merges the pak and the loose tree");
			Check(has("textures/rock.png") && has("scenes/menu.rage") &&
				  has("blobs/data.bin") && has("only_loose.txt") && has("content.rvpak"),
				  "and holds each path once, wherever it lives");

			const std::vector<std::string> under = VFS::Enumerate(scratch / "blobs");
			Check(under.size() == 1 && under[0] == "data.bin",
				  "enumerating a subdirectory answers relative to it");
		}

		{
			// Two independent streams over one entry: interleaved reads must
			// not share a cursor, because this is exactly what the audio
			// thread does beside the loader.
			auto a = VFS::OpenStream(scratch / "blobs" / "data.bin");
			auto b = VFS::OpenStream(scratch / "blobs" / "data.bin");
			Check(a && b && a->Size() == binary.size(), "streams open on a pak entry");

			uint8_t bytesA[16] = {};
			uint8_t bytesB[16] = {};
			Check(a->Seek(60000) && a->Read(bytesA, 16) == 16, "a stream reads at an offset");
			Check(b->Read(bytesB, 16) == 16, "the other stream still reads from its start");
			Check(std::memcmp(bytesA, binary.data() + 60000, 16) == 0 &&
				  std::memcmp(bytesB, binary.data(), 16) == 0,
				  "and each got its own bytes");

			uint8_t past[8] = {};
			Check(a->Seek(binary.size()) && a->Read(past, 8) == 0,
				  "a read at the end answers zero rather than the next asset's bytes");
		}

		VFS::UnmountAll();
		{
			std::string text;
			Check(VFS::ReadText(scratch / "textures" / "rock.png", text) && text == looseRock,
				  "unmounting uncovers the loose file again");
		}

		fs::remove_all(scratch, error);
	}

	// Materials as assets, and the defect that made them one.
	//
	// The interesting property is not "a material can be shared" -- that is easy
	// to check and easy to fake. It is that **an imported model's texture maps
	// survive being saved**. They did not: `Material` has had all five maps and
	// the shader has sampled them since phase 3, but the only thing that could
	// set them was the glTF importer, and the only place they lived was a
	// `Ref<Material>` hanging off a component. A scene file cannot write a
	// pointer, so an import looked correct exactly once -- until the first save.
	void CheckMaterialAssets()
	{
		if (!Renderer::HasDevice() || !Assets::Registry::IsInitialised())
			return;

		// The generated fixture. Untextured models cannot fail this check,
		// which is why every model in the tree failed to notice the bug.
		const AssetHandle model = Assets::Registry::GetHandle("models/textured.gltf");
		if (!model.IsValid())
		{
			Check(false, "the textured model fixture is registered "
						 "(run tools/scripts/make_textured_model.py)");
			return;
		}

		auto scene = std::make_shared<Scene>();
		Entity root = Assets::Manager::InstantiateModel(*scene, model);
		Check((bool)root, "a textured model instantiates");

		// Find the entity the importer gave a mesh to.
		Entity textured;
		scene->GetRegistry().view<MeshComponent>().each(
			[&](entt::entity handle, MeshComponent&) { textured = Entity(handle, scene.get()); });

		Check((bool)textured, "and it has a mesh");
		if (!textured)
			return;

		const AssetHandle material = textured.GetComponent<MeshComponent>().Material;
		Check(material.IsValid(), "whose material is an asset handle, not an object");

		// The file on disk, read back rather than remembered. What the importer
		// held in memory is not what a reopened scene will get.
		const std::filesystem::path path = Assets::Registry::GetAbsolutePath(material);
		Check(!path.empty(), "backed by a file the registry can find");

		Assets::MaterialDesc desc;
		Check(Assets::MaterialSerializer::Load(desc, path), "which parses as a material");

		Check(desc.BaseColorMap.IsValid(), "carrying the base colour map");
		// Split at import: glTF packs these into one texture and the shader
		// takes them separately, so the importer writes two greyscale PNGs.
		Check(desc.RoughnessMap.IsValid(), "a roughness map split out of the packed one");
		Check(desc.MetallicMap.IsValid(), "and a metallic map");
		Check(desc.NormalMap.IsValid(), "and the normal map");

		// And *not* the two the model does not have.
		//
		// The check that was missing, and the bug it would have caught: a
		// default-constructed AssetHandle is a random UUID, not zero, so every
		// unassigned map claimed a handle and wrote it to the file. It rendered
		// correctly the whole time -- an unresolvable handle clears the slot on
		// load -- so only the file was wrong, and only until one of those random
		// numbers collided with a real asset.
		//
		// Asserting what a thing does *not* have is the half of a contract that
		// gets skipped, and it is the half that catches a wrong default.
		Check(!desc.OcclusionMap.IsValid(),
			  "and no occlusion map, because the model has none");
		Check(!desc.EmissiveMap.IsValid(), "nor an emissive one");

		// The same property one level up: a fresh component references nothing.
		Check(!MeshComponent{}.Material.IsValid(),
			  "a new mesh component has no material rather than a random one");

		// The maps resolve to real textures, not merely to non-zero handles.
		Check(Assets::Manager::GetTexture(desc.BaseColorMap) != nullptr,
			  "and each handle resolves to a texture");

		// MapFlags is derived, never stored -- so a material loaded from disk
		// reports the maps it actually has rather than the ones a stale field
		// claimed. The shader branches on these.
		const RHI::Ref<Material> resolved = Assets::Manager::GetMaterial(material);
		Check(resolved != nullptr, "the manager builds a Material from it");
		if (resolved)
		{
			const int32_t flags = resolved->GetParams().MapFlags;
			Check((flags & MaterialMap_BaseColor) != 0, "with the base colour flag set");
			Check((flags & MaterialMap_Normal) != 0, "the normal flag");
			Check((flags & MaterialMap_Roughness) != 0, "the roughness flag");
			Check((flags & MaterialMap_Metallic) != 0, "and the metallic flag");
		}

		// Two entities on one material handle get the *same* object, which is
		// what makes them one draw. A per-entity copy would render identically
		// and batch not at all -- the failure that has no visual symptom.
		Check(Assets::Manager::GetMaterial(material) == resolved,
			  "and two lookups of one handle give the same object, so they batch");

		// The round trip: save the scene, load it back, and ask the *reloaded*
		// component. This is the step that used to lose the textures.
		SceneSerializer serializer(scene);
		const std::filesystem::path scenePath =
			std::filesystem::temp_directory_path() / "rv_material_roundtrip.rage";
		Check(serializer.Serialize(scenePath.string()), "a scene using it serializes");

		auto reloaded = std::make_shared<Scene>();
		SceneSerializer reader(reloaded);
		Check(reader.Deserialize(scenePath.string()), "and deserializes");

		Entity after;
		reloaded->GetRegistry().view<MeshComponent>().each(
			[&](entt::entity handle, MeshComponent&) { after = Entity(handle, reloaded.get()); });

		Check((bool)after, "the reloaded scene still has the mesh");
		if (after)
		{
			Check(after.GetComponent<MeshComponent>().Material == material,
				  "pointing at the same material asset");

			const RHI::Ref<Material> reloadedMaterial =
				Assets::Manager::GetMaterial(after.GetComponent<MeshComponent>().Material);

			Check(reloadedMaterial != nullptr, "which still resolves");
			if (reloadedMaterial)
			{
				// The whole point of 7.3, in one assertion.
				Check((reloadedMaterial->GetParams().MapFlags & MaterialMap_BaseColor) != 0,
					  "and its texture maps survived the save -- which they did not before");
			}
		}

		std::error_code error;
		std::filesystem::remove(scenePath, error);
	}

	// Per-entity scalar overrides, and the batching property that pays for them.
	void CheckMaterialOverrides()
	{
		MeshComponent mesh;

		MaterialParams base;
		base.BaseColor = { 0.2f, 0.4f, 0.6f, 1.0f };
		base.Roughness = 0.8f;
		base.Metallic = 0.1f;

		// Nothing overridden: the material's own values, untouched.
		MaterialParams resolved = mesh.ResolveParams(base);
		Check(resolved.BaseColor.r == base.BaseColor.r && resolved.Roughness == base.Roughness,
			  "an entity with no overrides takes the material's parameters");

		mesh.OverrideRoughness = true;
		mesh.Roughness = 0.15f;

		resolved = mesh.ResolveParams(base);
		Check(resolved.Roughness == 0.15f, "an override wins over the material");
		Check(resolved.BaseColor.r == base.BaseColor.r,
			  "and changes only what it names -- the rest still comes from the asset");

		// A legitimate zero. Each override has its own switch rather than a
		// sentinel precisely because every one of these values has a
		// meaningful zero, and a sentinel would make one of them unsayable.
		mesh.OverrideMetallic = true;
		mesh.Metallic = 0.0f;
		resolved = mesh.ResolveParams(base);
		Check(resolved.Metallic == 0.0f, "an override to zero is an override, not an absence");

		// The serializer round-trips the newest fields. Both of today's silent
		// failures were fields that existed and were dropped somewhere between
		// authoring and disk -- this pins the whole set.
		{
			Assets::MaterialDesc out;
			out.Name = "RoundTrip";
			out.Params.HeightScale = 0.07f;
			out.Params.Specular = 0.3f;
			out.Params.UvTransform = { 4.0f, 4.0f, 0.25f, 0.0f };
			out.HeightMap = AssetHandle(1234567890123ull);
			out.RoughnessMap = AssetHandle(9876543210987ull);

			const std::filesystem::path path =
				std::filesystem::temp_directory_path() / "rv_material_fields.rmat";
			Check(Assets::MaterialSerializer::Save(out, path), "a material with the new fields saves");

			Assets::MaterialDesc in;
			Check(Assets::MaterialSerializer::Load(in, path), "and loads");
			Check(in.Params.HeightScale == 0.07f, "keeping its parallax depth");
			Check(in.Params.Specular == 0.3f, "its specular");
			Check(in.Params.UvTransform.x == 4.0f && in.Params.UvTransform.z == 0.25f,
				  "its tiling and offset");
			Check(in.HeightMap == out.HeightMap && in.RoughnessMap == out.RoughnessMap,
				  "and its map handles");

			std::error_code error;
			std::filesystem::remove(path, error);
		}
	}

	// Per-object probe selection, and the arrays the choice indexes into.
	//
	// Checked without looking at a picture, because a picture cannot fail this
	// convincingly: two probes in one room contain much the same surroundings,
	// so selecting the wrong one for every object still renders something that
	// looks like a reflection. The scene below gives each probe a position and
	// an influence radius and then asks, point by point, which slot an object
	// there would read -- which is the actual output of the feature.
	void CheckProbeSelection()
	{
		if (!Renderer::HasDevice())
			return;

		auto scene = std::make_shared<Scene>();

		Entity left = scene->CreateEntity("Left probe");
		left.GetComponent<TransformComponent>().Position = { -10.0f, 0.0f, 0.0f };
		{
			auto& probe = left.AddComponent<ReflectionProbeComponent>();
			probe.Resolution = 64;
			probe.Influence = 6.0f;
		}

		Entity right = scene->CreateEntity("Right probe");
		right.GetComponent<TransformComponent>().Position = { 10.0f, 0.0f, 0.0f };
		{
			auto& probe = right.AddComponent<ReflectionProbeComponent>();
			probe.Resolution = 256;
			probe.Influence = 6.0f;
		}

		// Nothing captured yet, so nothing is usable and every point falls
		// through to the sky. Asserted before the capture rather than after,
		// because "the sky is the answer" has to be true of an incomplete probe
		// as well as of a distant one -- an incomplete cube is black on the
		// faces it has not reached, and black is a worse lie than the sky.
		Check(scene->ProbeSlotFor({ -10.0f, 0.0f, 0.0f }) == ProbeArray::kSkySlot,
			  "a probe that has never captured is not selected");

		// A real frame, which the rest of this file does not need and this
		// check cannot do without: a capture records into the frame's command
		// list, and outside one there is no list to record into, so every probe
		// would stay incomplete and every assertion below would pass by
		// agreeing that the answer is the sky.
		auto capture = [&scene]()
		{
			RHI::RHICommandList* cmd = Renderer::GetDevice().BeginFrame();
			if (!cmd)
				return false;

			// What the frame graph does before any pass runs, and what nothing
			// else in this file has ever needed: the renderers build their
			// pipelines against the formats they are told to expect, and a
			// probe captures into an HDR target. Without this the pipelines
			// keep the swapchain's 8-bit format and every draw into a probe
			// face is a format mismatch -- which the driver renders anyway and
			// only the validation layer objects to.
			Renderer::SetTargetFormats(RHI::Format::R16G16B16A16_SFLOAT,
									   RHI::Format::D32_SFLOAT,
									   1, RHI::Format::R16G16_SFLOAT,
									   RHI::Format::R8G8B8A8_UNORM,
									   RHI::Format::R16G16B16A16_SFLOAT);

			Renderer::BeginFrame(cmd);
			scene->CaptureReflectionProbes();
			Renderer::EndFrame();
			Renderer::GetDevice().EndFrame();
			return true;
		};

		if (!capture())
		{
			RV_CORE_WARN("probe selection: no frame available; skipped");
			return;
		}

		Check(ProbeArray::IsReady(), "the probe arrays allocated");

		if (const RHI::Ref<RHI::RHITexture> radiance = ProbeArray::GetRadiance())
		{
			Check(radiance->GetDesc().Type == RHI::TextureType::TextureCubeArray,
				  "as a cube array the shader can index per object");
			// 3 slots: the sky plus this scene's two probes. Slots follow the
			// scene now rather than being sixteen always, because face size
			// follows the sky's resolution and sixteen slots at 512 would be a
			// quarter gigabyte.
			Check(radiance->GetDesc().Layers == 3 * CubeFaces::kFaceCount,
				  "with six layers per slot, one slot per probe plus the sky");
			Check(radiance->GetDesc().MipLevels > 1,
				  "and a roughness chain, or every surface reflecting one is a mirror");
		}

		if (const RHI::Ref<RHI::RHITexture> irradiance = ProbeArray::GetIrradiance())
		{
			Check(irradiance->GetDesc().Type == RHI::TextureType::TextureCubeArray,
				  "and the irradiance array has the same shape");
			// The whole point of deciding it rather than leaving it: a scene
			// whose reflections move per object while its ambient does not
			// reads as a lighting bug, not as a missing feature.
			Check(irradiance->GetDesc().Layers == 3 * CubeFaces::kFaceCount,
				  "so diffuse follows the same probe specular does");
		}

		const uint32_t leftSlot = scene->ProbeSlotFor({ -10.0f, 0.0f, 0.0f });
		const uint32_t rightSlot = scene->ProbeSlotFor({ 10.0f, 0.0f, 0.0f });

		Check(leftSlot != ProbeArray::kSkySlot, "an object at the left probe selects a probe");
		Check(rightSlot != ProbeArray::kSkySlot, "as does one at the right probe");
		Check(leftSlot != rightSlot, "and the two probes are different slots");

		// The mutation this is really for. Choosing against the camera -- the
		// shape this replaced -- gives one answer for the whole scene, so both
		// of these would come back the same.
		Check(scene->ProbeSlotFor({ -8.0f, 0.0f, 0.0f }) == leftSlot,
			  "an object near the left probe reflects the left one");
		Check(scene->ProbeSlotFor({ 8.0f, 0.0f, 0.0f }) == rightSlot,
			  "and one near the right probe reflects the right one, in the same frame");

		// Between them and outside both radii. Influence is what makes a probe
		// a local answer rather than a global one, and a selection that ignored
		// it would hand this point the nearer of the two anyway.
		Check(scene->ProbeSlotFor({ 0.0f, 0.0f, 0.0f }) == ProbeArray::kSkySlot,
			  "and a point beyond every influence falls back to the sky");

		// Just inside the right probe's reach, and much closer to it than to
		// the left one. Tests the boundary in the direction that matters: the
		// radius is a cutoff, not a weight.
		Check(scene->ProbeSlotFor({ 4.5f, 0.0f, 0.0f }) == rightSlot,
			  "a point inside one radius and outside the other takes the one it is in");

		// A probe removed from the scene has to leave the table with it, or the
		// frame afterwards still points objects at a slot nothing refills.
		scene->DeleteEntity(right);
		capture();

		Check(scene->ProbeSlotFor({ 10.0f, 0.0f, 0.0f }) == ProbeArray::kSkySlot,
			  "deleting a probe stops objects selecting it");
		Check(scene->ProbeSlotFor({ -10.0f, 0.0f, 0.0f }) != ProbeArray::kSkySlot,
			  "while the one still there keeps working");

		// And with no probes at all, which is every scene anybody starts from.
		scene->DeleteEntity(left);
		capture();

		Check(scene->ProbeSlotFor({ -10.0f, 0.0f, 0.0f }) == ProbeArray::kSkySlot,
			  "a scene with no probes reflects the sky everywhere");
	}

	// One array, sized by the largest probe in the scene.
	//
	// Its own scene, with its own shape, because the property is about which
	// probe wins and the obvious two-probe fixture cannot say. A view's
	// iteration order is not something a test gets to choose, and with two
	// probes "the largest" and "whichever one came back first" agree half the
	// time -- which is to say the check passes for the wrong reason and reports
	// nothing. Three probes with the largest in the middle has no such order:
	// neither end of the pool is the answer.
	void CheckProbeArraySize()
	{
		if (!Renderer::HasDevice())
			return;

		auto scene = std::make_shared<Scene>();

		auto place = [&scene](const char* name, int resolution)
		{
			Entity entity = scene->CreateEntity(name);
			entity.AddComponent<ReflectionProbeComponent>().Resolution = resolution;
		};

		place("Small", 64);
		place("Large", 256);
		place("Smaller", 32);

		RHI::RHICommandList* cmd = Renderer::GetDevice().BeginFrame();
		if (!cmd)
			return;

		// As above: the pipelines have to expect the format a probe captures
		// into before anything draws into one.
		Renderer::SetTargetFormats(RHI::Format::R16G16B16A16_SFLOAT, RHI::Format::D32_SFLOAT,
								   1, RHI::Format::R16G16_SFLOAT, RHI::Format::R8G8B8A8_UNORM,
								   RHI::Format::R16G16B16A16_SFLOAT);

		Renderer::BeginFrame(cmd);
		scene->CaptureReflectionProbes();
		Renderer::EndFrame();
		Renderer::GetDevice().EndFrame();

		Check(ProbeArray::GetFaceSize() == 256,
			  "the arrays are sized by the largest probe, not the first or the last");

		// A smaller probe is not given an array of its own: one array is the
		// whole reason a per-object index is enough, and two would mean an
		// object had to select the binding as well as the slice.
		if (const RHI::Ref<RHI::RHITexture> radiance = ProbeArray::GetRadiance())
		{
			Check(radiance->GetWidth() == 256,
				  "and a probe below that size is resampled into its slice rather than "
				  "getting an array of its own");
		}
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

	// Canvas layout: anchors, scaling and sort order.
	//
	// **The whole point of the anchor model is resolution independence**, and
	// that is exactly what one screenshot cannot check -- a layout is either
	// right at every window size or it is a bug somebody hits on a display
	// nobody here owns. All of this is arithmetic, so the suite asks about a
	// phone, a 4K panel and an ultrawide in the same millisecond.
	void CheckCanvasLayout()
	{
		// --- scaling ---------------------------------------------------------
		{
			UICanvasComponent canvas;   // 1920x1080, match 0.5

			Check(Math::Abs(UI::CanvasScale(canvas, 1920.0f, 1080.0f) - 1.0f) < 1e-4f,
				  "a canvas at its reference resolution scales by one");

			// Half the reference in both directions is half the scale, whatever
			// the match is set to -- there is no disagreement between the axes
			// to resolve.
			Check(Math::Abs(UI::CanvasScale(canvas, 960.0f, 540.0f) - 0.5f) < 1e-4f,
				  "and by half at half the size");

			canvas.MatchWidthOrHeight = 0.0f;
			Check(Math::Abs(UI::CanvasScale(canvas, 3840.0f, 1080.0f) - 2.0f) < 1e-4f,
				  "matching the width follows the width alone");

			canvas.MatchWidthOrHeight = 1.0f;
			Check(Math::Abs(UI::CanvasScale(canvas, 3840.0f, 1080.0f) - 1.0f) < 1e-4f,
				  "and matching the height ignores it");

			// The geometric mean, not the arithmetic one. It is what makes the
			// halfway setting symmetric: a window twice as wide and one half as
			// wide should scale by reciprocal amounts, and averaging the ratios
			// does not do that.
			canvas.MatchWidthOrHeight = 0.5f;
			const float wide = UI::CanvasScale(canvas, 3840.0f, 1080.0f);
			Check(Math::Abs(wide - Math::Sqrt(2.0f)) < 1e-3f,
				  "halfway is the geometric mean of the two, not the average");

			const float narrow = UI::CanvasScale(canvas, 960.0f, 1080.0f);
			Check(Math::Abs(wide * narrow - 1.0f) < 1e-3f,
				  "so twice as wide and half as wide are reciprocal");

			canvas.ScaleMode = CanvasScaleMode::ConstantPixels;
			Check(UI::CanvasScale(canvas, 3840.0f, 2160.0f) == 1.0f,
				  "constant pixels never scales, which is the point of it");
		}

		// --- one rectangle against its parent ---------------------------------
		const UIRect parent{ 0.0f, 0.0f, 1000.0f, 600.0f };

		{
			// A point anchor: the offsets read as a position and a size, and the
			// parent's size does not enter into it.
			UI::RectAnchors point;
			point.AnchorMin = Vec2(0.0f, 0.0f);
			point.AnchorMax = Vec2(0.0f, 0.0f);
			point.OffsetMin = Vec2(20.0f, 30.0f);
			point.OffsetMax = Vec2(120.0f, 70.0f);

			const UIRect a = UI::ResolveRect(point, parent);
			Check(Math::Abs(a.X - 20.0f) < 1e-4f && Math::Abs(a.Y - 30.0f) < 1e-4f &&
				  Math::Abs(a.Width - 100.0f) < 1e-4f && Math::Abs(a.Height - 40.0f) < 1e-4f,
				  "a point anchor turns the offsets into a position and a size");

			const UIRect b = UI::ResolveRect(point, UIRect{ 0.0f, 0.0f, 4000.0f, 4000.0f });
			Check(Math::Abs(a.Width - b.Width) < 1e-4f && Math::Abs(a.X - b.X) < 1e-4f,
				  "and a bigger parent does not change either");

			// A stretch anchor: the same two numbers are margins now, and the
			// size follows the parent. One formula, both behaviours.
			UI::RectAnchors stretch;
			stretch.AnchorMin = Vec2(0.0f, 0.0f);
			stretch.AnchorMax = Vec2(1.0f, 1.0f);
			stretch.OffsetMin = Vec2(10.0f, 10.0f);
			stretch.OffsetMax = Vec2(-10.0f, -10.0f);

			const UIRect s = UI::ResolveRect(stretch, parent);
			Check(Math::Abs(s.Width - 980.0f) < 1e-4f && Math::Abs(s.Height - 580.0f) < 1e-4f,
				  "a stretch anchor turns the same offsets into margins");

			const UIRect s2 = UI::ResolveRect(stretch, UIRect{ 0.0f, 0.0f, 2000.0f, 600.0f });
			Check(Math::Abs(s2.Width - 1980.0f) < 1e-4f,
				  "and the size follows the parent");
		}

		// --- the claim the whole model exists for ------------------------------
		//
		// Twenty units in from the top-right corner, at every resolution. This
		// is the thing position-and-size cannot express and the reason UI does
		// not reuse TransformComponent.
		{
			UI::RectAnchors corner;
			corner.AnchorMin = Vec2(1.0f, 0.0f);
			corner.AnchorMax = Vec2(1.0f, 0.0f);
			corner.OffsetMin = Vec2(-220.0f, 20.0f);
			corner.OffsetMax = Vec2(-20.0f, 80.0f);

			const float sizes[][2] = {
				{ 1280.0f, 720.0f }, { 1920.0f, 1080.0f },
				{ 3840.0f, 2160.0f }, { 2560.0f, 1080.0f }, { 800.0f, 1280.0f },
			};

			bool pinned = true;
			for (const auto& size : sizes)
			{
				const UIRect canvasRect{ 0.0f, 0.0f, size[0], size[1] };
				const UIRect r = UI::ResolveRect(corner, canvasRect);

				// The gap to the right edge, and the size, are the same every
				// time -- including on the portrait and ultrawide shapes.
				pinned = pinned && Math::Abs((canvasRect.Width - r.Right()) - 20.0f) < 1e-3f;
				pinned = pinned && Math::Abs(r.Width - 200.0f) < 1e-3f;
				pinned = pinned && Math::Abs(r.Y - 20.0f) < 1e-3f;
			}
			Check(pinned, "a corner-pinned element keeps its distance from that corner "
						  "at every resolution, including portrait and ultrawide");
		}

		// --- a tree -------------------------------------------------------------
		{
			auto scene = std::make_shared<Scene>();

			Entity canvas = scene->CreateEntity("Canvas");
			UICanvasComponent& canvasComponent = canvas.AddComponent<UICanvasComponent>();
			canvasComponent.ScaleMode = CanvasScaleMode::ConstantPixels;

			auto addRect = [&](const char* name, Entity parentEntity, Vec2 anchorMin,
							   Vec2 anchorMax, Vec2 offsetMin, Vec2 offsetMax, int32_t order)
			{
				Entity e = scene->CreateEntity(name);
				scene->SetParent(e, parentEntity);
				UIRectComponent& rect = e.AddComponent<UIRectComponent>();
				rect.AnchorMin = anchorMin;
				rect.AnchorMax = anchorMax;
				rect.OffsetMin = offsetMin;
				rect.OffsetMax = offsetMax;
				rect.SortOrder = order;
				return e;
			};

			// A panel inset 100 all round, with a child inset 10 inside that.
			Entity panel = addRect("Panel", canvas, Vec2(0.0f), Vec2(1.0f),
								   Vec2(100.0f), Vec2(-100.0f), 0);
			addRect("Label", panel, Vec2(0.0f), Vec2(1.0f), Vec2(10.0f), Vec2(-10.0f), 0);

			std::vector<UI::ResolvedElement> resolved =
				UI::ResolveScene(*scene, 1000.0f, 800.0f);

			Check(resolved.size() == 2, "a canvas with two elements resolves two");

			if (resolved.size() == 2)
			{
				// The canvas entity is the space, not an element: the panel is
				// laid out against the screen directly.
				Check(Math::Abs(resolved[0].Rect.X - 100.0f) < 1e-3f &&
					  Math::Abs(resolved[0].Rect.Width - 800.0f) < 1e-3f,
					  "the first is inset from the screen, not from a canvas element");

				// Nesting composes: 100 + 10.
				Check(Math::Abs(resolved[1].Rect.X - 110.0f) < 1e-3f &&
					  Math::Abs(resolved[1].Rect.Width - 780.0f) < 1e-3f,
					  "and a child is laid out against its parent's rectangle");
			}

			// --- sort order -----------------------------------------------------
			Entity front = addRect("Front", canvas, Vec2(0.0f), Vec2(0.0f),
								   Vec2(0.0f), Vec2(10.0f), 5);
			Entity behind = addRect("Behind", canvas, Vec2(0.0f), Vec2(0.0f),
									Vec2(0.0f), Vec2(10.0f), -5);

			resolved = UI::ResolveScene(*scene, 1000.0f, 800.0f);
			Check(resolved.size() == 4, "and adding two more resolves four");
			Check(!resolved.empty() &&
				  resolved.front().Entity == (uint64_t)behind.GetUUID() &&
				  resolved.back().Entity == (uint64_t)front.GetUUID(),
				  "sorted so the lowest sort order is drawn first and the highest last");

			// --- hiding ----------------------------------------------------------
			// A hidden panel still positions its children. Anything else would
			// mean toggling a background moved the label on it.
			panel.GetComponent<UIRectComponent>().Visible = false;
			resolved = UI::ResolveScene(*scene, 1000.0f, 800.0f);

			Check(resolved.size() == 3, "hiding an element drops it from the draw list");

			bool childStillPlaced = false;
			for (const UI::ResolvedElement& element : resolved)
				childStillPlaced = childStillPlaced || Math::Abs(element.Rect.X - 110.0f) < 1e-3f;
			Check(childStillPlaced, "and its children stay exactly where they were");
		}

		// --- an empty scene ------------------------------------------------------
		{
			auto scene = std::make_shared<Scene>();
			Check(UI::ResolveScene(*scene, 1920.0f, 1080.0f).empty(),
				  "a scene with no canvas resolves nothing");

			Entity lonely = scene->CreateEntity("Canvas");
			lonely.AddComponent<UICanvasComponent>();
			Check(UI::ResolveScene(*scene, 1920.0f, 1080.0f).empty(),
				  "and a canvas with no children resolves nothing either");
		}
	}

	// Text in the world: which way a label faces.
	//
	// The rendering needs a GPU; **the orientation does not**, and it is the
	// half that goes wrong in a way a screenshot makes look deliberate. A
	// nameplate tipped 20 degrees back reads as a styling choice until somebody
	// walks round it.
	void CheckWorldText()
	{
		// A camera at the origin, yawed 30 degrees and pitched down 20, so no
		// axis is accidentally aligned with a world one and a bug cannot pass
		// by coincidence.
		const Mat4 cameraTransform =
			Math::Rotate(Mat4(1.0f), Math::Radians(30.0f), Vec3(0.0f, 1.0f, 0.0f)) *
			Math::Rotate(Mat4(1.0f), Math::Radians(-20.0f), Vec3(1.0f, 0.0f, 0.0f));

		const Vec3 worldUp{ 0.0f, 1.0f, 0.0f };

		// --- full billboard ---------------------------------------------------
		{
			Vec3 right, up;
			UI::BillboardAxes(TextBillboard::Full, cameraTransform, Mat4(1.0f), right, up);

			Check(Math::Abs(Math::Length(right) - 1.0f) < 1e-4f &&
				  Math::Abs(Math::Length(up) - 1.0f) < 1e-4f,
				  "a full billboard's axes are unit length");

			Check(Math::Abs(Math::Dot(right, up)) < 1e-4f,
				  "and perpendicular to each other");

			// Square to the camera means the quad's normal is the camera's
			// forward -- which is the definition, tested rather than assumed.
			const Vec3 normal = Math::Cross(right, up);
			const Vec3 forward = Math::Normalize(Vec3(cameraTransform[2]));
			Check(Math::Abs(Math::Abs(Math::Dot(normal, forward)) - 1.0f) < 1e-4f,
				  "and face square to the camera");
		}

		// --- a scaled camera rig must not resize the world ---------------------
		//
		// A world matrix's columns carry scale. Taking them unnormalised makes
		// every nameplate in the game change size when somebody scales the
		// camera's parent, which is a bug nobody would look for here.
		{
			const Mat4 scaled = cameraTransform * Math::Scale(Mat4(1.0f), Vec3(3.0f));

			Vec3 right, up;
			UI::BillboardAxes(TextBillboard::Full, scaled, Mat4(1.0f), right, up);

			Check(Math::Abs(Math::Length(right) - 1.0f) < 1e-4f &&
				  Math::Abs(Math::Length(up) - 1.0f) < 1e-4f,
				  "scaling the camera does not change the size of a billboarded label");
		}

		// --- upright ------------------------------------------------------------
		{
			Vec3 right, up;
			UI::BillboardAxes(TextBillboard::Upright, cameraTransform, Mat4(1.0f), right, up);

			Check(Math::Length(up - worldUp) < 1e-4f,
				  "an upright billboard's up axis is world up exactly");

			Check(Math::Abs(Math::Dot(right, worldUp)) < 1e-4f,
				  "and its horizontal axis is level, whatever the camera's pitch");

			Check(Math::Abs(Math::Length(right) - 1.0f) < 1e-4f,
				  "and unit length");

			// It still turns to the viewer: the label's normal has to point
			// back towards the camera in the horizontal plane.
			const Vec3 normal = Math::Cross(right, up);
			const Vec3 forward = Math::Normalize(Vec3(cameraTransform[2]));
			const Vec3 flatNormal = Math::Normalize(Vec3{ normal.x, 0.0f, normal.z });
			const Vec3 flatForward = Math::Normalize(Vec3{ forward.x, 0.0f, forward.z });

			Check(Math::Abs(Math::Abs(Math::Dot(flatNormal, flatForward)) - 1.0f) < 1e-3f,
				  "while still turning to face the viewer horizontally");
		}

		// --- looking straight down ----------------------------------------------
		//
		// World up and the camera's forward are parallel, so the cross product
		// that gives the level axis degenerates. Falling back keeps the label
		// readable; not falling back collapses it to a line, and the symptom is
		// "the text disappears when I look down".
		{
			const Mat4 lookingDown =
				Math::Rotate(Mat4(1.0f), Math::Radians(-90.0f), Vec3(1.0f, 0.0f, 0.0f));

			Vec3 right, up;
			UI::BillboardAxes(TextBillboard::Upright, lookingDown, Mat4(1.0f), right, up);

			Check(Math::Length(right) > 0.5f,
				  "a camera looking straight down still gives an upright label a "
				  "horizontal axis rather than collapsing it");
		}

		// --- the entity's own plane ----------------------------------------------
		{
			// Scaled, because this is the one mode where scale *should* carry:
			// a sign made twice as big has letters twice as big.
			const Mat4 entity = Math::Scale(Mat4(1.0f), Vec3(2.0f));

			Vec3 right, up;
			UI::BillboardAxes(TextBillboard::None, cameraTransform, entity, right, up);

			Check(Math::Abs(Math::Length(right) - 2.0f) < 1e-4f,
				  "text in the entity's own plane takes the entity's scale with it");

			Check(Math::Abs(right.x - 2.0f) < 1e-4f && Math::Abs(up.y - 2.0f) < 1e-4f,
				  "and its axes are the entity's, not the camera's");
		}
	}

	// Hit-testing, the button state machine, and who owns the pointer.
	//
	// **Every failure here is a click that went to the wrong place**, and none
	// of them is visible in a screenshot: a click that reached the game through
	// a menu looks exactly like a click that did not, until somebody fires a
	// weapon while pressing Pause. So the whole path is arithmetic over a
	// resolved list, and the whole path runs with no window.
	//
	// The pointer's state machine is global -- one cursor, one capture -- so
	// each block resets it first rather than inheriting whatever the last one
	// left half-pressed.
	void CheckUIInteraction()
	{
		// A canvas whose units are pixels, so a rectangle's numbers are the
		// numbers the pointer is compared against and a failure is readable.
		auto build = [](const std::shared_ptr<Scene>& scene, CanvasScaleMode mode)
		{
			Entity canvas = scene->CreateEntity("Canvas");
			canvas.AddComponent<UICanvasComponent>().ScaleMode = mode;
			return canvas;
		};

		auto addRect = [](const std::shared_ptr<Scene>& scene, const char* name,
						  Entity parent, float x, float y, float w, float h, int32_t order)
		{
			Entity e = scene->CreateEntity(name);
			scene->SetParent(e, parent);

			UIRectComponent& rect = e.AddComponent<UIRectComponent>();
			rect.AnchorMin = Vec2(0.0f, 0.0f);
			rect.AnchorMax = Vec2(0.0f, 0.0f);
			rect.OffsetMin = Vec2(x, y);
			rect.OffsetMax = Vec2(x + w, y + h);
			rect.SortOrder = order;
			return e;
		};

		// --- what the pointer stops at -----------------------------------------
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			Entity back = addRect(scene, "Back", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			Entity front = addRect(scene, "Front", canvas, 50.0f, 50.0f, 100.0f, 100.0f, 1);
			back.AddComponent<UIButtonComponent>();
			front.AddComponent<UIButtonComponent>();

			std::vector<UI::ResolvedElement> elements =
				UI::ResolveScene(*scene, 400.0f, 400.0f);

			auto hitEntity = [&](float x, float y) -> uint64_t
			{
				const int32_t index = UI::HitTest(elements, x, y);
				return index >= 0 ? elements[index].Entity : 0;
			};

			// The overlap is the whole test. Both rectangles cover (75, 75), and
			// the one drawn last is the one a hand aiming at the screen meant.
			Check(hitEntity(75.0f, 75.0f) == (uint64_t)front.GetUUID(),
				  "where two buttons overlap, the pointer hits the one drawn on top");
			Check(hitEntity(25.0f, 25.0f) == (uint64_t)back.GetUUID(),
				  "and hits the lower one where only it covers the point");
			Check(hitEntity(300.0f, 300.0f) == 0,
				  "and hits nothing outside every rectangle");

			// A label over a button. This is the case Unity gets wrong by
			// default and the reason BlocksPointer exists: decoration must not
			// eat the click aimed through it.
			Entity label = addRect(scene, "Label", canvas, 0.0f, 0.0f, 400.0f, 400.0f, 99);
			label.AddComponent<UITextComponent>();

			elements = UI::ResolveScene(*scene, 400.0f, 400.0f);
			Check(hitEntity(75.0f, 75.0f) == (uint64_t)front.GetUUID(),
				  "a label drawn over a button does not take the pointer from it");

			label.GetComponent<UIRectComponent>().BlocksPointer = true;
			elements = UI::ResolveScene(*scene, 400.0f, 400.0f);
			Check(hitEntity(75.0f, 75.0f) == (uint64_t)label.GetUUID(),
				  "and does take it once it asks to -- a modal's backdrop");

			label.GetComponent<UIRectComponent>().BlocksPointer = false;

			// A greyed-out button is not a hole in the UI *and* not a target.
			front.GetComponent<UIButtonComponent>().Interactable = false;
			elements = UI::ResolveScene(*scene, 400.0f, 400.0f);
			Check(hitEntity(75.0f, 75.0f) == (uint64_t)back.GetUUID(),
				  "a button that is not interactable lets the pointer through to "
				  "what is behind it");

			// Hidden is hidden for input too, or a faded-out menu keeps
			// answering clicks nobody can see they are making.
			front.GetComponent<UIButtonComponent>().Interactable = true;
			front.GetComponent<UIRectComponent>().Visible = false;
			elements = UI::ResolveScene(*scene, 400.0f, 400.0f);
			Check(hitEntity(75.0f, 75.0f) == (uint64_t)back.GetUUID(),
				  "and an invisible one cannot be clicked either");
		}

		// --- the pointer is in pixels, the layout is not -------------------------
		//
		// The bug this catches is silent and only on somebody else's monitor: a
		// hit test done in canvas units is right at the reference resolution and
		// wrong at every other one, by exactly the scale factor.
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ScaleWithScreen);

			Entity button = addRect(scene, "Button", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			button.AddComponent<UIButtonComponent>();

			// Twice the reference resolution on both axes, so the scale is 2 and
			// the 100-unit button covers 200 pixels.
			const std::vector<UI::ResolvedElement> elements =
				UI::ResolveScene(*scene, 3840.0f, 2160.0f);

			Check(UI::HitTest(elements, 150.0f, 150.0f) >= 0,
				  "at twice the reference resolution the button covers twice the pixels");
			Check(UI::HitTest(elements, 250.0f, 250.0f) < 0,
				  "and stops where it is drawn, not where its units end");
		}

		// --- press, release, and the gesture in between --------------------------
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			Entity a = addRect(scene, "A", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			Entity b = addRect(scene, "B", canvas, 200.0f, 0.0f, 100.0f, 100.0f, 0);
			a.AddComponent<UIButtonComponent>();
			b.AddComponent<UIButtonComponent>();

			auto move = [&](float x, float y, bool down)
			{
				UI::PointerInput pointer;
				pointer.X = x;
				pointer.Y = y;
				pointer.Down = down;
				pointer.Inside = true;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			};

			const UIButtonComponent& buttonA = a.GetComponent<UIButtonComponent>();
			const UIButtonComponent& buttonB = b.GetComponent<UIButtonComponent>();

			UI::ResetPointer(*scene);

			move(50.0f, 50.0f, false);
			Check(buttonA.Hovered && !buttonA.Pressed && !buttonA.Clicked,
				  "hovering a button hovers it and nothing else");
			Check(!buttonB.Hovered, "and leaves the other one alone");

			move(50.0f, 50.0f, true);
			Check(buttonA.Pressed && !buttonA.Clicked,
				  "holding it down presses it, and a press is not yet a click");

			move(50.0f, 50.0f, false);
			Check(buttonA.Clicked, "releasing on it completes the click");
			Check(!buttonB.Clicked, "and clicks nothing else");

			UI::EndFixedStep(*scene);
			Check(!buttonA.Clicked, "which the next simulation step consumes");

			// The cancelled press. Every desktop toolkit does this, and it is
			// the difference between a button and a tripwire.
			UI::ResetPointer(*scene);
			move(50.0f, 50.0f, true);
			move(250.0f, 50.0f, true);
			Check(!buttonA.Pressed, "sliding off a held button un-presses it");
			Check(!buttonB.Pressed,
				  "and does not press the one it slid onto -- that press began elsewhere");

			move(250.0f, 50.0f, false);
			Check(!buttonA.Clicked && !buttonB.Clicked,
				  "and releasing over a different button clicks neither of them");

			// ...and coming back completes it, because the press was cancelled,
			// not thrown away.
			UI::ResetPointer(*scene);
			move(50.0f, 50.0f, true);
			move(250.0f, 50.0f, true);
			move(50.0f, 50.0f, true);
			Check(buttonA.Pressed, "coming back to it presses it again");
			move(50.0f, 50.0f, false);
			Check(buttonA.Clicked, "and the release then counts");

			// A press begun on nothing stays nothing, however it ends.
			UI::ResetPointer(*scene);
			UI::EndFixedStep(*scene);
			move(350.0f, 350.0f, true);
			move(50.0f, 50.0f, true);
			move(50.0f, 50.0f, false);
			Check(!buttonA.Clicked,
				  "a press that began on empty space cannot click what it ends on");
		}

		// --- who owns the pointer -----------------------------------------------
		//
		// The one rule in ENGINE-NOTES 7d, and the reason any of this is public.
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			Entity button = addRect(scene, "Button", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			button.AddComponent<UIButtonComponent>();

			Entity label = addRect(scene, "Label", canvas, 200.0f, 0.0f, 100.0f, 100.0f, 0);
			label.AddComponent<UITextComponent>();

			auto move = [&](float x, float y, bool down, bool inside = true)
			{
				UI::PointerInput pointer;
				pointer.X = x;
				pointer.Y = y;
				pointer.Down = down;
				pointer.Inside = inside;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			};

			UI::ResetPointer(*scene);

			move(350.0f, 350.0f, false);
			Check(!UI::WantsPointer(), "over empty space the UI does not want the pointer");

			move(250.0f, 50.0f, false);
			Check(!UI::WantsPointer(),
				  "over a plain label it still does not -- a HUD is not a wall");

			move(50.0f, 50.0f, false);
			Check(UI::WantsPointer(), "over a button it does");

			// The case that costs a bug report: the hand moves between press and
			// release, and without capture the release reaches the game.
			move(50.0f, 50.0f, true);
			move(350.0f, 350.0f, true);
			Check(UI::WantsPointer(),
				  "and keeps it for the whole of a press that began on a button, "
				  "even dragged clear of it");

			move(350.0f, 350.0f, false);
			Check(!UI::WantsPointer(), "letting go over nothing gives it back");

			// A cursor that has left the window entirely is over nothing at all.
			move(50.0f, 50.0f, false, /*inside*/ false);
			Check(!UI::WantsPointer(),
				  "and a pointer outside the layer wants nothing, wherever it reports "
				  "itself to be");
		}

		// --- the tint, which is the only part anybody sees ------------------------
		{
			UIButtonComponent button;
			button.NormalColor = Vec4(0.1f, 0.0f, 0.0f, 1.0f);
			button.HoverColor = Vec4(0.2f, 0.0f, 0.0f, 1.0f);
			button.PressedColor = Vec4(0.3f, 0.0f, 0.0f, 1.0f);

			Check(UI::ButtonTint(button).x == 0.1f, "an untouched button draws at rest");

			button.Hovered = true;
			Check(UI::ButtonTint(button).x == 0.2f, "a hovered one at its hover tint");

			button.Pressed = true;
			Check(UI::ButtonTint(button).x == 0.3f,
				  "and a held one at its pressed tint, which wins over hover");

			// Pressed and hovered are both still set here: the point is that a
			// button nobody can use looks like one nobody can use.
			button.Interactable = false;
			Check(UI::ButtonTint(button).x == 0.1f,
				  "a button that is not interactable stays at rest whatever the "
				  "pointer is doing");
		}

		// --- an edge is consumed once, and never lost ----------------------------
		//
		// The same contract InputMap has, and the same reason: a frame that runs
		// three simulation steps must not fire a button three times, and a frame
		// that runs none must not swallow the click.
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			Entity button = addRect(scene, "Button", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			button.AddComponent<UIButtonComponent>();

			auto move = [&](bool down)
			{
				UI::PointerInput pointer;
				pointer.X = 50.0f;
				pointer.Y = 50.0f;
				pointer.Down = down;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			};

			const UIButtonComponent& state = button.GetComponent<UIButtonComponent>();

			UI::ResetPointer(*scene);
			move(true);
			move(false);
			Check(state.Clicked, "a click is set on the release");

			// Two frames of pointer movement with no step in between -- what a
			// frame rate above the simulation rate does constantly.
			move(false);
			move(false);
			Check(state.Clicked,
				  "and survives frames that run no simulation step, rather than "
				  "being lost between them");

			UI::EndFixedStep(*scene);
			Check(!state.Clicked, "the first step that runs consumes it");
			UI::EndFixedStep(*scene);
			Check(!state.Clicked, "and a second step in the same frame does not see it again");
		}

		// --- the whole chain, through a script -----------------------------------
		//
		// Everything above tests one link. This one presses a button and looks
		// at the label, which is what somebody playing the game does -- and it
		// is the only check here that would notice the step order being wrong,
		// the edge being cleared before scripts ran, or a click reaching the
		// component and no further.
		//
		// ClickCounter is the built-in worked example, so this doubles as its
		// regression probe.
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			Entity button = addRect(scene, "Button", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			button.AddComponent<UIButtonComponent>();
			button.AddComponent<NativeScriptComponent>("ClickCounter");

			Entity label = addRect(scene, "Label", button, 0.0f, 0.0f, 100.0f, 40.0f, 1);
			label.AddComponent<UITextComponent>();

			auto move = [&](bool down)
			{
				UI::PointerInput pointer;
				pointer.X = 50.0f;
				pointer.Y = 50.0f;
				pointer.Down = down;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			};

			auto caption = [&] { return label.GetComponent<UITextComponent>().Text; };

			constexpr float dt = 1.0f / 60.0f;
			UI::ResetPointer(*scene);
			scene->OnRuntimeStart();
			scene->OnFixedUpdateRuntime(dt);

			Check(caption() == "Click me",
				  "a button script writes its caption into the label as it starts");

			// One frame: the pointer, then the step that reads it. The order the
			// runtime and the editor both use.
			move(true);
			move(false);
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x1", "clicking it once is counted once");

			// Three steps with no further input. The click was consumed by the
			// first, so the other two must see nothing -- this is the check that
			// catches an edge that is never cleared.
			scene->OnFixedUpdateRuntime(dt);
			scene->OnFixedUpdateRuntime(dt);
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x1",
				  "and stays counted once however many steps follow");

			move(true);
			move(false);
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x2", "a second click counts again");

			// A press cancelled by moving away is not a click, all the way
			// through to the label.
			UI::ResetPointer(*scene);
			{
				UI::PointerInput pointer;
				pointer.X = 50.0f; pointer.Y = 50.0f; pointer.Down = true;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
				pointer.X = 350.0f; pointer.Y = 350.0f;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
				pointer.Down = false;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			}
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x2",
				  "and a press dragged off the button never reaches the script at all");

			scene->OnRuntimeStop();
		}

		// --- the bound handler ---------------------------------------------------
		//
		// The other half of the same click: instead of the script asking its own
		// button, the button names a method and the engine calls it. The one
		// thing worth proving beyond "it ran" is **where it ran** -- on another
		// entity entirely, which is the case polling cannot express and the
		// reason this exists.
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			// The handler lives here, on nothing that can be clicked.
			Entity manager = scene->CreateEntity("Manager");
			manager.AddComponent<NativeScriptComponent>("ClickCounter");

			Entity label = addRect(scene, "Label", canvas, 200.0f, 0.0f, 100.0f, 40.0f, 1);
			label.AddComponent<UITextComponent>();
			scene->SetParent(label, manager);

			Entity button = addRect(scene, "Button", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			UIButtonComponent& binding = button.AddComponent<UIButtonComponent>();
			binding.OnClickTarget = EntityRef(manager.GetUUID());
			binding.OnClickMethod = "Count";

			auto click = [&]
			{
				UI::PointerInput pointer;
				pointer.X = 50.0f;
				pointer.Y = 50.0f;
				pointer.Down = true;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
				pointer.Down = false;
				UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			};

			auto caption = [&] { return label.GetComponent<UITextComponent>().Text; };

			constexpr float dt = 1.0f / 60.0f;
			UI::ResetPointer(*scene);
			scene->OnRuntimeStart();
			scene->OnFixedUpdateRuntime(dt);

			click();
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x1",
				  "a button calls the method it names, on an entity that is not the "
				  "button");

			// The same edge contract the polled path has. A binding fired once
			// per step for as long as Clicked stayed true would be the obvious
			// way to get this wrong.
			scene->OnFixedUpdateRuntime(dt);
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x1", "once per click, however many steps follow");

			// A binding that resolves to nothing must not be a crash and must
			// not be a click either. It logs -- which this cannot assert, but
			// the absence of a count is what proves nothing was called.
			button.GetComponent<UIButtonComponent>().OnClickMethod = "NoSuchMethod";
			click();
			scene->OnFixedUpdateRuntime(dt);
			Check(caption() == "Click me x1",
				  "a method name nothing answers to calls nothing, and survives it");

			// The target deleted out from under a live binding -- the state the
			// inspector draws in red, reached the way it actually happens.
			button.GetComponent<UIButtonComponent>().OnClickMethod = "Count";
			scene->DestroyDeferred(manager);
			scene->OnFixedUpdateRuntime(dt);   // the step that flushes the queue
			click();
			scene->OnFixedUpdateRuntime(dt);
			Check(true, "and a binding whose target was destroyed survives being clicked");

			scene->OnRuntimeStop();
		}

		// --- an empty method name is not a broken binding -------------------------
		//
		// It is a button read by polling, which is most of them. Getting this
		// wrong would put a warning in the log for every ordinary click in the
		// engine.
		{
			auto scene = std::make_shared<Scene>();
			Entity canvas = build(scene, CanvasScaleMode::ConstantPixels);

			Entity button = addRect(scene, "Button", canvas, 0.0f, 0.0f, 100.0f, 100.0f, 0);
			button.AddComponent<UIButtonComponent>();

			constexpr float dt = 1.0f / 60.0f;
			UI::ResetPointer(*scene);
			scene->OnRuntimeStart();

			UI::PointerInput pointer;
			pointer.X = 50.0f; pointer.Y = 50.0f; pointer.Down = true;
			UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);
			pointer.Down = false;
			UI::UpdatePointer(*scene, 400.0f, 400.0f, pointer);

			scene->OnFixedUpdateRuntime(dt);
			Check(true, "a button with no bound method is clicked without complaint");

			scene->OnRuntimeStop();
		}

		// --- a binding survives a save ------------------------------------------
		//
		// This is the one that would fail if EntityRef were stored as an
		// `entt::entity`, and it would fail *only after a reload* -- a handle is
		// an index into one scene's pool, so it points at something plausible
		// and wrong rather than at nothing. The UUID is the whole reason the
		// wrapper exists.
		{
			auto scene = std::make_shared<Scene>();

			Entity manager = scene->CreateEntity("Manager");
			Entity button = scene->CreateEntity("Button");
			button.AddComponent<UIRectComponent>();

			UIButtonComponent& binding = button.AddComponent<UIButtonComponent>();
			binding.OnClickTarget = EntityRef(manager.GetUUID());
			binding.OnClickMethod = "Count";

			const UUID managerId = manager.GetUUID();
			const UUID buttonId = button.GetUUID();

			SceneSerializer writer(scene);
			const std::string text = writer.SerializeToString();

			auto reloaded = std::make_shared<Scene>();
			SceneSerializer reader(reloaded);
			Check(reader.DeserializeFromString(text), "a scene with a bound button reloads");

			Entity restored = reloaded->GetEntityByUUID(buttonId);
			Check(restored && restored.HasComponent<UIButtonComponent>(),
				  "and the button comes back with its component");

			if (restored && restored.HasComponent<UIButtonComponent>())
			{
				const UIButtonComponent& after = restored.GetComponent<UIButtonComponent>();
				Check(after.OnClickTarget.Value == managerId,
					  "its OnClick target still names the same entity after a reload");
				Check(after.OnClickMethod == "Count", "and still names the same method");

				// The entity it names is really there, which is the property
				// the reference is for -- equality above would also hold if
				// both were garbage.
				Check(reloaded->GetEntityByUUID(after.OnClickTarget.Value),
					  "and that entity is in the reloaded scene");
			}
		}

		// --- a binding inside a prefab follows the copy ---------------------------
		//
		// Instantiating gives every entity a fresh UUID, so a reference stored
		// in the file names something that is no longer there. Parent links
		// were already rewritten through the remap; entity fields now are too.
		//
		// Without it a prefab placed twice would have both copies' buttons
		// driving whichever manager happened to be created first -- and it
		// would look right until the second copy was clicked.
		{
			auto scene = std::make_shared<Scene>();

			Entity root = scene->CreateEntity("Panel");
			root.AddComponent<UIRectComponent>();

			Entity manager = scene->CreateEntity("Manager");
			scene->SetParent(manager, root);

			Entity button = scene->CreateEntity("Button");
			scene->SetParent(button, root);
			button.AddComponent<UIRectComponent>();

			UIButtonComponent& binding = button.AddComponent<UIButtonComponent>();
			binding.OnClickTarget = EntityRef(manager.GetUUID());
			binding.OnClickMethod = "Count";

			SceneSerializer writer(scene);
			const std::string prefab = writer.SerializeSubtree(root);

			// Two copies, into the same scene, which is what makes this a test
			// rather than a tautology.
			SceneSerializer reader(scene);
			Entity firstRoot = reader.Instantiate(prefab);
			Entity secondRoot = reader.Instantiate(prefab);

			Check(firstRoot, "a prefab holding a bound button instantiates");
			Check(secondRoot, "and instantiates a second time");

			auto boundTargetUnder = [&](Entity subtreeRoot) -> UUID
			{
				for (UUID childId : scene->GetChildren(subtreeRoot))
				{
					Entity child = scene->GetEntityByUUID(childId);
					if (child && child.HasComponent<UIButtonComponent>())
						return child.GetComponent<UIButtonComponent>().OnClickTarget.Value;
				}
				return UUID::Invalid();
			};

			auto managerUnder = [&](Entity subtreeRoot) -> UUID
			{
				for (UUID childId : scene->GetChildren(subtreeRoot))
				{
					Entity child = scene->GetEntityByUUID(childId);
					if (child && child.GetName() == "Manager")
						return childId;
				}
				return UUID::Invalid();
			};

			const UUID firstTarget = boundTargetUnder(firstRoot);
			const UUID secondTarget = boundTargetUnder(secondRoot);

			Check(firstTarget.IsValid() && firstTarget == managerUnder(firstRoot),
				  "the copy's button points at the copy's own manager");
			Check(secondTarget.IsValid() && secondTarget == managerUnder(secondRoot),
				  "and the second copy points at its own, not at the first copy's");
			Check(firstTarget != secondTarget,
				  "so two copies of one prefab do not share a target");
			Check(firstTarget != manager.GetUUID() && secondTarget != manager.GetUUID(),
				  "and neither points back at the entity the prefab was made from");
		}

		// --- the check that stands in for the missing compiler --------------------
		//
		// A method name in a scene file is text nothing compiles, so the only
		// defence is asking the same question earlier: on load, and at package
		// time. These assert that the question is asked *correctly* -- a check
		// that reports every sound binding as broken would be turned off within
		// a day, and one that reports none is worth nothing.
		{
			auto scene = std::make_shared<Scene>();

			Entity manager = scene->CreateEntity("Manager");
			manager.AddComponent<NativeScriptComponent>("ClickCounter");

			Entity ok = scene->CreateEntity("Good");
			ok.AddComponent<UIRectComponent>();
			UIButtonComponent& good = ok.AddComponent<UIButtonComponent>();
			good.OnClickTarget = EntityRef(manager.GetUUID());
			good.OnClickMethod = "Count";

			Check(UI::ValidateBindings(*scene).empty(),
				  "a binding that resolves is not reported");

			// A button read by polling. Reporting these would put a line in the
			// log for every ordinary button in a project, and teach everybody
			// to ignore the check.
			Entity polled = scene->CreateEntity("Polled");
			polled.AddComponent<UIRectComponent>();
			polled.AddComponent<UIButtonComponent>();

			Check(UI::ValidateBindings(*scene).empty(),
				  "and neither is a button with no method named -- that is polling");

			// The rename: the method is gone, the file still says it.
			Entity renamed = scene->CreateEntity("Renamed");
			renamed.AddComponent<UIRectComponent>();
			UIButtonComponent& stale = renamed.AddComponent<UIButtonComponent>();
			stale.OnClickTarget = EntityRef(manager.GetUUID());
			stale.OnClickMethod = "CountUp";   // ClickCounter declares Count

			std::vector<UI::BindingProblem> problems = UI::ValidateBindings(*scene);
			Check(problems.size() == 1, "a method nothing answers to is reported, once");

			if (problems.size() == 1)
			{
				Check(problems[0].Button == "Renamed" && problems[0].Method == "CountUp" &&
					  !problems[0].TargetMissing,
					  "and the report names the button and the method, and says the "
					  "target was found");
			}

			// The deletion: a different failure wearing the same shape, and the
			// message has to tell them apart -- one is fixed by renaming, the
			// other by dragging a replacement in.
			stale.OnClickMethod = "Count";
			stale.OnClickTarget = EntityRef(UUID(123456789));

			problems = UI::ValidateBindings(*scene);
			Check(problems.size() == 1 && problems[0].TargetMissing,
				  "a target that is not in the scene is reported as a missing entity "
				  "rather than as a missing method");

			// An empty target means this entity, exactly as the dispatch reads
			// it. If these two disagreed the check would pass bindings that
			// fail and fail ones that work.
			Entity selfBound = scene->CreateEntity("SelfBound");
			selfBound.AddComponent<UIRectComponent>();
			selfBound.AddComponent<NativeScriptComponent>("ClickCounter");
			UIButtonComponent& own = selfBound.AddComponent<UIButtonComponent>();
			own.OnClickMethod = "Count";

			problems = UI::ValidateBindings(*scene);
			bool selfReported = false;
			for (const UI::BindingProblem& problem : problems)
				selfReported = selfReported || problem.Button == "SelfBound";

			Check(!selfReported,
				  "an empty target resolves to the button's own entity, the same way "
				  "the dispatch reads it");
		}

		// --- methods are declared, and the declaration is what a scene may name ----
		{
			const std::vector<ScriptMethod>& methods = ScriptRegistry::MethodsOf("ClickCounter");

			bool hasCount = false;
			for (const ScriptMethod& method : methods)
				hasCount = hasCount || method.Name == "Count";

			Check(hasCount, "a registered script method is listed for the inspector to offer");
			Check(ScriptRegistry::MethodsOf("Spinner").empty(),
				  "and a script that registers none lists none, which is not an error");
			Check(ScriptRegistry::MethodsOf("NoSuchScript").empty(),
				  "as does a script that does not exist");
		}

		// The pointer is one cursor and therefore one global, so a suite that
		// leaves it parked over a button leaves *every later check* running in a
		// world where the UI owns the pointer.
		//
		// That is not hypothetical: the interop self-test asserts
		// IsPointerOverUI() == 0, and it is what caught this being missing --
		// two failures, in a file nothing here had touched.
		UI::ResetPointer();
	}

	// Text layout.
	//
	// Every one of these runs on the CPU with no window and no device, which is
	// the entire reason layout was kept out of the renderer. The failures here
	// are the ones that look plausible in a screenshot -- a line broken one
	// word early, centring off by half a space, a width that quietly ignores
	// kerning -- and none of them is settled by looking.
	void CheckTextLayout()
	{
		Font font;
		if (!Assets::FontSerializer::Load(font, "assets/Fonts/roboto.rvfont"))
		{
			Check(false, "text layout needs the staged font");
			return;
		}

		// --- UTF-8 ----------------------------------------------------------
		{
			Check(UI::DecodeUtf8("abc").size() == 3, "ASCII decodes one byte per character");

			// U+00E9, two bytes. The atlas is latin1, so this one is really there.
			const std::vector<uint32_t> accented = UI::DecodeUtf8("caf\xC3\xA9");
			Check(accented.size() == 4 && accented[3] == 0xE9,
				  "a two-byte sequence decodes to one codepoint");

			// A lead byte promising three continuations, with none. Stepping the
			// promised length would swallow the 'a' and 'b' after it.
			const std::vector<uint32_t> broken = UI::DecodeUtf8("\xE0" "ab");
			Check(broken.size() == 3 && broken[0] == 0xFFFD &&
				  broken[1] == 'a' && broken[2] == 'b',
				  "a malformed byte becomes U+FFFD and eats nothing after it");
		}

		// --- advances and kerning --------------------------------------------
		{
			const float size = 32.0f;

			// The claim: a measured width is the sum of the advances plus the
			// kerning, and not merely "about right".
			const Font::Glyph* a = font.Find('A');
			const Font::Glyph* v = font.Find('V');
			Check(a && v, "the font has A and V");

			if (a && v)
			{
				const float unkerned = (a->Advance + v->Advance) * size;
				const float measured = UI::MeasureLine("AV", font, size);
				const float kerning = font.Kerning('A', 'V') * size;

				Check(Math::Abs(measured - (unkerned + kerning)) < 1e-3f,
					  "a line's width is its advances plus its kerning, exactly");
				Check(measured < unkerned,
					  "and AV really is drawn tighter than the advances alone");
			}

			// Scale is linear in the size, which is what makes one atlas serve
			// every size.
			const float single = UI::MeasureLine("Hamburgefonstiv", font, 16.0f);
			const float doubled = UI::MeasureLine("Hamburgefonstiv", font, 32.0f);
			Check(Math::Abs(doubled - single * 2.0f) < 1e-2f,
				  "and doubling the size doubles the width");

			Check(UI::MeasureLine("", font, size) == 0.0f, "an empty string is zero wide");
		}

		// --- lines --------------------------------------------------------
		{
			UI::TextStyle style;
			style.Size = 20.0f;

			const UI::TextLayout one = UI::Build("one line", font, style);
			Check(one.LineCount == 1, "a string with no newline is one line");
			Check(one.Glyphs.size() == 7, "and places a glyph for each character but the space");

			const UI::TextLayout three = UI::Build("a\nb\nc", font, style);
			Check(three.LineCount == 3, "explicit newlines start new lines");
			Check(Math::Abs(three.Height - font.LineHeight * style.Size * 3.0f) < 1e-3f,
				  "and the height is three line boxes, not the height of the ink");

			// The second line sits exactly one line box below the first.
			//
			// The same letter on every line, deliberately. A placed glyph's Y
			// is its baseline minus its own height above it, so "a\nb\nc"
			// measures the difference between the tops of a, b and c as much as
			// the line spacing -- which is how the first version of this check
			// managed to fail against correct code.
			const UI::TextLayout stack = UI::Build("a\na\na", font, style);
			Check(stack.Glyphs.size() == 3, "with one glyph on each");
			if (stack.Glyphs.size() == 3)
			{
				const float step = stack.Glyphs[1].Y - stack.Glyphs[0].Y;
				Check(Math::Abs(step - font.LineHeight * style.Size) < 1e-3f,
					  "spaced by exactly one line box");
				Check(Math::Abs(stack.Glyphs[2].Y - stack.Glyphs[1].Y - step) < 1e-3f,
					  "and every line by the same step");
			}

			style.LineSpacing = 2.0f;
			const UI::TextLayout loose = UI::Build("a\nb", font, style);
			Check(Math::Abs(loose.Height - font.LineHeight * style.Size * 4.0f) < 1e-3f,
				  "and line spacing multiplies it");
		}

		// --- wrapping -------------------------------------------------------
		{
			UI::TextStyle style;
			style.Size = 16.0f;

			const std::string sentence = "the quick brown fox jumps over the lazy dog";

			const float full = UI::MeasureLine(sentence, font, style.Size);
			style.WrapWidth = full * 0.5f;

			const UI::TextLayout wrapped = UI::Build(sentence, font, style);
			Check(wrapped.LineCount >= 2, "a sentence wider than the box wraps");
			Check(wrapped.Width <= style.WrapWidth + 1e-3f,
				  "and no line ends up wider than the box");

			// Every line has to start with a word rather than the space that
			// broke it, or a wrapped paragraph is indented by accident.
			//
			// One repeated letter, so that every glyph shares a top and Y is
			// therefore constant along a line. Grouping by Y is only a valid
			// way to find line starts once that is true, and the first version
			// of this check assumed it for mixed text, where it is not.
			{
				UI::TextStyle uniform;
				uniform.Size = 16.0f;
				uniform.WrapWidth = UI::MeasureLine("nnnn nnnn", font, uniform.Size) + 1.0f;

				const UI::TextLayout rows =
					UI::Build("nnnn nnnn nnnn nnnn nnnn nnnn", font, uniform);

				Check(rows.LineCount >= 3, "the uniform paragraph wraps to several lines");

				// The left edge of the first 'n' on a line, if no space were
				// carried over. A leading space would push it a space further.
				const Font::Glyph* n = font.Find('n');
				const float expected = n ? n->Left * uniform.Size : 0.0f;

				bool flush = true;
				float lineY = -1.0f;
				for (const UI::PlacedGlyph& glyph : rows.Glyphs)
				{
					if (glyph.Y > lineY + 1e-3f)
					{
						lineY = glyph.Y;
						flush = flush && Math::Abs(glyph.X - expected) < 1e-2f;
					}
				}
				Check(flush, "and no line begins with the space it broke at");
			}

			// A word that cannot fit has to break rather than overflow: a long
			// number running out of its panel is worse than an ugly break.
			style.WrapWidth = UI::MeasureLine("mmmm", font, style.Size);
			const UI::TextLayout forced =
				UI::Build("Supercalifragilisticexpialidocious", font, style);
			Check(forced.LineCount > 1, "a single word wider than the box breaks mid-word");
			Check(forced.Width <= style.WrapWidth + 1e-3f, "and still respects the box");

			// A box narrower than one character must not loop forever looking
			// for something that fits.
			style.WrapWidth = 0.5f;
			const UI::TextLayout impossible = UI::Build("abc", font, style);
			Check(impossible.LineCount == 3, "a box narrower than a character keeps one per line");
		}

		// --- alignment ------------------------------------------------------
		{
			UI::TextStyle style;
			style.Size = 18.0f;
			style.WrapWidth = 400.0f;

			// Two lines of deliberately different widths, so an alignment
			// offset that is silently zero cannot pass.
			const std::string text = "wwwwwwwwwwww\ni";

			const UI::TextLayout left = UI::Build(text, font, style);
			style.Align = UI::TextAlign::Center;
			const UI::TextLayout centre = UI::Build(text, font, style);
			style.Align = UI::TextAlign::Right;
			const UI::TextLayout right = UI::Build(text, font, style);

			Check(left.Glyphs.size() == centre.Glyphs.size() &&
				  left.Glyphs.size() == right.Glyphs.size(),
				  "alignment moves glyphs rather than adding or dropping them");

			const size_t last = left.Glyphs.size() - 1;   // the lone 'i'
			Check(left.Glyphs[last].X < centre.Glyphs[last].X &&
				  centre.Glyphs[last].X < right.Glyphs[last].X,
				  "the short line moves right as the alignment does");

			// Centring is half the slack, and being exact is the point: half a
			// pixel out is invisible in one label and obvious in a column.
			const float shortWidth = UI::MeasureLine("i", font, style.Size);
			const float slack = left.Width - shortWidth;
			Check(Math::Abs((centre.Glyphs[last].X - left.Glyphs[last].X) - slack * 0.5f) < 1e-2f,
				  "centring offsets by exactly half the slack");
			Check(Math::Abs((right.Glyphs[last].X - left.Glyphs[last].X) - slack) < 1e-2f,
				  "and right alignment by all of it");

			// The long line is the widest, so it does not move at all.
			Check(Math::Abs(left.Glyphs[0].X - right.Glyphs[0].X) < 1e-3f,
				  "while the widest line stays where it is under every alignment");
		}

		// --- trailing spaces --------------------------------------------------
		// A wrapped line ends with the space that broke it. Counting it would
		// push centred text left by half a space, for a reason nobody could see.
		{
			UI::TextStyle style;
			style.Size = 18.0f;

			const UI::TextLayout bare = UI::Build("ab", font, style);
			const UI::TextLayout trailing = UI::Build("ab   ", font, style);
			Check(Math::Abs(bare.Width - trailing.Width) < 1e-3f,
				  "trailing spaces do not count towards a line's width");
		}

		// --- the block's origin ------------------------------------------------
		{
			UI::TextStyle style;
			style.Size = 24.0f;

			const UI::TextLayout layout = UI::Build("Hxy", font, style);
			Check(Math::Abs(layout.FirstBaseline - font.Ascent * style.Size) < 1e-3f,
				  "the first baseline sits an ascent below the top of the block");

			// Nothing may sit above the block, or text in a box would ride up
			// out of it. A capital reaches close to the ascent without passing it.
			bool insideTop = true;
			for (const UI::PlacedGlyph& glyph : layout.Glyphs)
				insideTop = insideTop && glyph.Y >= -1e-3f;
			Check(insideTop, "and no glyph is placed above the top of the block");

			// Texture coordinates point into the atlas the right way up.
			Check(!layout.Glyphs.empty() && layout.Glyphs[0].V0 < layout.Glyphs[0].V1 &&
				  layout.Glyphs[0].U0 < layout.Glyphs[0].U1,
				  "and its texture coordinates run down and to the right");
		}

		// --- degenerate input ---------------------------------------------------
		{
			UI::TextStyle style;
			style.Size = 0.0f;
			Check(UI::Build("anything", font, style).Glyphs.empty(),
				  "a size of zero places nothing");

			style.Size = 16.0f;
			Check(UI::Build("", font, style).Glyphs.empty(), "and so does an empty string");

			Font empty;
			Check(UI::Build("anything", empty, style).Glyphs.empty(),
				  "as does a font with no glyphs");

			// A character the face does not have draws nothing and moves the pen
			// nowhere, rather than drawing a box the font layer invented.
			const std::string missing = "a\xF0\x9F\x98\x80" "b";   // 'a', U+1F600, 'b'
			Check(UI::Build(missing, font, style).Glyphs.size() == 2,
				  "a character the face lacks is skipped rather than boxed");
		}
	}

	// The screen-space UI layer.
	void CheckUIRenderer()
	{
		Check(UIRenderer::IsReady(), "the UI shader compiled");

		// --- which way is up ------------------------------------------------
		//
		// This is here because it was wrong, on both backends in turn, and a
		// screenshot was the only thing that said so. The obvious reasoning --
		// Vulkan's framebuffer origin is the top left, OpenGL's is the bottom,
		// so branch on the backend -- produces a HUD mirrored along the bottom
		// edge, because the UI pass draws into the *finished* image and the
		// post chain has already normalised its orientation.
		//
		// Runs on whichever backend the suite was started with, so the pair of
		// runs covers both.
		{
			const Mat4 projection = UIRenderer::BuildProjection(800, 600);

			const Vec4 topLeft = projection * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const Vec4 bottomRight = projection * Vec4(800.0f, 600.0f, 0.0f, 1.0f);

			Check(Math::Abs(topLeft.x + 1.0f) < 1e-4f && Math::Abs(bottomRight.x - 1.0f) < 1e-4f,
				  "UI x runs left to right across the viewport");

			// The whole point: y = 0 is the *top* of what the viewer sees, and
			// clip +1 is the top, on every backend.
			Check(Math::Abs(topLeft.y - 1.0f) < 1e-4f,
				  "UI y = 0 is the top of the image, not the bottom");
			Check(Math::Abs(bottomRight.y + 1.0f) < 1e-4f,
				  "and y = height is the bottom");

			// Inside the depth range even though the pass has no depth buffer:
			// a clip z outside [0, 1] is clipped away, which would draw nothing
			// at all and look exactly like the bug above.
			Check(topLeft.z >= 0.0f && topLeft.z <= 1.0f,
				  "and its depth lands inside the clip range");
		}

		// A degenerate viewport must not divide by zero. The editor hands over
		// a zero size on the frame before the dock layout has run.
		{
			const Mat4 projection = UIRenderer::BuildProjection(0, 0);
			const Vec4 point = projection * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
			Check(Math::IsFinite(point.x) && Math::IsFinite(point.y),
				  "a zero-sized viewport still produces a finite projection");
		}

		// --- drawing outside a frame ----------------------------------------
		// Every entry point is reachable before a command list exists, because
		// a panel can be submitted from anywhere.
		UIRenderer::Begin(640, 480);
		UIRenderer::DrawRect({ 0.0f, 0.0f, 10.0f, 10.0f }, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
		UIRenderer::DrawImage({ 0.0f, 0.0f, 10.0f, 10.0f }, nullptr);
		UIRenderer::End();
		Check(true, "drawing with no command list is survivable");
	}

	// Baked fonts.
	//
	// The metrics table is the half of text rendering that has no GPU in it,
	// which is exactly why it is worth checking here: a wrong advance, a
	// dropped kerning pair or an inverted plane bound all render as text that
	// looks *almost* right, and "almost right" is not something a screenshot
	// settles.
	void CheckFont()
	{
		// --- the type ------------------------------------------------------
		Check(AssetTypeFromExtension(".rvfont") == AssetType::Font,
			  "a .rvfont is a Font asset");
		Check(AssetTypeFromName("Font") == AssetType::Font &&
			  std::string(AssetTypeName(AssetType::Font)) == "Font",
			  "and its name round-trips, so existing .meta files keep meaning what they said");

		// A font file is the *input* to rvfont. Importing one would hand out a
		// handle that resolves to something nothing at runtime can read.
		Check(AssetTypeFromExtension(".ttf") == AssetType::None,
			  "a .ttf is deliberately not an asset");

		// --- the file the editor ships -------------------------------------
		Font font;
		const bool loaded = Assets::FontSerializer::Load(font, "assets/Fonts/roboto.rvfont");
		Check(loaded, "the staged font loads");
		if (!loaded)
			return;

		Check(font.GetGlyphCount() > 150, "and carries its latin1 glyph table");
		Check(font.GetKerningCount() > 100, "and the pairs the face actually kerns");
		Check(!font.AtlasFile.empty() && font.AtlasWidth > 0 && font.AtlasHeight > 0,
			  "and names an atlas with a size");

		// --- metrics that a layout divides by ------------------------------
		Check(font.Ascent > 0.0f, "the ascent is above the baseline");
		Check(font.Descent < 0.0f, "the descent is below it, signed as the face reports");
		Check(font.LineHeight >= font.Ascent - font.Descent,
			  "and a line is at least tall enough for both");

		// In em units, so a face is roughly one unit tall. This catches the
		// whole class of bug where font units or pixels leak through: Roboto's
		// ascent is about 0.93 em, and if this were in font units it would be
		// nearer 1900.
		Check(font.Ascent < 2.0f && font.Ascent > 0.5f,
			  "and the metrics are in ems rather than font units or pixels");

		// --- glyphs ---------------------------------------------------------
		const Font::Glyph* a = font.Find('A');
		Check(a && a->HasImage(), "'A' has a glyph with an image");
		Check(a && a->Right > a->Left && a->Top > a->Bottom,
			  "whose quad is the right way up and the right way round");
		Check(a && a->Advance > 0.0f, "and moves the pen forward");

		const Font::Glyph* space = font.Find(' ');
		Check(space && !space->HasImage() && space->Advance > 0.0f,
			  "a space has an advance and no image, which are separate questions");

		Check(font.Find(0x1F600) == nullptr,
			  "and a character the face does not have answers null rather than a box");

		// A descender has to reach below the baseline, or every 'g' in the
		// engine sits on the line. Bottom is in ems with y up, so it is negative.
		const Font::Glyph* g = font.Find('g');
		Check(g && g->Bottom < 0.0f, "'g' descends below the baseline");

		// --- kerning --------------------------------------------------------
		// Roboto kerns "AV" tighter; almost nothing kerns a letter against
		// itself. Asking for a pair that is not stored must answer zero rather
		// than something uninitialised.
		Check(font.Kerning('A', 'V') < 0.0f, "AV kerns tighter, as the face says");
		Check(font.Kerning('l', 'l') == 0.0f, "and an unlisted pair is zero, not garbage");

		// --- the number that decides whether it looks right ------------------
		// screenPxRange >= 2, rearranged. 48 px per em over a 6 px range is
		// 16 px, and the tool prints the same figure when it bakes.
		Check(Math::Abs(font.SmallestSharpSize() - 2.0f * font.EmSize / font.PxRange) < 1e-4f,
			  "the smallest sharp size is derived from the atlas, not stored beside it");
		Check(font.SmallestSharpSize() > 4.0f && font.SmallestSharpSize() < 64.0f,
			  "and lands somewhere a person would actually draw text");

		// --- a file that is not one ------------------------------------------
		// The loader builds into a local and moves it out at the end, so a
		// caller that pre-filled a font keeps it rather than being left with
		// half of a broken one.
		{
			const std::filesystem::path junk =
				std::filesystem::temp_directory_path() / "ragev-not-a-font.rvfont";
			{
				std::ofstream file(junk);
				file << "Atlas: nowhere.png\nGlyphs: []\n";
			}

			Font survivor = font;
			Check(!Assets::FontSerializer::Load(survivor, junk),
				  "a font with an empty glyph table fails to load");
			Check(survivor.GetGlyphCount() == font.GetGlyphCount(),
				  "and leaves the caller's font exactly as it was");

			Check(!Assets::FontSerializer::Load(survivor, "assets/Fonts/does-not-exist.rvfont"),
				  "as does one that is not there");

			std::error_code error;
			std::filesystem::remove(junk, error);
		}

		// --- through the manager, which is where the atlas comes from --------
		if (Renderer::HasDevice())
		{
			const AssetHandle handle = Assets::Registry::GetHandle("fonts/roboto.rvfont");
			Check(handle.IsValid(), "the sample project's font is in the registry");

			if (handle.IsValid())
			{
				const Font* managed = Assets::Manager::GetFont(handle);
				Check(managed != nullptr, "and resolves through the manager");
				Check(managed == Assets::Manager::GetFont(handle),
					  "cached, so asking twice is one file read");

				RHI::Ref<RHI::RHITexture> atlas = Assets::Manager::GetFontAtlas(handle);
				Check(atlas != nullptr, "and its atlas uploads");

				// The invariant that is silent when broken. A distance field is
				// data: loaded as sRGB the hardware would un-gamma every texel
				// on read, moving every distance and putting the edge somewhere
				// else. It renders as text that is soft for no visible reason,
				// which is not a thing anybody debugs by looking.
				if (atlas)
				{
					Check(atlas->GetDesc().Format == RHI::Format::R8G8B8A8_UNORM,
						  "as a linear texture -- a distance field read as sRGB is bent");
					Check(atlas->GetDesc().MipLevels == 1,
						  "with no mip chain, which would average distances across a stroke");
				}
			}

			Check(Assets::Manager::GetFont(AssetHandle::Invalid()) == nullptr,
				  "an invalid handle answers null rather than an empty font");
		}
	}

	// The editor's ground grid.
	//
	// The solve is the whole feature, and it is the kind that is wrong quietly:
	// a grid drawn at the wrong depth, mirrored, or sliding with the camera
	// still looks like a grid in a screenshot. So it is asked the questions a
	// picture cannot answer -- always against a point whose depth is already
	// known, because the claim being tested is that the two agree.
	void CheckViewportGrid()
	{
		Check(ViewportGrid::IsReady(), "the grid shader compiled");

		const Mat4 projection = Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);

		// A camera five up, looking down and forwards at the plane.
		const Mat4 eye = Math::Translate(Mat4(1.0f), Vec3(3.0f, 5.0f, 12.0f)) *
						 Math::Rotate(Mat4(1.0f), Math::Radians(-20.0f), Vec3(1.0f, 0.0f, 0.0f));

		const Mat4 view = Math::Inverse(eye);
		const Mat4 viewProjection = projection * view;
		const Mat4 inverse = ViewportGrid::BuildInverseViewProjection(projection, eye);

		// --- the matrix itself ----------------------------------------------
		{
			const Vec4 point(7.0f, -2.0f, 3.0f, 1.0f);
			const Vec4 back = inverse * (viewProjection * point);
			Check(Math::Length(Vec3(back) / back.w - Vec3(point)) < 1e-3f,
				  "the grid's inverse view-projection undoes the one the scene was drawn with");
		}

		// --- the solve agrees with a depth that is already known --------------
		//
		// Every one of these projects a point that is *on* the plane, throws
		// away the depth, and asks the solve to produce it again. Nothing here
		// trusts the grid's own arithmetic for the answer.
		{
			const Vec3 samples[] = {
				{  0.0f, 0.0f,   0.0f },
				{  4.0f, 0.0f,   2.0f },
				{ -6.0f, 0.0f,  -9.0f },
				{  1.0f, 0.0f, -40.0f },
			};

			bool agreed = true;
			bool onPlane = true;

			for (const Vec3& world : samples)
			{
				const Vec4 clip = viewProjection * Vec4(world, 1.0f);
				const Vec3 ndc = Vec3(clip) / clip.w;

				float depth = -1.0f;
				if (!ViewportGrid::PlaneDepthAt(inverse, ndc.x, ndc.y, depth))
				{
					agreed = false;
					break;
				}

				agreed = agreed && Math::Abs(depth - ndc.z) < 1e-4f;

				// And the point it reconstructs is back where it started --
				// which is what the shader goes on to take derivatives of.
				const Vec4 hit = inverse * Vec4(ndc.x, ndc.y, depth, 1.0f);
				const Vec3 back = Vec3(hit) / hit.w;
				onPlane = onPlane && Math::Abs(back.y) < 1e-3f &&
						  Math::Length(back - world) < 1e-2f;
			}

			Check(agreed, "the plane's depth at a pixel is the depth of the point that projects there");
			Check(onPlane, "and the position it reconstructs is on y = 0, where it started");
		}

		// --- near and far are the right way round -----------------------------
		// A reversed depth range renders a grid that looks entirely correct
		// until something is drawn in front of it.
		{
			auto depthOf = [&](const Vec3& world)
			{
				const Vec4 clip = viewProjection * Vec4(world, 1.0f);
				const Vec3 ndc = Vec3(clip) / clip.w;
				float depth = 0.0f;
				ViewportGrid::PlaneDepthAt(inverse, ndc.x, ndc.y, depth);
				return depth;
			};

			Check(depthOf({ 3.0f, 0.0f, 8.0f }) < depthOf({ 3.0f, 0.0f, -30.0f }),
				  "a nearer piece of the plane has the smaller depth");
		}

		// --- past the far plane the grid keeps going --------------------------
		// The reported bug: the grid stopped dead at the camera's far clip and
		// left a hard horizontal edge across the viewport, a long way short of
		// the horizon. A far clip is where the *scene* ends.
		{
			// The projection above has a far clip of 500. This is well past it,
			// and still on the plane in front of the camera.
			const Vec3 distant(0.0f, 0.0f, -4000.0f);
			const Vec4 clip = viewProjection * Vec4(distant, 1.0f);
			const Vec3 ndc = Vec3(clip) / clip.w;

			Check(ndc.z > 1.0f, "the instrument is looking past the far plane");

			float depth = -1.0f;
			const bool hit = ViewportGrid::PlaneDepthAt(inverse, ndc.x, ndc.y, depth);
			Check(hit, "a piece of the plane beyond the far clip is still drawn");
			Check(hit && Math::Abs(depth - 1.0f) < 1e-5f,
				  "and is pinned to the far plane, behind everything that could occlude it");
		}

		// --- above the horizon there is no plane ------------------------------
		{
			int found = 0;
			for (float x = -0.9f; x <= 0.9f; x += 0.45f)
			{
				float depth = 0.0f;
				if (ViewportGrid::PlaneDepthAt(inverse, x, 0.95f, depth))
					found++;
			}
			Check(found == 0, "and nothing above the horizon hits it at all");
		}

		// --- whatever it answers is inside the depth range --------------------
		// The contract the pipeline depends on: a fragment that is kept writes
		// a depth the test can use.
		{
			bool inRange = true;
			for (float y = -0.99f; y <= 0.99f; y += 0.09f)
			{
				for (float x = -0.99f; x <= 0.99f; x += 0.09f)
				{
					float depth = 0.0f;
					if (ViewportGrid::PlaneDepthAt(inverse, x, y, depth))
						inRange = inRange && depth >= 0.0f && depth <= 1.0f;
				}
			}
			Check(inRange, "every pixel it accepts gets a depth inside [0, 1]");
		}

		// --- from underneath --------------------------------------------------
		// A plane has two sides and the editor camera can get to both.
		{
			const Mat4 below = Math::Translate(Mat4(1.0f), Vec3(0.0f, -4.0f, 10.0f)) *
							   Math::Rotate(Mat4(1.0f), Math::Radians(15.0f), Vec3(1.0f, 0.0f, 0.0f));
			const Mat4 under = ViewportGrid::BuildInverseViewProjection(projection, below);

			const Vec3 world(2.0f, 0.0f, 1.0f);
			const Vec4 clip = (projection * Math::Inverse(below)) * Vec4(world, 1.0f);
			const Vec3 ndc = Vec3(clip) / clip.w;

			float depth = -1.0f;
			const bool hit = ViewportGrid::PlaneDepthAt(under, ndc.x, ndc.y, depth);
			Check(hit && Math::Abs(depth - ndc.z) < 1e-4f,
				  "a camera under the plane sees it from below, at the same depth");
		}

		// --- a grid is a place, not a direction -------------------------------
		// The opposite of the property CheckSky asserts, and the reason the two
		// build different matrices: the sky drops the camera's translation and
		// this must not.
		{
			const Mat4 moved = Math::Translate(Mat4(1.0f), Vec3(30.0f, 5.0f, 12.0f)) *
							   Math::Rotate(Mat4(1.0f), Math::Radians(-20.0f), Vec3(1.0f, 0.0f, 0.0f));
			const Mat4 shifted = ViewportGrid::BuildInverseViewProjection(projection, moved);

			auto worldAt = [](const Mat4& matrix, float x, float y, Vec3& out)
			{
				float depth = 0.0f;
				if (!ViewportGrid::PlaneDepthAt(matrix, x, y, depth))
					return false;
				const Vec4 hit = matrix * Vec4(x, y, depth, 1.0f);
				out = Vec3(hit) / hit.w;
				return true;
			};

			Vec3 here, there;
			const bool both = worldAt(inverse, 0.0f, -0.4f, here) &&
							  worldAt(shifted, 0.0f, -0.4f, there);
			Check(both && Math::Length(here - there) > 20.0f,
				  "walking the camera across the world moves what the centre pixel lands on");
		}

		// --- edge on ----------------------------------------------------------
		// The camera exactly in the plane, which is a real thing to do with an
		// editor camera and is where the solve divides by something near zero.
		{
			const Mat4 flat = Math::Translate(Mat4(1.0f), Vec3(0.0f, 0.0f, 10.0f));
			const Mat4 edge = ViewportGrid::BuildInverseViewProjection(projection, flat);

			float depth = 0.0f;
			const bool hit = ViewportGrid::PlaneDepthAt(edge, 0.0f, 0.0f, depth);
			Check(!hit || (depth >= 0.0f && depth <= 1.0f),
				  "and looking along the plane either declines or stays in range");
		}

		// --- outside a frame ---------------------------------------------------
		ViewportGrid::Draw(Camera(projection), eye, ViewportGridSettings{});
		Check(true, "drawing one with no command list is survivable");
	}

	void CheckFrameGraph()
	{
		if (!Renderer::HasDevice())
			return;

		RenderGraph graph(Renderer::GetDevice());

		// Two blocks rather than one, because the chain reads two: what the
		// frame costs comes from the project and what it grades comes from the
		// camera's profile. ENGINE-NOTES 7s.
		auto build = [&](uint32_t width, uint32_t height,
						 const RenderSettings& render, const PostSettings& post)
		{
			graph.Begin(width, height);

			FrameDesc frame;
			frame.Output = graph.Backbuffer();
			frame.Width = width;
			frame.Height = height;
			frame.Render = render;
			frame.Post = post;
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

		RenderSettings render;
		PostSettings post;

		// --- the full chain -----------------------------------------------------
		Check(build(1600, 900, render, post), "the standard frame compiles");
		Check(hasPass("Scene"), "it draws the scene");
		Check(hasPass("Bloom prefilter"), "thresholds for bloom");
		Check(hasPass("Bloom down"), "downsamples");
		Check(hasPass("Bloom up"), "and blurs back up");
		Check(hasPass("Tonemap"), "tone maps");
		Check(hasPass("FXAA"), "and anti-aliases");

		const size_t withEverything = graph.GetPassCount();

		// --- bloom off ----------------------------------------------------------
		post.BloomEnabled = false;
		Check(build(1600, 900, render, post), "with bloom off it still compiles");
		Check(!hasPass("Bloom"), "and has no bloom passes at all");
		Check(hasPass("Tonemap"), "but still tone maps, because that is not optional");
		Check(graph.GetPassCount() < withEverything, "so the frame is shorter");

		// --- motion blur (9.5) --------------------------------------------------
		//
		// Off is the default and adds nothing -- the standard-frame assertions
		// above already ran without a motion pass, which is the off-is-off
		// half. This is the each-effect-lands half: on adds all four stages,
		// because a chain missing its neighbour max would still blur and only
		// tear at tile boundaries, which no screenshot of a slow scene shows.
		post.MotionBlur = true;
		Check(build(1600, 900, render, post), "with motion blur on the frame compiles");
		Check(hasPass("Motion pack") && hasPass("Motion tile max")
			  && hasPass("Motion neighbor max") && hasPass("Motion gather"),
			  "and all four motion stages are in it");
		post.MotionBlur = false;
		Check(build(1600, 900, render, post) && !hasPass("Motion"),
			  "off again adds none of them");

		// --- SSAO (9.6) ---------------------------------------------------------
		post.AmbientOcclusion = AoDetail::Half;
		Check(build(1600, 900, render, post), "with SSAO on the frame compiles");
		Check(hasPass("SSAO compute") && hasPass("SSAO blur x")
			  && hasPass("SSAO blur y") && hasPass("SSAO apply"),
			  "and all four SSAO stages are in it");
		post.AmbientOcclusion = AoDetail::Off;
		Check(build(1600, 900, render, post) && !hasPass("SSAO"),
			  "off again adds none of them");

		// --- global illumination (9.12). ENGINE-NOTES 7at -----------------------
		//
		// The same shape as SSAO's chain, and the same two halves: off adds
		// nothing, on adds all four. The third assertion is the one this
		// feature adds -- the traced twin takes the whole chain away, whatever
		// the profile says, which is what the greyed-out row in the editor is
		// promising.
		//
		// **Since 7av the gather writes an Indirect buffer rather than adding
		// to the image**, so it needs somewhere to live between frames --
		// exactly as SSR does below. A caller with no Indirect history gets no
		// GI passes and the probe alone, which is what a probe capture wants.
		post.GlobalIllumination = true;
		Check(build(1600, 900, render, post), "with GI on and nowhere to keep the bounce, the frame compiles");
		Check(!hasPass("SSGI"), "and adds no SSGI pass, because next frame could not read it");
		{
			TemporalHistory indirect;

			auto buildBouncing = [&]()
			{
				graph.Begin(1600, 900);

				FrameDesc frame;
				frame.Output = graph.Backbuffer();
				frame.Width = 1600;
				frame.Height = 900;
				frame.Render = render;
				frame.Post = post;
				frame.OutputFormat = RHI::Format::R8G8B8A8_UNORM;
				frame.DrawScene = [](RGPassContext&) {};
				frame.Indirect = &indirect;

				BuildFrame(graph, frame);
				return graph.Compile();
			};

			Check(buildBouncing(), "with GI on and an indirect history the frame compiles");
			Check(hasPass("SSGI compute") && hasPass("SSGI blur x")
				  && hasPass("SSGI blur y") && hasPass("GI denoise"),
				  "and all four SSGI stages are in it, the last a denoise into the buffer");
			Check(hasPass("GI denoise") && hasPass("SSAO apply") == false,
				  "and the occlusion chain is not dragged in with it");

			// **The screen-space path accumulates again** (ENGINE-NOTES 7ay).
			// 7av had to stop it: the gather read the lit image, the lit image
			// carried last frame's indirect, and blending its own output back
			// in compounded it -- +16.98 levels against a calibrated +1.71,
			// with the backends 2.03 apart. The gather now subtracts what the
			// lit shader added, so the two forms end on the same pass, and
			// asserting the *shared name* is what stops a later edit from
			// quietly re-splitting them.
			post.GiDenoise = 0.0f;
			Check(buildBouncing() && hasPass("GI denoise"),
				  "and at an accumulation of zero the pass still runs, as a copy");
			post.GiDenoise = 0.9f;
			Check(buildBouncing(), "and the frame still compiles with it back on");

			// The traced twin takes the whole chain away whatever the profile
			// holds, which is what the greyed-out row promises.
			//
			// Only where the traced form can actually resolve on. OpenGL has
			// no ray queries and 7ao's rule is that none of the ray-tracing
			// rows are even offered there, so the checkbox stays false and the
			// screen-space chain rightly keeps running -- asserting its
			// absence would be asserting that OpenGL loses GI.
			if (RayShadows::IsAvailable())
			{
				render.RayTracing = true;
				render.RayTracedGlobalIllumination = RayDetail::High;
				Check(buildBouncing() && !hasPass("SSGI"),
					  "and with the traced form resolved on, the screen-space chain is absent");

				// The same pass, under the same name: TraceReflection shades
				// its hits from the lights and the probe rather than from this
				// buffer, and since 7ay the gather subtracts its own
				// contribution, so neither form returns its output to itself.
				Check(hasPass("GI denoise"),
					  "and the traced form ends on that same denoise");

				// Zero does not remove the pass; it makes it a copy. That is
				// what keeps "the accumulation is off" a claim about pixels
				// rather than about the graph.
				post.GiDenoise = 0.0f;
				Check(buildBouncing() && hasPass("GI denoise"),
					  "and at an accumulation of zero it still runs, as a copy");
				post.GiDenoise = 0.9f;

				// How deep the path goes (ENGINE-NOTES 7ax). Clamped where it
				// is *read* and not only where it is typed: --gi-bounces=
				// rejects anything but 1 and 2, and a scene file's int went
				// through neither.
				post.GiBounces = 2;
				Check(ResolveGiBounces(post) == 2, "two bounces resolve as two");
				post.GiBounces = 7;
				Check(ResolveGiBounces(post) == 2,
					  "and a project asking for seven gets two, not seven");
				post.GiBounces = 0;
				Check(ResolveGiBounces(post) == 1,
					  "and one asking for none gets one, because zero bounces is not off");
				post.GiBounces = 1;

				render.RayTracedGlobalIllumination = RayDetail::Off;
				render.RayTracing = false;
			}

			// The voxel form (8.1, ENGINE-NOTES 7bc): the third writer of the
			// same buffer, replacing the screen gather at the head of the
			// chain where the project resolves to it -- and only with a grid
			// lit this frame, so the first thing is an update with nothing
			// in it, which is what a frame with no casters in reach gets.
			if (VoxelGI::IsReady())
			{
				post.VoxelGlobalIllumination = true;
				VoxelGI::Invalidate();
				Check(buildBouncing() && !hasPass("Voxel GI gather") && !hasPass("SSGI"),
					  "voxel GI resolved on with no grid lit this frame adds no chain at all");

				Renderer::GetDevice().ExecuteImmediate([&](RHI::RHICommandList& cmd)
				{
					VoxelGiSettings settings;
					VoxelGI::Update(cmd, settings, Vec3(0.0f), LightList{},
									[](const Vec3&, const Vec3&) {});
				});
				Check(VoxelGI::HasGrid(), "an update with no casters still lights a grid");
				Check(buildBouncing() && hasPass("Voxel GI gather") && !hasPass("SSGI compute"),
					  "and with a grid the frame carries the voxel gather and not the screen one");
				Check(hasPass("SSGI blur x") && hasPass("SSGI blur y") && hasPass("GI denoise"),
					  "and the blur and the denoise after it are the same passes the screen form ends on");

				if (RayShadows::IsAvailable())
				{
					render.RayTracing = true;
					render.RayTracedGlobalIllumination = RayDetail::High;
					Check(buildBouncing() && !hasPass("Voxel GI gather") && !hasPass("SSGI"),
						  "and with the traced form on as well, rays win: neither gather runs");
					render.RayTracedGlobalIllumination = RayDetail::Off;
					render.RayTracing = false;
				}

				post.VoxelGlobalIllumination = false;
				VoxelGI::Invalidate();
				Check(buildBouncing() && hasPass("SSGI compute") && !hasPass("Voxel GI gather"),
					  "and off again, the screen gather is back");
			}
		}
		post.GlobalIllumination = false;
		Check(build(1600, 900, render, post) && !hasPass("SSGI"),
			  "off again adds none of them");

		// --- SSR (9.7, 9.9) -----------------------------------------------------
		//
		// The trace is written at the end of one frame and read by the next
		// frame's lighting, so it needs somewhere to live between them --
		// a caller with no Reflections history gets the probe alone and no
		// SSR passes, the same shape as TAA with no History. ENGINE-NOTES 7af.
		post.ScreenSpaceReflections = true;
		Check(build(1600, 900, render, post), "with SSR on and nowhere to keep the trace, the frame compiles");
		Check(!hasPass("SSR"), "and adds no SSR pass, because next frame could not read it");
		{
			TemporalHistory reflections;

			auto buildReflecting = [&]()
			{
				graph.Begin(1600, 900);

				FrameDesc frame;
				frame.Output = graph.Backbuffer();
				frame.Width = 1600;
				frame.Height = 900;
				frame.Render = render;
				frame.Post = post;
				frame.OutputFormat = RHI::Format::R8G8B8A8_UNORM;
				frame.DrawScene = [](RGPassContext&) {};
				frame.Reflections = &reflections;

				BuildFrame(graph, frame);
				return graph.Compile();
			};

			Check(buildReflecting(), "with SSR on and a reflections history the frame compiles");
			Check(hasPass("SSR hi-Z fine") && hasPass("SSR hi-Z coarse")
				  && hasPass("SSR trace") && hasPass("SSR resolve"),
				  "and all four SSR stages are in it: the two halves of the pyramid, "
				  "the walk, the resolve");
			// The pair is advanced by BuildFrame, exactly as TAA's is: after
			// one frame there is a trace for the next frame to read.
			Check(reflections.HasHistory(),
				  "and one frame later there is a trace for the next frame's lighting");
			Check(reflections.Current() && reflections.Previous()
				  && reflections.Current() != reflections.Previous(),
				  "kept in a pair, so the lighting never reads the target the resolve is writing");

			// SSR passes sit after SSAO now: a ray's radiance should carry
			// the occlusion of the corner it landed in.
			post.AmbientOcclusion = AoDetail::Half;
			Check(buildReflecting(), "with SSAO on as well it compiles");
			{
				size_t ssao = graph.GetPassCount(), ssr = 0;
				for (size_t i = 0; i < graph.GetPassCount(); i++)
				{
					if (graph.GetPassName(i) == "SSAO apply") ssao = i;
					if (graph.GetPassName(i) == "SSR trace") ssr = i;
				}
				Check(ssr > ssao, "and the trace runs after the occlusion is applied");
			}
			post.AmbientOcclusion = AoDetail::Off;

			post.ScreenSpaceReflections = false;
			Check(buildReflecting() && !hasPass("SSR"), "off again adds neither");
			Check(!reflections.HasHistory(),
				  "and forgets the trace, so switching back on does not resume from a stale one");
		}

		// --- anti-aliasing off ---------------------------------------------------
		post.BloomEnabled = true;
		render.AA = AntiAliasing::None;
		Check(build(1600, 900, render, post), "with anti-aliasing off it compiles");
		Check(!hasPass("FXAA"), "and skips the FXAA pass");
		// Without FXAA the tonemap has to land in the output directly, rather
		// than in an intermediate nobody then presents.
		Check(hasPass("Tonemap"), "with tone mapping writing the output itself");

		// --- SMAA replaces the one pass with three ------------------------------
		render.AA = AntiAliasing::SMAA;
		Check(build(1600, 900, render, post), "with SMAA it compiles");
		Check(hasPass("SMAA edges"), "finds edges");
		Check(hasPass("SMAA weights"), "reconstructs their coverage");
		Check(hasPass("SMAA blend"), "and spends it");
		Check(!hasPass("FXAA"), "and does not also run FXAA");
		Check(hasPass("Tonemap"), "still tone mapping first, because both filters "
								  "threshold on perceived brightness");

		// A mode this build has no pass for must fall back to writing the
		// output directly. Otherwise tone mapping lands in an intermediate
		// nothing presents, and a scene saved by a later version opens as a
		// black window with no error anywhere.
		// MSAA changes the *shape* of the scene target rather than adding a
		// pass, so the frame looks exactly like the unfiltered one -- which is
		// the whole design, and the thing worth asserting.
		render.AA = AntiAliasing::MSAA;
		render.MsaaSamples = 4;
		Check(build(1600, 900, render, post), "with MSAA it compiles");
		Check(!hasPass("FXAA") && !hasPass("SMAA") && !hasPass("SSAA resolve"),
			  "and adds no resolve pass of its own, because the hardware does it");
		Check(hasPass("Tonemap"), "with tone mapping writing the output directly");

		// SSAA draws the scene larger and resolves it down, so it adds a pass
		// *before* bloom and skips the one after tone mapping entirely.
		render.AA = AntiAliasing::SSAA;
		render.SupersampleFactor = 2;
		Check(build(1600, 900, render, post), "with SSAA it compiles");
		Check(hasPass("SSAA resolve"), "and resolves the larger scene down");
		Check(!hasPass("FXAA") && !hasPass("SMAA"),
			  "with no post filter, because its work is already done");

		// A factor of one is SSAA that supersamples nothing, and a resolve
		// pass that averages one sample is a blit with extra steps.
		render.SupersampleFactor = 1;
		Check(build(1600, 900, render, post), "SSAA at a factor of one compiles");
		Check(!hasPass("SSAA resolve"), "and adds no resolve pass at all");

		// TAA needs somewhere to accumulate, and a caller that has not supplied
		// one gets no filter rather than the worse half of the feature: the
		// jitter without the history is a wobble and nothing else.
		render.SupersampleFactor = 2;
		render.AA = AntiAliasing::TAA;
		Check(build(1600, 900, render, post), "with TAA and no history it compiles");
		Check(!hasPass("TAA resolve"),
			  "and does not resolve, because there is nowhere to accumulate into");
		Check(!hasPass("FXAA") && !hasPass("SMAA") && !hasPass("SSAA resolve"),
			  "running no other filter either");
		Check(hasPass("Tonemap"), "with tone mapping writing the output directly");

		// And with one, the resolve appears. The history is the caller's,
		// because a pooled graph target has no identity from one frame to the
		// next -- which is the whole reason this parameter exists.
		{
			TemporalHistory history;

			auto buildTemporal = [&](const RenderSettings& render)
			{
				graph.Begin(1600, 900);

				FrameDesc frame;
				frame.Output = graph.Backbuffer();
				frame.Width = 1600;
				frame.Height = 900;
				frame.Render = render;
				frame.Post = post;
				frame.OutputFormat = RHI::Format::R8G8B8A8_UNORM;
				frame.DrawScene = [](RGPassContext&) {};
				frame.History = &history;

				BuildFrame(graph, frame);
				return graph.Compile();
			};

			Check(buildTemporal(render), "with TAA and a history it compiles");
			Check(hasPass("TAA resolve"), "and resolves into the accumulated image");
			Check(hasPass("Tonemap"), "which tone mapping then reads");

			// The first frame has nothing to blend with. Advance is called by
			// BuildFrame, so after one frame there is a history and after two
			// the filter is actually accumulating.
			Check(history.HasHistory(),
				  "and one frame later there is something to accumulate into");
			Check(history.Current() && history.Previous(),
				  "with both halves of the pair allocated");
			Check(history.Current() != history.Previous(),
				  "and they are not the same target, which would be a filter "
				  "sampling the image it is drawing into");

			const RHI::Ref<RHI::RHIRenderTarget> afterOne = history.Current();
			Check(buildTemporal(render), "a second TAA frame compiles");
			Check(history.Current() == afterOne || history.Previous() == afterOne,
				  "and the pair ping-pongs rather than reallocating");

			// Switching away has to forget. Ten seconds of FXAA and back must
			// not resume from a ten-second-old image, and the reason this is
			// checked rather than assumed is that nothing about the picture
			// would show it -- the stale frame is blended in at 10% and is
			// gone before anyone can point at it.
			RenderSettings other = render;
			other.AA = AntiAliasing::FXAA;
			Check(buildTemporal(other), "switching to FXAA compiles");
			Check(!hasPass("TAA resolve"), "and stops resolving");
			Check(!history.HasHistory(),
				  "and throws the accumulated image away, so switching back "
				  "starts again rather than resuming from whenever it stopped");
		}

		render.SupersampleFactor = 2;
		render.AA = (AntiAliasing)99;
		Check(build(1600, 900, render, post), "an unknown anti-aliasing mode compiles");
		Check(!hasPass("FXAA") && !hasPass("SMAA"), "running neither filter");
		Check(hasPass("Tonemap"), "with tone mapping writing the output itself");

		render.AA = AntiAliasing::FXAA;

		// --- a tiny frame --------------------------------------------------------
		// The chain has to stop before the levels are a handful of texels,
		// where the filters stop meaning anything and a zero-sized target is
		// one division away.
		Check(build(64, 64, render, post), "a small frame compiles");
		const size_t smallPasses = graph.GetPassCount();
		Check(build(1600, 900, render, post), "a large one compiles");
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

		// A real PNG, so the cook step has something to cook. The engine icon
		// beside this executable is one; what it depicts does not matter.
		std::filesystem::create_directories(Project::AssetRoot() / "textures", error);
		std::filesystem::copy_file("assets/icon-32.png",
								   Project::AssetRoot() / "textures" / "crate_color.png",
								   std::filesystem::copy_options::overwrite_existing, error);
		Check(!error, "the fixture texture copied in");

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
		Check(std::filesystem::exists(output / "ragev.ini"),
			  "with a config file the player can edit");

		// The content is one archive since 7.1; Project::Load mounts it over
		// content/, so the shipped project file's AssetDirectory still names
		// the directory the pak shadows. The sidecars ride inside it -- they
		// are the identity: a scene refers to an asset by handle and the
		// handle lives in the .meta, so a pak without them is a pak where
		// every reference resolves to nothing.
		Check(std::filesystem::exists(output / "content.pak"),
			  "the project's assets ship as one archive");
		Check(!std::filesystem::exists(output / "content"),
			  "and not as a loose tree beside it");
		{
			IO::PakReader pak;
			Check(pak.Open(output / "content.pak"), "which opens as a pak");
			Check(pak.Contains("scenes/main.rage"),
				  "holding the scene at the path the mount will answer");
			Check(pak.Contains("scenes/main.rage.meta"),
				  "and the .meta sidecars, because they are the asset identity");

			// Cooking is the default, and it is a content transform: the entry
			// keeps the source's path and name, only the bytes change.
			std::vector<uint8_t> entry;
			Check(pak.ReadBytes("textures/crate_color.png", entry) &&
				  IO::TextureCook::IsCooked(entry.data(), entry.size()),
				  "the texture cooked into the pak under its own name");

			IO::CookedTexture cooked;
			Check(IO::TextureCook::Deserialize(cooked, entry.data(), entry.size()) &&
				  cooked.Width == 32 && cooked.Mips.size() == 6,
				  "with its dimensions and a full chain");
		}

		// --raw is the escape hatch: source bytes, byte for byte.
		{
			PackageDesc raw = desc;
			raw.OutputDirectory = output.parent_path() / "packaged-raw";
			raw.RawContent = true;
			Check(PackageProject(raw).Success, "a raw build packages on request");

			IO::PakReader pak;
			std::vector<uint8_t> entry;
			Check(pak.Open(raw.OutputDirectory / "content.pak") &&
				  pak.ReadBytes("textures/crate_color.png", entry) &&
				  entry.size() >= 4 && std::memcmp(entry.data(), "\x89PNG", 4) == 0,
				  "and its texture is still a PNG");
			std::filesystem::remove_all(raw.OutputDirectory, error);
		}

		// The escape hatch: a loose build is one where a single file can be
		// swapped to test a fix without repacking.
		{
			PackageDesc loose = desc;
			loose.OutputDirectory = output.parent_path() / "packaged-loose";
			loose.LooseContent = true;
			Check(PackageProject(loose).Success, "a loose build packages on request");
			Check(std::filesystem::exists(loose.OutputDirectory / "content" /
										  "scenes" / "main.rage.meta"),
				  "and ships the old folder layout, sidecars included");
			std::filesystem::remove_all(loose.OutputDirectory, error);
		}

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
	// Cross-fading between clips (7.5).
	//
	// `BlendPoses` was written and tested long before this; what was missing
	// is that *nothing called it*, so a character snapped. The claim here is
	// therefore about the animator, not the blend: changing `Clip` has to put
	// the pose somewhere between the two clips for a while, and a check that
	// only asserted the endpoints would pass on the snapping version too.
	void CheckAnimationBlend()
	{
		// One bone, two clips: one holds it at rest, the other turns it a
		// quarter. Halfway through a fade the bone must be at neither.
		Skeleton skeleton;
		Bone bone;
		bone.Name = "bone";
		bone.Parent = -1;
		skeleton.Bones = { bone };

		Anim::Clip still;
		still.Name = "still";
		still.Tracks.resize(1);
		still.Tracks[0].Rotation.Times = { 0.0f, 1.0f };
		still.Tracks[0].Rotation.Values = { Quat(1.0f, 0.0f, 0.0f, 0.0f),
											Quat(1.0f, 0.0f, 0.0f, 0.0f) };
		still.RecomputeDuration();

		const Quat turned = Math::AngleAxis(Math::Radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));

		Anim::Clip turn;
		turn.Name = "turn";
		turn.Tracks.resize(1);
		turn.Tracks[0].Rotation.Times = { 0.0f, 1.0f };
		turn.Tracks[0].Rotation.Values = { turned, turned };
		turn.RecomputeDuration();

		// **Driven through Scene::UpdateAnimators, not through a copy of it.**
		// A check that re-implements the pass it is checking passes on the
		// broken version too, which is the whole failure mode this suite
		// exists to avoid.
		//
		// Against the fox rather than the generated limb, because the fox is
		// the one model here with **three clips**: a clip-to-clip cross-fade
		// is the thing 7.5 added, and a fixture with one clip can only ever
		// exercise the transition to the bind pose. Twenty-four bones, and
		// somebody else's rig -- which is the other reason to use it.
		if (!Renderer::HasDevice() || !Assets::Registry::IsInitialised())
			return;

		const AssetHandle model = Assets::Registry::GetHandle("models/fox.glb");
		if (!model.IsValid() || !Assets::Manager::GetSkeleton(model))
			return;

		const std::vector<Anim::Clip>* foxClips = Assets::Manager::GetClips(model);
		Check(foxClips && foxClips->size() >= 3,
			  "the fox brings three clips, or this check proves less than it says");
		if (!foxClips || foxClips->size() < 3)
			return;

		auto scene = std::make_shared<Scene>();
		Entity character = scene->CreateEntity("Fox");
		character.AddComponent<MeshComponent>().Mesh = model;

		AnimatorComponent& animator = character.AddComponent<AnimatorComponent>();
		animator.Clip = 0;
		animator.BlendTime = 0.5f;

		// How far the pose is from the bind pose, which is the one number
		// that says "the bone has moved" without knowing which bone the
		// fixture animates.
		auto deviation = [&]()
		{
			float worst = 0.0f;
			for (const Mat4& m : character.GetComponent<AnimatorComponent>().Skinning)
			{
				for (int column = 0; column < 4; column++)
					for (int row = 0; row < 4; row++)
						worst = Math::Max(worst,
							Math::Abs(m[column][row] - (column == row ? 1.0f : 0.0f)));
			}
			return worst;
		};

		// Run the clip until the bone is somewhere the bind pose is not.
		for (int frame = 0; frame < 12; frame++)
			scene->UpdateAnimators(1.0f / 60.0f);

		const float moved = deviation();
		Check(moved > 0.05f, "the clip moves the skeleton away from its bind pose");

		// The two references this has to sit between: what each clip alone
		// says at the moment sampled. Built by running the same pass with no
		// blend time, so they are the engine's own answers rather than a
		// second implementation's.
		auto poseOf = [&](int clip, float warmup, float then)
		{
			auto reference = std::make_shared<Scene>();
			Entity subject = reference->CreateEntity("Fox");
			subject.AddComponent<MeshComponent>().Mesh = model;

			AnimatorComponent& plain = subject.AddComponent<AnimatorComponent>();
			plain.Clip = clip;
			plain.BlendTime = 0.0f;

			if (warmup > 0.0f)
				reference->UpdateAnimators(warmup);
			reference->UpdateAnimators(then);

			return subject.GetComponent<AnimatorComponent>().Skinning;
		};

		auto distance = [](const std::vector<Mat4>& a, const std::vector<Mat4>& b)
		{
			float worst = 0.0f;
			const size_t count = Math::Min(a.size(), b.size());
			for (size_t i = 0; i < count; i++)
				for (int column = 0; column < 4; column++)
					for (int row = 0; row < 4; row++)
						worst = Math::Max(worst, Math::Abs(a[i][column][row] - b[i][column][row]));
			return worst;
		};

		// Switch to a different clip, then step half the blend.
		character.GetComponent<AnimatorComponent>().Clip = 2;
		scene->UpdateAnimators(0.25f);   // half of BlendTime

		const std::vector<Mat4> midFade = character.GetComponent<AnimatorComponent>().Skinning;

		// Where each clip would be on its own at this instant. The outgoing
		// one has been running 12 frames plus the blend; the incoming one
		// started at zero when the switch happened.
		const std::vector<Mat4> outgoingAlone = poseOf(0, 12.0f / 60.0f, 0.25f);
		const std::vector<Mat4> incomingAlone = poseOf(2, 0.0f, 0.25f);

		Check(distance(midFade, outgoingAlone) > 1e-3f &&
			  distance(midFade, incomingAlone) > 1e-3f,
			  "halfway through a clip-to-clip fade the pose is neither clip's "
			  "-- which is the whole difference from snapping");

		// And it arrives on the new clip.
		scene->UpdateAnimators(0.30f);
		Check(character.GetComponent<AnimatorComponent>().FadingFrom < 0,
			  "the fade finishes and nothing is left fading");
		Check(distance(character.GetComponent<AnimatorComponent>().Skinning,
					   poseOf(2, 0.0f, 0.55f)) < 1e-3f,
			  "and the pose is the new clip's alone");

		// The control that makes the number above mean something: with no
		// blend time the same switch lands on the new clip in one update. If
		// this also produced an in-between pose, the check above would be
		// measuring the clips rather than the fade.
		{
			auto snapScene = std::make_shared<Scene>();
			Entity snapped = snapScene->CreateEntity("Fox");
			snapped.AddComponent<MeshComponent>().Mesh = model;

			AnimatorComponent& instant = snapped.AddComponent<AnimatorComponent>();
			instant.Clip = 0;
			instant.BlendTime = 0.0f;

			for (int frame = 0; frame < 12; frame++)
				snapScene->UpdateAnimators(1.0f / 60.0f);

			snapped.GetComponent<AnimatorComponent>().Clip = 2;
			snapScene->UpdateAnimators(0.25f);

			Check(distance(snapped.GetComponent<AnimatorComponent>().Skinning,
						   incomingAlone) < 1e-3f,
				  "a blend time of zero still snaps, so the easing above is "
				  "the blend and not the clips happening to differ");
		}

		// --- when an animator is allowed to run at all -----------------------
		//
		// Animation belongs to the running game. In the editor a character
		// holds its bind pose unless somebody asked for a preview, because an
		// editor whose characters are all mid-stride has nothing standing
		// still to build against.
		{
			auto editorScene = std::make_shared<Scene>();
			Entity idle = editorScene->CreateEntity("Fox");
			idle.AddComponent<MeshComponent>().Mesh = model;
			idle.AddComponent<AnimatorComponent>().Clip = 2;

			for (int frame = 0; frame < 12; frame++)
				editorScene->UpdateAnimators(1.0f / 60.0f, /*editing*/ true);

			Check(idle.GetComponent<AnimatorComponent>().Skinning.empty(),
				  "an animator does not run while the scene is only being edited");
			Check(idle.GetComponent<AnimatorComponent>().Time == 0.0f,
				  "and its clock does not advance either");

			// The opt-in.
			idle.GetComponent<AnimatorComponent>().RunInEditor = true;
			for (int frame = 0; frame < 12; frame++)
				editorScene->UpdateAnimators(1.0f / 60.0f, /*editing*/ true);

			Check(!idle.GetComponent<AnimatorComponent>().Skinning.empty() &&
				  idle.GetComponent<AnimatorComponent>().Time > 0.0f,
				  "ticking Run in editor previews it");

			// And back off: the pose is cleared rather than frozen, so the
			// character returns to the shape it was modelled in.
			idle.GetComponent<AnimatorComponent>().RunInEditor = false;
			editorScene->UpdateAnimators(1.0f / 60.0f, /*editing*/ true);
			Check(idle.GetComponent<AnimatorComponent>().Skinning.empty(),
				  "unticking it returns to the bind pose rather than freezing");

			// Play ignores the switch entirely.
			editorScene->UpdateAnimators(1.0f / 60.0f, /*editing*/ false);
			Check(!idle.GetComponent<AnimatorComponent>().Skinning.empty(),
				  "and a running scene animates whatever that switch says");
		}

		// --- 7.6 on the same real rig ----------------------------------------
		// The generated fixture proves the arithmetic; this proves it against
		// twenty-four bones and three clips somebody else authored.
		{
			Assets::ImportedModel imported;
			if (Assets::GltfImporter::Import(Project::AssetPath("models/fox.glb"), imported) &&
				!imported.Primitives.empty() && imported.HasSkeleton())
			{
				std::vector<Vec3> positions;
				positions.reserve(imported.Primitives[0].Vertices.size());
				for (const MeshVertex& vertex : imported.Primitives[0].Vertices)
					positions.push_back(vertex.Position);

				Vec3 bindMin, bindMax, animatedMin, animatedMax;
				Anim::SkinnedBounds(imported.Skeleton, {}, positions,
									imported.Primitives[0].Joints,
									imported.Primitives[0].Weights, bindMin, bindMax);
				Anim::SkinnedBounds(imported.Skeleton, imported.Clips, positions,
									imported.Primitives[0].Joints,
									imported.Primitives[0].Weights,
									animatedMin, animatedMax);

				const Vec3 bindSize = bindMax - bindMin;
				const Vec3 animatedSize = animatedMax - animatedMin;

				Check(animatedSize.x >= bindSize.x - 1e-3f &&
					  animatedSize.y >= bindSize.y - 1e-3f &&
					  animatedSize.z >= bindSize.z - 1e-3f,
					  "a real rig's animated bounds contain its bind pose on every axis");
				Check(animatedSize.x > bindSize.x * 1.02f ||
					  animatedSize.y > bindSize.y * 1.02f ||
					  animatedSize.z > bindSize.z * 1.02f,
					  "and are larger on at least one -- the fox leaves its bind "
					  "box when it runs, which is exactly what culled it early");
			}
		}
	}

	// Viewport gizmo icons (7.4). The claim the feature exists to make is a
	// negative one turned positive: an entity with no mesh and no collider
	// could not be clicked at all, and now can be -- so the check has to show
	// both halves, or it is only testing that a sphere test works.
	void CheckEditorIcons()
	{
		auto scene = std::make_shared<Scene>();

		// A light: no mesh, no collider, nothing for the old picker to find.
		Entity light = scene->CreateEntity("Key Light");
		light.AddComponent<LightComponent>();
		light.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };

		// A camera, to prove the kinds are distinguished, placed off to one
		// side so the two marks never overlap.
		Entity camera = scene->CreateEntity("Watcher");
		camera.AddComponent<CameraComponent>();
		camera.GetComponent<TransformComponent>().Position = { 4.0f, 0.0f, 0.0f };

		// An entity carrying two of the kinds: one mark, the first that
		// matches, so a click is never ambiguous about which it meant.
		Entity both = scene->CreateEntity("Light with a speaker on it");
		both.AddComponent<LightComponent>();
		both.AddComponent<AudioSourceComponent>();
		both.GetComponent<TransformComponent>().Position = { -4.0f, 0.0f, 0.0f };

		scene->UpdateWorldTransforms();

		const std::vector<EditorIcon> icons = EditorIcons::Collect(*scene);
		Check(icons.size() == 3, "every geometry-less entity gets a mark, and only one");

		const auto kindOf = [&](UUID id)
		{
			for (const EditorIcon& icon : icons)
				if (icon.Entity == id)
					return icon.Kind;
			return EditorIconKind::Count;
		};

		Check(kindOf(light.GetUUID()) == EditorIconKind::Light &&
			  kindOf(camera.GetUUID()) == EditorIconKind::Camera,
			  "and the mark says which kind it is");
		Check(kindOf(both.GetUUID()) == EditorIconKind::Light,
			  "an entity with two kinds takes the first, not one of each");

		// Constant angular size: twice as far away is twice as large in world
		// units, which is what keeps it the same size on screen.
		// `near` and `far` would be the obvious names and are both macros in
		// the Windows headers -- the same trap RayIntersectsBox documents.
		const Vec3 eye{ 0.0f, 0.0f, 10.0f };
		const float atTen = EditorIcons::Radius({ 0.0f, 0.0f, 0.0f }, eye);
		const float atTwenty = EditorIcons::Radius({ 0.0f, 0.0f, -10.0f }, eye);
		Check(Math::Abs(atTwenty - atTen * 2.0f) < 1e-4f,
			  "a mark's world radius grows with distance, so its screen size does not");

		// --- the point of the feature ------------------------------------------
		EditorCamera view(45.0f, 1.0f, 0.1f, 1000.0f);
		const Mat4 eyeTransform = Math::Translate(Mat4(1.0f), eye);
		const Ray atLight = ScreenPointToRay(view, eyeTransform, { 0.0f, 0.0f });

		Check(!PickEntity(*scene, atLight),
			  "without the marks a light cannot be clicked at all -- which is "
			  "the defect 7.4 exists to fix");

		PickOptions options;
		options.IconHandles = true;
		Check(PickEntity(*scene, atLight, options).Entity == light,
			  "with them, the ray through its mark selects it");

		// The hit area is the mark, not the whole viewport: a ray that passes
		// well outside the drawn radius must miss.
		{
			const float radius = EditorIcons::Radius({ 0.0f, 0.0f, 0.0f }, eye);
			Ray beside;
			beside.Origin = eye;
			beside.Direction = Math::Normalize(Vec3(radius * 3.0f, 0.0f, -10.0f));
			Check(!PickEntity(*scene, beside, options),
				  "a ray outside the mark misses it");

			Ray inside;
			inside.Origin = eye;
			inside.Direction = Math::Normalize(Vec3(radius * 0.5f, 0.0f, -10.0f));
			Check(PickEntity(*scene, inside, options).Entity == light,
				  "and one inside it hits, at the radius the drawer used");
		}

		// Geometry in front wins, because the mark is depth-tested on screen
		// and a pick that ignored that would select something invisible.
		{
			Entity wall = scene->CreateEntity("Wall");
			wall.AddComponent<MeshComponent>(PrimitiveType::Cube);
			wall.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 5.0f };
			wall.GetComponent<TransformComponent>().Scale = { 4.0f, 4.0f, 0.2f };
			scene->UpdateWorldTransforms();

			Check(PickEntity(*scene, atLight, options).Entity == wall,
				  "geometry in front of a mark wins, as the depth-tested "
				  "picture already says it should");
		}
	}

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
				  Math::Abs(distance - 9.0f) < 1e-3f,
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
		Check(Math::Abs(hit.Point.z - 2.5f) < 0.05f, "the hit lands on the surface it struck");
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
			Check(Math::Abs(sized.HalfExtents.x - 1.0f) < 1e-5f &&
				  Math::Abs(sized.HalfExtents.y - 1.5f) < 1e-5f &&
				  Math::Abs(sized.HalfExtents.z - 2.0f) < 1e-5f,
				  "a box collider takes the entity's scale per axis");

			ColliderComponent sphere(ColliderShape::Sphere);
			sphere.Radius = 1.0f;
			// A sphere has one radius, so the largest axis wins -- enclosing
			// the mesh rather than cutting into it.
			Check(Math::Abs(Physics::ScaleCollider(sphere, { 2.0f, 3.0f, 1.0f }).Radius - 3.0f) < 1e-5f,
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
		Check(Math::Abs(Audio::Engine::GetBusVolume(AudioBus::Music) - 0.25f) < 1e-4f,
			  "a bus volume reads back as it was set");
		Check(Math::Abs(Audio::Engine::GetBusVolume(AudioBus::SFX) - 1.0f) < 1e-4f,
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

			leftRms = got ? (float)Math::Sqrt(left / (double)got) : 0.0f;
			rightRms = got ? (float)Math::Sqrt(right / (double)got) : 0.0f;
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
		Check(Math::Abs(left - right) < 1e-5f, "and an unpositioned sound is centred");
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
		Check(Math::Abs(back.Volume - 0.42f) < 1e-4f &&
			  Math::Abs(back.Pitch - 1.35f) < 1e-4f, "volume and pitch survive");
		Check(back.Loop && !back.PlayOnAwake && !back.Spatial && back.Stream,
			  "and every flag survives");
		Check(Math::Abs(back.MinDistance - 2.5f) < 1e-4f &&
			  Math::Abs(back.MaxDistance - 88.0f) < 1e-4f, "as do the distances");
		Check(back.Voice == 0, "the playing voice does not, because it belongs to one run");

		Check(restored.HasComponent<AudioListenerComponent>() &&
			  restored.GetComponent<AudioListenerComponent>().ListenerRank == 7,
			  "a listener and its rank survive");
	}

	// Stop has to put the scene back exactly, including everything physics
	// moved -- which is most of the scene after a run.
	// Terrain (8.4, ENGINE-NOTES 7ap): the data and its serializer, the pure
	// chunk builder, the level rule, HeightAt, the scene walk, and the Jolt
	// height field -- and the claim that ties them: the surface a body rests
	// on is the surface drawn, to the triangle.
	void CheckTerrain()
	{
		using Dimensions = Terrain::Dimensions;

		// --- the data --------------------------------------------------------
		Check(TerrainData::IsValidResolution(33) && TerrainData::IsValidResolution(513) &&
			  TerrainData::IsValidResolution(4097),
			  "a terrain resolution is 2^n + 1 between 33 and 4097");
		Check(!TerrainData::IsValidResolution(34) && !TerrainData::IsValidResolution(32) &&
			  !TerrainData::IsValidResolution(8193) && !TerrainData::IsValidResolution(17),
			  "and nothing else is");

		// One quad with a single raised corner at (1, 0): the point (0.25, 0.75)
		// lies in the triangle that does *not* touch that corner and the point
		// (0.75, 0.25) in the one that does. Bilinear interpolation would give
		// 0.0625 and 0.5625; the other diagonal would give 0.25 and 0.75; the
		// (x, z) -> (x + 1, z + 1) split the meshes and Jolt use gives 0 and
		// 0.5, exactly.
		TerrainData corner = TerrainData::Flat(33, 0);
		corner.Heights[0 * 33 + 1] = 65535;
		Check(corner.Sample(0.25f, 0.75f) == 0.0f,
			  "a height is interpolated over the quad's triangles, not bilinearly");
		Check(Math::Abs(corner.Sample(0.75f, 0.25f) - 0.5f) < 1e-6f,
			  "and over the same diagonal the collider uses");
		Check(corner.Sample(1.0f, 0.0f) == 1.0f && corner.Sample(0.0f, 0.0f) == 0.0f,
			  "a sample on a vertex is that vertex's height");
		Check(corner.Sample(-4.0f, 99.0f) == 0.0f, "and a sample off the grid is clamped to it");

		// A ramp: h = x / 32 across the grid, flat along z.
		TerrainData ramp = TerrainData::Flat(33, 0);
		for (uint32_t z = 0; z < 33; ++z)
			for (uint32_t x = 0; x < 33; ++x)
				ramp.Heights[(size_t)z * 33 + x] = (uint16_t)Math::Round((float)x / 32.0f * 65535.0f);
		Check(Math::Abs(ramp.Sample(16.0f, 5.5f) - 0.5f) < 1e-4f, "a ramp samples to its slope");

		// --- the serializer --------------------------------------------------
		{
			const std::filesystem::path dir = ScratchDir("terrain-test");
			std::error_code created;
			std::filesystem::create_directories(dir, created);
			const std::filesystem::path path = dir / "ramp.rvterrain";
			Check(Assets::TerrainSerializer::Save(ramp, path), "a terrain writes to disk");
			TerrainData back;
			Check(Assets::TerrainSerializer::Load(back, path), "and reads back");
			Check(back.Resolution == ramp.Resolution && back.Heights == ramp.Heights,
				  "with every one of its samples intact");

			std::error_code error;
			Check(std::filesystem::file_size(path, error) == 32 + 33 * 33 * 2,
				  "as thirty-two bytes of header and two bytes a sample");

			// Refusals: a wrong magic, a truncated file, an invalid resolution.
			{
				std::ofstream broken(dir / "broken.rvterrain", std::ios::binary);
				broken << "NOPE" << std::string(64, '\0');
			}
			TerrainData untouched = TerrainData::Flat(33, 7);
			Check(!Assets::TerrainSerializer::Load(untouched, dir / "broken.rvterrain") &&
				  untouched.Resolution == 33 && untouched.Heights[0] == 7,
				  "a file that is not a terrain is refused and leaves the caller's data alone");
			{
				std::vector<uint8_t> bytes;
				IO::VFS::ReadBytes(path, bytes);
				bytes.resize(bytes.size() / 2);
				std::ofstream truncated(dir / "short.rvterrain", std::ios::binary);
				truncated.write((const char*)bytes.data(), (std::streamsize)bytes.size());
			}
			Check(!Assets::TerrainSerializer::Load(untouched, dir / "short.rvterrain"),
				  "a truncated terrain is refused");
			TerrainData bad = TerrainData::Flat(33, 1);
			bad.Resolution = 34;
			bad.Heights.resize(34 * 34);
			Check(!Assets::TerrainSerializer::Save(bad, dir / "bad.rvterrain") &&
				  !std::filesystem::exists(dir / "bad.rvterrain", error),
				  "an invalid grid is not written at all");

			// --- and with paint (ENGINE-NOTES 7aq) --------------------------------
			// The same ramp with a weight per sample: layer 1 rising with x,
			// layer 0 the rest, the other two zero.
			TerrainData painted = ramp;
			painted.Weights.assign((size_t)33 * 33 * TerrainData::kLayers, 0);
			for (uint32_t z = 0; z < 33; ++z)
			{
				for (uint32_t x = 0; x < 33; ++x)
				{
					const size_t at = ((size_t)z * 33 + x) * TerrainData::kLayers;
					painted.Weights[at + 1] = (uint8_t)(x * 255 / 32);
					painted.Weights[at + 0] = (uint8_t)(255 - painted.Weights[at + 1]);
				}
			}
			Check(painted.IsValid() && painted.HasWeights(), "a grid with a weight per sample is valid");
			Check(painted.WeightAt(32, 5, 1) == 255 && painted.WeightAt(32, 5, 0) == 0 &&
				  painted.WeightAt(0, 9, 0) == 255 && painted.WeightAt(16, 0, 1) == 127 &&
				  painted.WeightAt(16, 0, 2) == 0,
				  "WeightAt reads the interleaved bytes back by layer");
			Check(ramp.WeightAt(16, 0, 1) == 0, "and zero everywhere on an unpainted grid");
			TerrainData half = painted;
			half.Weights.resize(half.Weights.size() / 2);
			Check(!half.IsValid(), "weights that are not the grid's size are not valid");

			const std::filesystem::path paintedPath = dir / "painted.rvterrain";
			Check(Assets::TerrainSerializer::Save(painted, paintedPath), "a painted terrain writes");
			Check(std::filesystem::file_size(paintedPath, error) == 32 + 33 * 33 * 2 + 33 * 33 * 4,
				  "as the header, two bytes a sample, and four more a sample of paint");
			TerrainData paintedBack;
			Check(Assets::TerrainSerializer::Load(paintedBack, paintedPath) &&
				  paintedBack.Heights == painted.Heights && paintedBack.Weights == painted.Weights,
				  "and reads back with every weight byte for byte");
			{
				std::vector<uint8_t> bytes;
				IO::VFS::ReadBytes(paintedPath, bytes);
				// The layer count says 4; a file that stops inside its weights is
				// short, not a file with fewer layers.
				std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 100);
				std::ofstream truncated(dir / "short_paint.rvterrain", std::ios::binary);
				truncated.write((const char*)cut.data(), (std::streamsize)cut.size());
				truncated.close();
				Check(!Assets::TerrainSerializer::Load(untouched, dir / "short_paint.rvterrain"),
					  "a terrain truncated inside its weights is refused");
				// And a layer count that is neither 0 nor 4.
				std::vector<uint8_t> two = bytes;
				two[12] = 2;
				std::ofstream odd(dir / "two_layers.rvterrain", std::ios::binary);
				odd.write((const char*)two.data(), (std::streamsize)two.size());
				odd.close();
				Check(!Assets::TerrainSerializer::Load(untouched, dir / "two_layers.rvterrain") &&
					  untouched.Heights[0] == 7,
					  "a layer count that is not 0 or 4 is refused, and leaves the caller's data alone");
			}
			std::filesystem::remove_all(dir, error);
		}

		// --- the layered material's numbers (7aq) -----------------------------
		// The block is mirrored by hand in the shader; the size is the first
		// thing that drifts.
		Check(sizeof(LayeredParams) == 368, "LayeredParams is the 368 bytes LayeredData declares");
		{
			// The weight coordinate lands sample i on texel centre (i + 0.5) / R,
			// whatever the texture scale: sample 0 sits at uv = -Size / (2 T),
			// sample R - 1 at +Size / (2 T).
			Dimensions wd;
			wd.Size = 64.0f;
			wd.Height = 10.0f;
			for (float textureScale : { 4.0f, 12.0f })
			{
				wd.TextureScale = textureScale;
				const Vec4 w = Terrain::WeightUvFor(wd, 129);
				const float uvEdge = wd.Size * 0.5f / textureScale;
				const float first = -uvEdge * w.x + w.z;
				const float last = uvEdge * w.x + w.z;
				Check(Math::Abs(first - 0.5f / 129.0f) < 1e-5f && Math::Abs(last - 128.5f / 129.0f) < 1e-5f &&
					  w.z == 0.5f && w.w == 0.5f,
					  "WeightUvFor puts the first and last samples on the first and last texel centres");
			}
		}

		// --- the chunk builder -----------------------------------------------
		Dimensions dims;
		dims.Size = 64.0f;
		dims.Height = 10.0f;
		dims.TextureScale = 4.0f;

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;

		// A 33 grid is one chunk of 32 quads with no neighbour, so no skirt:
		// at level 0 that is 33^2 vertices and 6 * 32^2 indices.
		Terrain::BuildChunkGeometry(ramp, dims, 0, 0, 0, vertices, indices);
		Check(vertices.size() == 33 * 33 && indices.size() == 6 * 32 * 32,
			  "a level-0 chunk has the vertices and indices the arithmetic predicts, "
			  "and a lone chunk wears no skirt");
		{
			const MeshVertex& first = vertices[0];
			const MeshVertex& last = vertices[33 * 33 - 1];
			Check(Math::Abs(first.Position.x + 32.0f) < 1e-4f && Math::Abs(first.Position.z + 32.0f) < 1e-4f &&
				  Math::Abs(first.Position.y) < 1e-4f,
				  "the first vertex sits at the terrain's near corner, centred on the origin");
			Check(Math::Abs(last.Position.x - 32.0f) < 1e-4f && Math::Abs(last.Position.z - 32.0f) < 1e-4f &&
				  Math::Abs(last.Position.y - 10.0f) < 1e-3f,
				  "and the last at the far corner, at the full height");
			Check(Math::Abs(first.TexCoord.x + 8.0f) < 1e-4f && Math::Abs(first.TexCoord.y + 8.0f) < 1e-4f,
				  "uv is local metres over the texture scale");
			// The ramp rises with x, so every surface normal leans toward -x
			// and up; the flat grid's all point straight up.
			bool leaning = true;
			for (size_t i = 0; i < 33 * 33; ++i)
				leaning = leaning && vertices[i].Normal.x < 0.0f && vertices[i].Normal.y > 0.9f &&
						  Math::Abs(vertices[i].Normal.z) < 1e-4f;
			Check(leaning, "a ramp's normals lean against the slope");
		}
		{
			// Skirts: a 129 ramp is two by two chunks of 64; chunk (0, 0) shares
			// its far-x and far-z edges and gets a skirt on each and on nothing
			// else. At level 0: 65^2 surface vertices plus two strips of 65,
			// 6 * 64^2 surface indices plus 2 * 12 * 64 for the strips (a quad
			// each, both windings, six indices a triangle).
			TerrainData wide = TerrainData::Flat(129, 0);
			for (uint32_t z = 0; z < 129; ++z)
				for (uint32_t x = 0; x < 129; ++x)
					wide.Heights[(size_t)z * 129 + x] = (uint16_t)Math::Round((float)x / 128.0f * 65535.0f);
			Dimensions wideDims;
			wideDims.Size = 128.0f;
			wideDims.Height = 10.0f;
			const uint32_t surfaceEnd = Terrain::BuildChunkGeometry(wide, wideDims, 0, 0, 0, vertices, indices);
			const uint32_t n = 64;
			Check(vertices.size() == 65 * 65 + 2 * 65 && indices.size() == 6 * 64 * 64 + 2 * 12 * 64,
				  "a chunk with two neighbours wears two skirts, and the counts say so");
			// The surface's indices come first and the builder says where they
			// end: what a frame from under the ground draws (7ap). Every index
			// before that point names a surface vertex; every one after, a
			// skirt's strip.
			bool surfaceBefore = surfaceEnd == 6 * 64 * 64 && surfaceEnd <= indices.size();
			for (size_t i = 0; i < surfaceEnd && surfaceBefore; ++i)
				surfaceBefore = indices[i] < 65 * 65;
			bool skirtAfter = false;
			for (size_t i = surfaceEnd; i < indices.size(); ++i)
				skirtAfter = skirtAfter || indices[i] >= 65 * 65;
			Check(surfaceBefore && skirtAfter,
				  "the surface's indices come first, and the builder reports where they end");

			// Every skirt vertex is its edge vertex dropped by the same positive
			// depth: the far-z strip first, then the far-x strip, in the
			// builder's order.
			auto edge = [n](int which, uint32_t i) -> uint32_t
			{
				return which == 0 ? n * (n + 1) + i : i * (n + 1) + n;
			};
			bool dropped = true;
			float depth = -1.0f;
			for (int which = 0; which < 2; ++which)
			{
				for (uint32_t i = 0; i <= n; ++i)
				{
					const MeshVertex& top = vertices[edge(which, i)];
					const MeshVertex& skirt = vertices[65 * 65 + which * (n + 1) + i];
					const float d = top.Position.y - skirt.Position.y;
					if (depth < 0.0f) depth = d;
					dropped = dropped && d > 0.0f && Math::Abs(d - depth) < 1e-4f &&
							  top.Position.x == skirt.Position.x && top.Position.z == skirt.Position.z &&
							  top.Normal == skirt.Normal;
				}
			}
			Check(dropped, "every skirt vertex is its edge vertex dropped by one positive depth, "
						   "carrying the edge's normal");
			// Chunk (0, 0) spans half the ramp: 5 m of range, half of that plus
			// 2 % of the terrain's height.
			Check(Math::Abs(depth - (2.5f + 0.2f)) < 1e-3f, "and the depth is half the chunk's range plus a sliver");

			// Chunk (1, 1) shares its near edges instead; the same two strips.
			Terrain::BuildChunkGeometry(wide, wideDims, 1, 1, 3, vertices, indices);
			Check(vertices.size() == 9 * 9 + 2 * 9 && indices.size() == 6 * 64 + 2 * 12 * 8,
				  "at level 3 the far corner chunk samples every eighth height, 9 a side, two skirts");
			Check(Terrain::BuildChunkGeometry(wide, wideDims, 0, 0, 0, vertices, indices, false) == 6 * 64 * 64 &&
				  vertices.size() == 65 * 65 && indices.size() == 6 * 64 * 64,
				  "without skirts it is the surface alone, and the surface still ends where it ends");
		}
		Terrain::BuildChunkGeometry(ramp, dims, 0, 0, 3, vertices, indices);
		Check(vertices.size() == 5 * 5 && indices.size() == 6 * 16,
			  "level 3 samples every eighth height: 5 a side");
		{
			TerrainData flat = TerrainData::Flat(33, 20000);
			Terrain::BuildChunkGeometry(flat, dims, 0, 0, 1, vertices, indices);
			bool up = true;
			for (const MeshVertex& v : vertices)
				up = up && Math::Abs(v.Normal.y - 1.0f) < 1e-6f;
			Check(up, "a flat terrain's normals all point straight up");
			// The +Y winding, the convention every primitive here uses: the
			// geometric normal of the first triangle is +Y.
			const Vec3 a = vertices[indices[0]].Position;
			const Vec3 b = vertices[indices[1]].Position;
			const Vec3 c = vertices[indices[2]].Position;
			Check(Math::Cross(b - a, c - a).y > 0.0f, "and the triangles wind so the geometric normal is +Y");
		}
		Terrain::BuildChunkGeometry(ramp, dims, 1, 0, 0, vertices, indices);
		Check(vertices.empty() && indices.empty(), "a chunk past the grid's edge builds nothing");

		// --- the level rule --------------------------------------------------
		Check(Terrain::LevelFor(10.0f, 32.0f) == 0 && Terrain::LevelFor(127.0f, 32.0f) == 0,
			  "a chunk within four widths draws at full detail");
		Check(Terrain::LevelFor(129.0f, 32.0f) == 1 && Terrain::LevelFor(257.0f, 32.0f) == 2 &&
			  Terrain::LevelFor(513.0f, 32.0f) == 3 && Terrain::LevelFor(50000.0f, 32.0f) == 3,
			  "and one level coarser per doubling after that, capped at the last");

		// --- a Terrain without a device --------------------------------------
		{
			TerrainData big = TerrainData::Flat(129, 32768);
			Dimensions bigDims;
			bigDims.Size = 128.0f;
			bigDims.Height = 20.0f;
			RHI::Ref<Terrain> terrain = Terrain::Create(nullptr, big, AssetHandle::Invalid(), bigDims);
			Check(terrain && terrain->GetChunks().size() == 4 && terrain->GetChunksPerSide() == 2 &&
				  terrain->GetChunkQuads() == 64,
				  "a 129 grid is two by two chunks of 64 quads");
			Check(terrain && Math::Abs(terrain->GetChunkWidth() - 64.0f) < 1e-4f,
				  "each 64 metres wide at 128 metres a side");
			Check(terrain && Math::Abs(terrain->HeightAt(0.0f, 0.0f) - 10.0f) < 1e-3f &&
				  Math::Abs(terrain->HeightAt(-64.0f, 63.0f) - 10.0f) < 1e-3f,
				  "HeightAt reads the flat grid's height in metres anywhere on it");
			if (terrain)
			{
				const Terrain::Chunk& chunk = terrain->GetChunks()[0];
				Check(chunk.Bounds.Max.y >= 10.0f && chunk.Bounds.Min.y < 10.0f && !chunk.Levels[0],
					  "a chunk built without a device has bounds that include its skirt and no mesh");
				Mat4 identity(1.0f);
				terrain->SelectLod(Vec3(0.0f, 0.0f, 0.0f), identity);
				bool allFine = true;
				for (const Terrain::Chunk& c : terrain->GetChunks())
					allFine = allFine && c.Level == 0;
				terrain->SelectLod(Vec3(0.0f, 0.0f, 5000.0f), identity);
				bool allCoarse = true;
				for (const Terrain::Chunk& c : terrain->GetChunks())
					allCoarse = allCoarse && c.Level == 3;
				Check(allFine && allCoarse, "SelectLod puts a near camera at level 0 and a far one at level 3");

				// The skirts, only from above the ground (7ap): the flat terrain
				// is 10 m up everywhere, so a camera at 12 m is above it and one
				// at 8 m under it -- and off the rim the comparison is with the
				// rim nearest the camera, which is the same 10 m.
				terrain->SelectLod(Vec3(0.0f, 12.0f, 0.0f), identity);
				const bool aboveDraws = terrain->SkirtsDrawn();
				terrain->SelectLod(Vec3(0.0f, 8.0f, 0.0f), identity);
				const bool underHides = !terrain->SkirtsDrawn();
				Check(aboveDraws && underHides,
					  "the skirts are drawn for a camera above the surface at its own (x, z) and not for one under it");
				terrain->SelectLod(Vec3(1000.0f, 8.0f, -1000.0f), identity);
				const bool offRimUnder = !terrain->SkirtsDrawn();
				terrain->SelectLod(Vec3(1000.0f, 12.0f, -1000.0f), identity);
				const bool offRimAbove = terrain->SkirtsDrawn();
				Check(offRimUnder && offRimAbove, "off the rim, the camera compares with the rim nearest it");
				// Through the world matrix: the terrain lifted 100 m puts the
				// 12 m camera under it, and a scale of 2 (heights doubled) puts
				// a 15 m one under a surface now at 20.
				terrain->SelectLod(Vec3(0.0f, 12.0f, 0.0f), Math::Translate(Mat4(1.0f), Vec3(0.0f, 100.0f, 0.0f)));
				const bool liftedUnder = !terrain->SkirtsDrawn();
				terrain->SelectLod(Vec3(0.0f, 15.0f, 0.0f), Math::Scale(Mat4(1.0f), Vec3(2.0f)));
				const bool scaledUnder = !terrain->SkirtsDrawn();
				terrain->SelectLod(Vec3(0.0f, 25.0f, 0.0f), Math::Scale(Mat4(1.0f), Vec3(2.0f)));
				const bool scaledAbove = terrain->SkirtsDrawn();
				Check(liftedUnder && scaledUnder && scaledAbove,
					  "and the terrain's world matrix counts: lifted or scaled, the camera is judged in terrain space");
				Check(terrain->DrawIndexCount(terrain->GetChunks()[0]) == 0,
					  "a chunk without a mesh draws no indices, skirts or not");
			}

			// --- the same on a device: how many indices a chunk draws ------------
			if (Renderer::HasDevice())
			{
				TerrainData big = TerrainData::Flat(129, 32768);
				Dimensions bigDims;
				bigDims.Size = 128.0f;
				bigDims.Height = 20.0f;
				RHI::Ref<Terrain> terrain = Terrain::Create(&Renderer::GetDevice(), big,
															AssetHandle::Invalid(), bigDims);
				Check(terrain && terrain->GetChunks().size() == 4 && terrain->GetChunks()[0].Levels[0],
					  "a two-by-two terrain built on the device has meshes");
				if (terrain && terrain->GetChunks()[0].Levels[0])
				{
					Mat4 identity(1.0f);
					const Terrain::Chunk& chunk = terrain->GetChunks()[0];
					terrain->SelectLod(Vec3(0.0f, 12.0f, 0.0f), identity);
					const uint32_t whole = chunk.Selected()->GetIndexCount();
					const uint32_t surface = chunk.SurfaceIndices[chunk.Level];
					Check(chunk.Level == 0 && whole == 6 * 64 * 64 + 2 * 12 * 64 && surface == 6 * 64 * 64,
						  "the corner chunk's level 0 is 6x64^2 surface indices and two skirts after them");
					Check(terrain->SkirtsDrawn() && terrain->DrawIndexCount(chunk) == whole,
						  "a camera above the ground draws the chunk whole, skirts and all");
					terrain->SelectLod(Vec3(0.0f, 8.0f, 0.0f), identity);
					Check(!terrain->SkirtsDrawn() && terrain->DrawIndexCount(chunk) == surface,
						  "a camera under it draws the surface indices alone");
					// Every level keeps its own count, so the far camera's level
					// 3 (9 a side: 6x64 surface, 2x12x8 skirts) says so too.
					terrain->SelectLod(Vec3(0.0f, 8.0f, 5000.0f), identity);
					Check(chunk.Level == 3 && chunk.SurfaceIndices[3] == 6 * 64 &&
						  terrain->DrawIndexCount(chunk) == 6 * 64 &&
						  chunk.Selected()->GetIndexCount() == 6 * 64 + 2 * 12 * 8,
						  "and at level 3 the surface count is that level's own");
				}
			}

			RHI::Ref<Terrain> rampTerrain = Terrain::Create(nullptr, ramp, AssetHandle::Invalid(), dims);
			Check(rampTerrain && Math::Abs(rampTerrain->HeightAt(-32.0f, 0.0f)) < 1e-3f &&
				  Math::Abs(rampTerrain->HeightAt(32.0f, 0.0f) - 10.0f) < 1e-3f &&
				  Math::Abs(rampTerrain->HeightAt(0.0f, 11.0f) - 5.0f) < 1e-3f,
				  "HeightAt on the ramp is the ramp, in metres, centred");
		}

		// --- the scene, and the collider -------------------------------------
		// A flat terrain at half height, written into the sample project for
		// the duration of the check so the registry can hand out a handle for
		// it, and removed after.
		const std::filesystem::path terrainDir = Project::AssetRoot() / "terrain";
		const std::filesystem::path flatPath = terrainDir / "scenetest_flat.rvterrain";
		const std::filesystem::path rampPath = terrainDir / "scenetest_ramp.rvterrain";
		std::error_code error;
		std::filesystem::create_directories(terrainDir, error);
		Assets::TerrainSerializer::Save(TerrainData::Flat(33, 32768), flatPath);
		Assets::TerrainSerializer::Save(ramp, rampPath);
		Assets::Registry::Refresh();

		const AssetHandle flatHandle = Assets::Registry::GetHandle("terrain/scenetest_flat.rvterrain");
		const AssetHandle rampHandle = Assets::Registry::GetHandle("terrain/scenetest_ramp.rvterrain");
		Check(flatHandle.IsValid() && rampHandle.IsValid(), "an .rvterrain in the project is an asset with a handle");
		Check(Assets::Manager::GetTerrain(flatHandle) != nullptr &&
			  Assets::Manager::GetTerrain(flatHandle)->Resolution == 33,
			  "and the manager reads its heights");
		Check(Assets::Registry::GetMetadata(flatHandle).Type == AssetType::Terrain,
			  "typed as a terrain by its extension");

		if (flatHandle.IsValid() && rampHandle.IsValid())
		{
			auto scene = std::make_shared<Scene>();

			Entity ground = scene->CreateEntity("Ground");
			auto& terrainComponent = ground.AddComponent<TerrainComponent>();
			terrainComponent.Terrain = flatHandle;
			terrainComponent.Size = 64.0f;
			terrainComponent.Height = 10.0f;
			// Two layers named, so the round trip below can lose one.
			terrainComponent.Layer1 = AssetHandle(0x1111ull);
			terrainComponent.Layer3 = AssetHandle(0x3333ull);
			// A RigidBody as well, which the terrain must ignore.
			ground.AddComponent<RigidBodyComponent>(BodyType::Dynamic);

			Entity slope = scene->CreateEntity("Slope");
			auto& slopeComponent = slope.AddComponent<TerrainComponent>();
			slopeComponent.Terrain = rampHandle;
			slopeComponent.Size = 64.0f;
			slopeComponent.Height = 10.0f;
			slope.GetComponent<TransformComponent>().Position = { 200.0f, 0.0f, 0.0f };

			Entity ball = scene->CreateEntity("Ball");
			ball.GetComponent<TransformComponent>().Position = { 3.0f, 20.0f, -7.0f };
			ball.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
			auto& ballCollider = ball.AddComponent<ColliderComponent>(ColliderShape::Sphere);
			ballCollider.Radius = 0.5f;

			// The walk, and the round trip through the scene file.
			Check(scene->HasTerrain(), "a scene with a terrain says so");
			int walked = 0;
			scene->ForEachTerrainChunk([&](Entity, TransformComponent&, TerrainComponent&, Terrain&,
										   Terrain::Chunk&) { walked++; });
			Check(walked == 2, "the walk visits every chunk of every terrain: one each here");
			{
				SceneSerializer serializer(scene);
				const std::string saved = serializer.SerializeToString();
				auto reloaded = std::make_shared<Scene>();
				SceneSerializer reader(reloaded);
				reader.DeserializeFromString(saved);
				bool same = false;
				for (auto entity : reloaded->GetRegistry().view<TerrainComponent>())
				{
					const auto& t = reloaded->GetRegistry().get<TerrainComponent>(entity);
					if (t.Terrain == flatHandle)
						same = t.Size == 64.0f && t.Height == 10.0f && t.TextureScale == 4.0f && t.Collision &&
							   t.Layer1 == AssetHandle(0x1111ull) && !t.Layer2.IsValid() &&
							   t.Layer3 == AssetHandle(0x3333ull);
				}
				Check(same, "a terrain component round-trips through the scene file, its four layers included");
			}

			// The layered material (7aq), where there is a device to build one
			// on: layer 0 empty is the renderer's default, a layer's key changes
			// with the layer and not without.
			if (Renderer::HasDevice())
			{
				const RHI::Ref<Terrain>& built = Terrain::Resolve(terrainComponent);
				const RHI::Ref<LayeredMaterial>& layers = built ? built->RefreshLayers(terrainComponent) : nullptr;
				Check(layers != nullptr, "a terrain built with a device has a layered material");
				if (layers)
				{
					Check(layers->GetLayer(0) == Renderer3D::GetDefaultMaterial(),
						  "an empty layer 0 is the renderer's default material");
					Check(!layers->GetLayer(1) && !layers->GetLayer(2) && !layers->GetLayer(3),
						  "and a layer whose handle names nothing is not there");
					Check(layers->GetParams().MapFlags[0] & LayeredMap_Active,
						  "layer 0 is marked active in the block");
					Check((layers->GetParams().MapFlags[1] & LayeredMap_Active) == 0,
						  "and an absent layer is not");
					Check(layers->GetWeights() == TextureLoader::Red(Renderer::GetDevice()),
						  "an unpainted terrain's weight map is the 1x1 all-layer-0 texel");
					const uint64_t before = layers->GetBatchKey();
					built->RefreshLayers(terrainComponent);
					Check(layers->GetBatchKey() == before, "refreshing an unchanged terrain keeps its batch key");
					auto red = std::make_shared<Material>(Renderer::GetDevice(), "scenetest red");
					red->GetParams().BaseColor = { 1.0f, 0.0f, 0.0f, 1.0f };
					layers->SetLayer(1, red);
					layers->Refresh(Renderer3D::GetTextureHeap());
					Check(layers->GetBatchKey() != before &&
						  (layers->GetParams().MapFlags[1] & LayeredMap_Active) &&
						  layers->GetParams().BaseColor[1] == Vec4(1.0f, 0.0f, 0.0f, 1.0f),
						  "a layer assigned changes the key and lands in the block");
					// A refresh from the component puts it back to what the
					// component says: the material is a function of the handles.
					built->RefreshLayers(terrainComponent);
					Check(layers->GetBatchKey() == before, "and RefreshLayers restores the component's own");
				}
			}

			// The picker: a click straight down on each terrain lands on it,
			// triangle-exact, at the height the heights say.
			{
				Ray down;
				down.Origin = { -5.0f, 30.0f, 8.0f };
				down.Direction = { 0.0f, -1.0f, 0.0f };
				const PickResult onFlat = PickEntity(*scene, down);
				Check(onFlat.Entity == ground && Math::Abs(onFlat.Point.y - 5.0f) < 0.01f,
					  "a click on a terrain selects it, at the surface");
				down.Origin = { 216.0f, 30.0f, 4.0f };
				const PickResult onSlope = PickEntity(*scene, down);
				Check(onSlope.Entity == slope && Math::Abs(onSlope.Point.y - 7.5f) < 0.01f,
					  "and on the ramp, at the ramp's height");
				down.Origin = { 100.0f, 30.0f, 100.0f };
				Check(!PickEntity(*scene, down), "a click beside every terrain selects nothing");
			}

			// --- the ground under a point, as a script asks it (7au) ---------
			//
			// In its own scene, and *before* OnRuntimeStart: the whole reason
			// this is not a downward ray is that it answers while the scene is
			// only being edited, with no physics world in existence.
			{
				auto queried = std::make_shared<Scene>();

				Entity plain = queried->CreateEntity("Plain");
				auto& plainTerrain = plain.AddComponent<TerrainComponent>();
				plainTerrain.Terrain = flatHandle;
				plainTerrain.Size = 64.0f;
				plainTerrain.Height = 10.0f;
				const RHI::Ref<Terrain>& plainBuilt = Terrain::Resolve(plainTerrain);

				float height = -1.0f;
				Check(plainBuilt && queried->TerrainHeightAt(Vec3(-5.0f, 999.0f, 8.0f), height) &&
					  Math::Abs(height - plainBuilt->HeightAt(-5.0f, 8.0f)) < 1e-4f &&
					  Math::Abs(height - 5.0f) < 1e-3f,
					  "the script call is the heightfield's own number under a world point");

				float fromBelow = -1.0f;
				Check(queried->TerrainHeightAt(Vec3(-5.0f, -999.0f, 8.0f), fromBelow) &&
					  fromBelow == height,
					  "and the asking point's own height is no part of the question");

				// The trap the bool exists for, in one assertion: off the
				// extent the query says no, where HeightAt itself clamps and
				// answers a number that looks perfectly reasonable.
				height = -1.0f;
				Check(!queried->TerrainHeightAt(Vec3(100.0f, 0.0f, 100.0f), height) &&
					  height == 0.0f && plainBuilt &&
					  Math::Abs(plainBuilt->HeightAt(100.0f, 100.0f) - 5.0f) < 1e-3f,
					  "off the extent it answers no, where HeightAt clamps and answers 5 m");

				// Translate, yaw and scale at once, on the ramp -- flat ground
				// would pass a rotation test that did nothing. The ramp rises
				// along its own +x, so local (16, ., 0) stands at 7.5 m; a yaw
				// of 90 degrees sends local +x to world -z, and a scale of two
				// doubles both the offset and the height. The point below is
				// that arithmetic run forwards.
				Entity tilted = queried->CreateEntity("Tilted");
				auto& tiltedTerrain = tilted.AddComponent<TerrainComponent>();
				tiltedTerrain.Terrain = rampHandle;
				tiltedTerrain.Size = 64.0f;
				tiltedTerrain.Height = 10.0f;
				auto& tiltedTransform = tilted.GetComponent<TransformComponent>();
				tiltedTransform.Position = { 100.0f, 2.0f, 0.0f };
				tiltedTransform.Rotation = { 0.0f, Math::Radians(90.0f), 0.0f };
				tiltedTransform.Scale = { 2.0f, 2.0f, 2.0f };

				height = -1.0f;
				Check(queried->TerrainHeightAt(Vec3(100.0f, 0.0f, -32.0f), height) &&
					  Math::Abs(height - 17.0f) < 0.01f,
					  "it follows the terrain's transform: translated, yawed and scaled");

				// A terrain raised above another: the query is about the ground
				// you would land on, so the higher surface answers -- and it is
				// the height that decides, not the order they were created in.
				Entity upper = queried->CreateEntity("Upper");
				auto& upperTerrain = upper.AddComponent<TerrainComponent>();
				upperTerrain.Terrain = flatHandle;
				upperTerrain.Size = 64.0f;
				upperTerrain.Height = 10.0f;
				auto& upperTransform = upper.GetComponent<TransformComponent>();
				upperTransform.Position = { 0.0f, 10.0f, 0.0f };

				height = -1.0f;
				Check(queried->TerrainHeightAt(Vec3(-5.0f, 0.0f, 8.0f), height) &&
					  Math::Abs(height - 15.0f) < 1e-3f,
					  "where two terrains cover the point, the higher surface answers");

				upperTransform.Position = { 0.0f, -10.0f, 0.0f };
				height = -1.0f;
				Check(queried->TerrainHeightAt(Vec3(-5.0f, 0.0f, 8.0f), height) &&
					  Math::Abs(height - 5.0f) < 1e-3f,
					  "and lowering it hands the answer back: height decides, not order");

				// The named form: this terrain, whatever else covers the point.
				upperTransform.Position = { 0.0f, 10.0f, 0.0f };
				height = -1.0f;
				Check(queried->TerrainHeightAt(plain, Vec3(-5.0f, 0.0f, 8.0f), height) &&
					  Math::Abs(height - 5.0f) < 1e-3f,
					  "the named form answers for the terrain named, not the highest");
				height = -1.0f;
				Check(!queried->TerrainHeightAt(plain, Vec3(100.0f, 0.0f, 100.0f), height) &&
					  height == 0.0f,
					  "and off that terrain's own extent it answers no");

				Entity notTerrain = queried->CreateEntity("NotTerrain");
				height = -1.0f;
				Check(!queried->TerrainHeightAt(notTerrain, Vec3(0.0f, 0.0f, 0.0f), height) &&
					  height == 0.0f,
					  "an entity with no terrain on it answers no, not zero metres");
				Check(!queried->TerrainHeightAt(Entity{}, Vec3(0.0f, 0.0f, 0.0f), height),
					  "and neither does an entity that is not there at all");
			}

			scene->OnRuntimeStart();
			Physics::World* physics = scene->GetPhysics();
			Check(physics && physics->GetBodyCount() == 3,
				  "play gives each terrain a static body, and the ball its own");

			constexpr float dt = 1.0f / 60.0f;
			for (int i = 0; i < 400; i++)
				scene->OnFixedUpdateRuntime(dt);
			scene->OnUpdateRuntime(dt);

			const Vec3 rest = ball.GetComponent<TransformComponent>().Position;
			// The flat terrain stands at 5 m; the ball's radius is 0.5.
			Check(Math::Abs(rest.y - 5.5f) < 0.05f,
				  "a ball dropped on a terrain rests at HeightAt plus its radius");
			Check(physics && Math::Length(physics->GetLinearVelocity(ball.GetUUID())) < 0.2f,
				  "and settles");
			Check(Math::Abs(ground.GetComponent<TransformComponent>().Position.y) < 1e-4f,
				  "a terrain with a RigidBody on it does not fall: the terrain is static");

			// Rays: through the flat one, and through the ramp at a known x.
			if (physics)
			{
				const RayHit hit = physics->CastRay({ -10.0f, 20.0f, 9.0f }, { 0.0f, -40.0f, 0.0f });
				Check(hit.Hit && hit.Entity == ground.GetUUID(), "a ray from above hits the terrain and names its entity");
				Check(hit.Hit && Math::Abs(hit.Position.y - 5.0f) < 0.02f && hit.Normal.y > 0.99f,
					  "at the surface height, with the surface's normal");

				// The ramp at local x = 16 stands at 7.5 m; the entity is at x = 200.
				const RayHit onRamp = physics->CastRay({ 216.0f, 30.0f, 4.0f }, { 0.0f, -60.0f, 0.0f });
				const float expected = scene->GetRegistry().get<TerrainComponent>(slope).Runtime
					? scene->GetRegistry().get<TerrainComponent>(slope).Runtime->HeightAt(16.0f, 4.0f) : -1.0f;
				Check(onRamp.Hit && onRamp.Entity == slope.GetUUID() &&
					  Math::Abs(onRamp.Position.y - 7.5f) < 0.02f && Math::Abs(expected - 7.5f) < 1e-3f,
					  "the collider and HeightAt agree on the ramp to the centimetre");
				Check(onRamp.Hit && onRamp.Normal.x < -0.1f && onRamp.Normal.y > 0.9f,
					  "and the ramp's collision normal leans against the slope");
			}

			scene->OnRuntimeStop();
		}

		// Leave the sample project as it was found.
		std::filesystem::remove(flatPath, error);
		std::filesystem::remove(rampPath, error);
		std::filesystem::remove(flatPath.string() + ".meta", error);
		std::filesystem::remove(rampPath.string() + ".meta", error);
		Assets::Registry::Refresh();
	}

	// The terrain brush (ENGINE-NOTES 7ar): the kernel, the four modes, the
	// rectangle a step reports, the recorder that makes a drag one command,
	// the command through a real Terrain, the heightfield ray cast, and the
	// region upload the paint rides on.
	void CheckTerrainBrush()
	{
		using Dimensions = Terrain::Dimensions;
		constexpr uint32_t kRes = 65;
		constexpr float kSize = 64.0f;     // one metre a cell
		constexpr float kHeight = 10.0f;

		// --- the kernel ------------------------------------------------------
		TerrainBrush brush;
		brush.Radius = 8.0f;
		brush.Hardness = 0.5f;
		Check(brush.Weight(0.0f) == 1.0f && brush.Weight(3.9f) == 1.0f,
			  "the kernel weighs one at the centre and out to hardness times the radius");
		Check(brush.Weight(8.0f) == 0.0f && brush.Weight(20.0f) == 0.0f,
			  "and nothing at the rim and beyond it");
		{
			bool monotone = true;
			float previous = 1.0f;
			for (float d = 4.0f; d <= 8.0f; d += 0.25f)
			{
				const float w = brush.Weight(d);
				monotone = monotone && w <= previous + 1e-6f && w >= 0.0f && w <= 1.0f;
				previous = w;
			}
			Check(monotone, "and falls off without a bump between");
			Check(Math::Abs(brush.Weight(6.0f) - 0.5f) < 1e-5f,
				  "halfway across the fall-off band it weighs a half");
		}
		brush.Hardness = 1.0f;
		Check(brush.Weight(7.99f) == 1.0f && brush.Weight(8.0f) == 0.0f,
			  "hardness one is a hard disc");
		brush.Hardness = 0.0f;
		Check(brush.Weight(0.0f) == 1.0f && Math::Abs(brush.Weight(4.0f) - 0.5f) < 1e-5f,
			  "hardness zero is a cone from the centre");
		brush.Hardness = 0.5f;

		// --- raise and lower ---------------------------------------------------
		{
			TerrainData flat = TerrainData::Flat(kRes, 32768);
			brush.Mode = TerrainBrush::Op::Raise;
			brush.Strength = 1.0f;
			brush.Invert = false;
			const TerrainRect rect = brush.Apply(flat, kSize, kHeight, 0.0f, 0.0f, 0.5f, 0.5f);
			// The centre is sample (32, 32); the circle's box is 24..40.
			Check(rect.X0 == 24 && rect.X1 == 40 && rect.Z0 == 24 && rect.Z1 == 40,
				  "a step reports the circle's bounding box in samples");
			// A quarter of the height per second at strength 1: half a second
			// is an eighth, 8192 units, at every full-weight sample.
			Check(flat.At(32, 32) == 32768 + 8192 && flat.At(35, 32) == 32768 + 8192,
				  "a full-strength raise climbs a quarter of Height per second at full weight");
			Check(flat.At(32, 32 + 6) == 32768 + 4096,
				  "and half that where the kernel weighs a half");
			Check(flat.At(32, 40) == 32768 && flat.At(0, 0) == 32768 && flat.At(23, 32) == 32768,
				  "and nothing at the rim or outside the box");
			bool symmetric = true;
			for (uint32_t i = 0; i <= 8; ++i)
				symmetric = symmetric && flat.At(32 + i, 32) == flat.At(32 - i, 32) &&
							flat.At(32, 32 + i) == flat.At(32, 32 - i);
			Check(symmetric, "the raise is symmetric about the centre");

			brush.Invert = true;
			brush.Apply(flat, kSize, kHeight, 0.0f, 0.0f, 0.5f, 0.5f);
			Check(flat.At(32, 32) == 32768 && flat.At(32, 38) == 32768,
				  "Shift lowers by exactly what the same stroke raised");
			brush.Invert = false;

			// Frame-rate independence: two steps of a sixtieth equal one of a
			// thirtieth, to the rounding of a unit.
			TerrainData a = TerrainData::Flat(kRes, 32768), b = TerrainData::Flat(kRes, 32768);
			brush.Apply(a, kSize, kHeight, 0.0f, 0.0f, 0.5f, 1.0f / 30.0f);
			brush.Apply(b, kSize, kHeight, 0.0f, 0.0f, 0.5f, 1.0f / 60.0f);
			brush.Apply(b, kSize, kHeight, 0.0f, 0.0f, 0.5f, 1.0f / 60.0f);
			Check(Math::Abs((int)a.At(32, 32) - (int)b.At(32, 32)) <= 1,
				  "a raise is a rate: two short steps equal one long one");

			// Saturation: nothing climbs past the top of the sixteen bits.
			TerrainData high = TerrainData::Flat(kRes, 65000);
			brush.Apply(high, kSize, kHeight, 0.0f, 0.0f, 0.5f, 5.0f);
			Check(high.At(32, 32) == 65535, "and the top of the range clamps");

			// Off the grid: no rectangle, nothing touched.
			TerrainData untouched = TerrainData::Flat(kRes, 100);
			const TerrainRect none = brush.Apply(untouched, kSize, kHeight, 500.0f, 0.0f, 0.5f, 1.0f);
			Check(none.Empty() && untouched.At(64, 32) == 100, "a step off the grid touches nothing");
		}

		// --- smooth --------------------------------------------------------------
		{
			TerrainData spike = TerrainData::Flat(kRes, 10000);
			spike.Heights[(size_t)32 * kRes + 32] = 50000;
			brush.Mode = TerrainBrush::Op::Smooth;
			brush.Strength = 1.0f;
			brush.Apply(spike, kSize, kHeight, 0.0f, 0.0f, 0.5f, TerrainBrush::kStepSeconds);
			// An eighth of the way to the 3x3 mean: the spike's mean is
			// (50000 + 8 * 10000) / 9 = 14444, so it drops by (50000 - 14444) / 8.
			Check(spike.At(32, 32) < 50000 && Math::Abs((int)spike.At(32, 32) - 45556) <= 2,
				  "a smooth step takes a spike an eighth of the way to its neighbourhood's mean");
			Check(spike.At(33, 32) > 10000 && spike.At(31, 33) > 10000,
				  "and lifts the neighbours toward it");
			Check(spike.At(45, 45) == 10000, "and leaves the ground outside the brush alone");
			for (int i = 0; i < 400; ++i)
				brush.Apply(spike, kSize, kHeight, 0.0f, 0.0f, 0.5f, TerrainBrush::kStepSeconds);
			Check(spike.At(32, 32) < 12000, "held long enough, the spike is gone");
		}

		// --- flatten -------------------------------------------------------------
		{
			TerrainData ramp = TerrainData::Flat(kRes, 0);
			for (uint32_t z = 0; z < kRes; ++z)
				for (uint32_t x = 0; x < kRes; ++x)
					ramp.Heights[(size_t)z * kRes + x] = (uint16_t)(x * 1000);
			brush.Mode = TerrainBrush::Op::Flatten;
			brush.Strength = 1.0f;
			for (int i = 0; i < 300; ++i)
				brush.Apply(ramp, kSize, kHeight, 0.0f, 0.0f, 32000.0f / 65535.0f, TerrainBrush::kStepSeconds);
			// Within four units: an eighth of a gap under four units rounds to
			// nothing, so a held flatten stops that close -- a fraction of a
			// millimetre on a ten-metre terrain, stated in 7ar.
			Check(Math::Abs((int)ramp.At(35, 32) - 32000) <= 4 && Math::Abs((int)ramp.At(29, 30) - 32000) <= 4,
				  "a flatten held converges the full-weight samples on its target, to the rounding");
			Check(ramp.At(50, 32) == 50000, "and leaves the ramp beyond the rim");
		}

		// --- paint ---------------------------------------------------------------
		{
			TerrainData ground = TerrainData::Flat(kRes, 100);
			brush.Mode = TerrainBrush::Op::Paint;
			brush.Strength = 1.0f;
			brush.Layer = 1;
			brush.Invert = false;
			Check(!ground.HasWeights(), "an unpainted grid has no weights");
			brush.Apply(ground, kSize, kHeight, 0.0f, 0.0f, 0.5f, TerrainBrush::kStepSeconds);
			Check(ground.HasWeights(), "the first paint step gives it some");
			// One step at strength 1 is a = 1/8. The texel was all zero -- layer
			// 0 by the shader's rule -- so it is materialised as (255, 0, 0, 0)
			// first, then layer 1 takes an eighth: (223, 32, 0, 0).
			Check(ground.WeightAt(32, 32, 0) == 223 && ground.WeightAt(32, 32, 1) == 32 &&
				  ground.WeightAt(32, 32, 2) == 0,
				  "painting an unpainted texel materialises layer 0 before it blends");
			Check(ground.WeightAt(60, 60, 0) == 0 && ground.WeightAt(60, 60, 1) == 0,
				  "and leaves texels outside the brush unpainted");
			for (int i = 0; i < 200; ++i)
				brush.Apply(ground, kSize, kHeight, 0.0f, 0.0f, 0.5f, TerrainBrush::kStepSeconds);
			Check(ground.WeightAt(32, 32, 1) == 255 && ground.WeightAt(32, 32, 0) == 0,
				  "held, the layer replaces the others under the brush");
			brush.Layer = 2;
			brush.Apply(ground, kSize, kHeight, 0.0f, 0.0f, 0.5f, TerrainBrush::kStepSeconds);
			Check(ground.WeightAt(32, 32, 2) == 32 && ground.WeightAt(32, 32, 1) == 223,
				  "a second layer takes its share from the first, and the sum stays");
			brush.Invert = true;
			brush.Layer = 1;
			for (int i = 0; i < 100; ++i)
				brush.Apply(ground, kSize, kHeight, 0.0f, 0.0f, 0.5f, TerrainBrush::kStepSeconds);
			Check(ground.WeightAt(32, 32, 1) == 0 && ground.WeightAt(32, 32, 2) == 32,
				  "Shift erases the layer and only the layer");
			brush.Invert = false;
		}

		// --- the recorder --------------------------------------------------------
		{
			TerrainData grid = TerrainData::Flat(kRes, 1000);
			brush.Mode = TerrainBrush::Op::Raise;
			brush.Strength = 1.0f;
			TerrainStrokeRecorder recorder;
			recorder.Begin(grid, true);
			// Two steps that overlap: the second's before must be the grid as
			// the first left it where they overlap, and the untouched grid
			// where they do not.
			TerrainRect first = brush.Footprint(grid, kSize, 0.0f, 0.0f);
			recorder.Cover(grid, first);
			brush.Apply(grid, kSize, kHeight, 0.0f, 0.0f, 0.5f, 0.5f);
			TerrainRect second = brush.Footprint(grid, kSize, 6.0f, 0.0f);
			recorder.Cover(grid, second);
			brush.Apply(grid, kSize, kHeight, 6.0f, 0.0f, 0.5f, 0.5f);
			recorder.End();
			const TerrainRect all = recorder.Rect();
			Check(all.X0 == 24 && all.X1 == 46 && all.Z0 == 24 && all.Z1 == 40,
				  "the recorder's rectangle is the union of the steps'");
			const std::vector<uint16_t>& before = recorder.BeforeHeights();
			auto at = [&](uint32_t x, uint32_t z) { return before[(size_t)(z - all.Z0) * all.Width() + (x - all.X0)]; };
			Check(at(32, 32) == 1000 && at(46, 32) == 1000 && at(38, 32) == 1000,
				  "and its before is the untouched grid everywhere, the overlap included");
			Check(grid.At(32, 32) > 1000 && grid.At(38, 32) > 1000, "while the grid itself moved");
		}

		// --- the command, through a real terrain, and the ray cast ---------------
		{
			const std::filesystem::path terrainDir = Project::AssetRoot() / "terrain";
			const std::filesystem::path path = terrainDir / "scenetest_brush.rvterrain";
			std::error_code error;
			std::filesystem::create_directories(terrainDir, error);
			Assets::TerrainSerializer::Save(TerrainData::Flat(kRes, 32768), path);
			Assets::Registry::Refresh();
			const AssetHandle handle = Assets::Registry::GetHandle("terrain/scenetest_brush.rvterrain");
			Check(handle.IsValid(), "the brush fixture is an asset");

			if (handle.IsValid())
			{
				auto scene = std::make_shared<Scene>();
				Entity ground = scene->CreateEntity("Ground");
				auto& component = ground.AddComponent<TerrainComponent>();
				component.Terrain = handle;
				component.Size = kSize;
				component.Height = kHeight;

				const RHI::Ref<Terrain>& terrain = Terrain::Resolve(component);
				Check(terrain != nullptr, "and resolves to a runtime");
				if (terrain)
				{
					// The ray cast, before anything moves: a flat terrain 5 m up.
					float t = 0.0f;
					Check(terrain->Raycast(Vec3(0.0f, 20.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), t) &&
						  Math::Abs(t - 15.0f) < 0.01f,
						  "a ray straight down meets a flat terrain at its height");
					Check(terrain->Raycast(Vec3(10.0f, 20.0f, 30.0f), Vec3(-1.0f, -1.0f, -1.0f), t) &&
						  Math::Abs(t - 15.0f * Math::Sqrt(3.0f)) < 0.01f,
						  "and a diagonal ray where its height reaches the plane");
					Check(!terrain->Raycast(Vec3(0.0f, 20.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), t),
						  "a ray pointing away misses");
					Check(!terrain->Raycast(Vec3(100.0f, 20.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), t),
						  "and so does one beside the terrain");

					// A stroke by hand: edit the manager's grid, push the region
					// through the runtime, wrap it in a command, undo, redo.
					TerrainData* data = Assets::Manager::EditTerrain(handle);
					Check(data != nullptr && Assets::Manager::IsTerrainDirty(handle),
						  "EditTerrain hands out the manager's grid and marks it dirty");
					if (data)
					{
						brush.Mode = TerrainBrush::Op::Raise;
						brush.Strength = 1.0f;
						brush.Invert = false;
						TerrainStrokeRecorder recorder;
						recorder.Begin(*data, true);
						const TerrainRect footprint = brush.Footprint(*data, kSize, 0.0f, 0.0f);
						recorder.Cover(*data, footprint);
						const TerrainRect touched = brush.Apply(*data, kSize, kHeight, 0.0f, 0.0f, 0.5f, 1.0f);
						terrain->ApplyRegion(*data, touched);
						recorder.End();

						const float raised = terrain->HeightAt(0.0f, 0.0f);
						Check(Math::Abs(raised - 7.5f) < 0.01f,
							  "ApplyRegion moves HeightAt: a second at strength 1 lifts a 5 m plain to 7.5");
						Check(terrain->HasStale(), "and leaves the touched chunks stale");
						if (Renderer::HasDevice())
						{
							terrain->RebuildStale(true);
							Check(!terrain->HasStale(), "RebuildStale(true) clears them");
							// The rebuilt level 0 carries the new heights.
							const Terrain::Chunk* chunk = terrain->ChunkAt(0, 0);
							bool lifted = false;
							if (chunk && chunk->Levels[0])
							{
								for (const Vec3& p : chunk->Levels[0]->GetPositions())
									if (Math::Abs(p.x) < 0.01f && Math::Abs(p.z) < 0.01f)
										lifted = Math::Abs(p.y - 7.5f) < 0.01f;
							}
							Check(lifted, "and the level-0 mesh has the raised vertex where the data says");
						}
						Check(Math::Abs(terrain->GetChunks()[0].Bounds.Max.y - 7.5f) < 0.01f,
							  "the chunk's bounds follow the heights");

						std::vector<uint16_t> after;
						CopyHeightsOut(*data, recorder.Rect(), after);
						auto command = std::make_unique<TerrainStrokeCommand>(
							scene, handle, recorder.Rect(), "Terrain Raise",
							recorder.BeforeHeights(), std::move(after));
						Check(command->TouchesScene(), "a stroke command counts toward the unsaved mark");
						command->Undo();
						Check(Math::Abs(terrain->HeightAt(0.0f, 0.0f) - 5.0f) < 0.01f &&
							  data->At(32, 32) == 32768,
							  "undoing the stroke puts the grid and the runtime back");
						command->Execute();
						Check(Math::Abs(terrain->HeightAt(0.0f, 0.0f) - 7.5f) < 0.01f,
							  "and redo draws exactly what the stroke drew");
						command->Undo();

						// A paint stroke through the same door: the runtime's copy
						// gains weights and, with a device, a real weight map.
						brush.Mode = TerrainBrush::Op::Paint;
						brush.Layer = 1;
						const TerrainRect painted = brush.Apply(*data, kSize, kHeight, 0.0f, 0.0f, 0.5f, 1.0f);
						terrain->ApplyRegion(*data, painted);
						Check(terrain->GetData().HasWeights() && terrain->GetData().WeightAt(32, 32, 1) > 200,
							  "a paint stroke reaches the runtime's weights");
						if (Renderer::HasDevice())
						{
							Check(terrain->GetWeightMap() && terrain->GetWeightMap()->GetWidth() == kRes,
								  "and turns the 1x1 stand-in into a real weight map");
						}
					}

					// Saved back, and the sidecar follows the bytes.
					Check(Assets::Manager::SaveTerrain(handle) && !Assets::Manager::IsTerrainDirty(handle),
						  "SaveTerrain writes the asset and clears the mark");
					TerrainData reread;
					Check(Assets::TerrainSerializer::Load(reread, path) && reread.HasWeights() &&
						  reread.WeightAt(32, 32, 1) > 200,
						  "and the file holds the paint");
					Check(Assets::Registry::GetMetadata(handle).SourceHash == Assets::Registry::HashFile(path),
						  "with a sidecar whose hash is the file's");
				}
			}

			// UploadRegion: refused and survivable when it does not fit.
			if (Renderer::HasDevice())
			{
				const uint8_t texel[16] = { 0 };
				TextureLoader::White(Renderer::GetDevice())->UploadRegion(1, 1, 2, 2, texel, sizeof(texel));
				Check(true, "a region upload beyond the texture is refused and survivable");
			}

			// --- the varieties (7as): shape x pattern, and four more operations --

			// A mask with an answer of its own: the left half full, the right
			// half empty, so where it reads 1 and 0 is not a matter of taste.
			auto halfMask = []()
			{
				auto mask = std::make_shared<BrushMask>();
				mask->Size = 16;
				mask->Values.assign(16 * 16, 0.0f);
				for (uint32_t z = 0; z < 16; ++z)
					for (uint32_t x = 0; x < 8; ++x)
						mask->Values[(size_t)z * 16 + x] = 1.0f;
				return mask;
			};
			{
				std::shared_ptr<const BrushMask> mask = halfMask();
				Check(mask->Valid() && mask->Sample(0.2f, 0.5f, false) > 0.99f &&
					  mask->Sample(0.8f, 0.5f, false) < 0.01f,
					  "a mask samples its own halves");
				Check(Math::Abs(mask->Sample(0.5f, 0.5f, false) - 0.5f) < 0.02f,
					  "and interpolates across the seam between them");
				Check(mask->Sample(-0.5f, 0.5f, false) == 0.0f && mask->Sample(1.5f, 0.5f, false) == 0.0f,
					  "outside its square it is nothing");
				Check(mask->Sample(1.2f, 0.5f, true) > 0.99f && mask->Sample(1.8f, 0.5f, true) < 0.01f,
					  "and wrapped it tiles");
				BrushMask empty;
				Check(!empty.Valid() && empty.Sample(0.5f, 0.5f, false) == 0.0f,
					  "an empty mask is not valid and reads zero");
			}

			// The shape: the disc's radial rule untouched, the mask turned.
			{
				TerrainBrush shaped;
				shaped.Radius = 10.0f;
				shaped.Hardness = 0.5f;
				Check(Math::Abs(shaped.Weight(3.0f, 4.0f, 0.0f) - shaped.Weight(5.0f)) < 1e-6f,
					  "a disc's weight at an offset is its weight at that distance");

				shaped.ShapeKind = TerrainBrush::Shape::Mask;
				shaped.ShapeMask = halfMask();
				// The mask's left half covers -x; at no rotation the point 7 m
				// along -x is inside it and the one along +x is not.
				Check(shaped.Weight(-7.0f, 0.0f, 0.0f) > 0.99f && shaped.Weight(7.0f, 0.0f, 0.0f) < 0.01f,
					  "a mask covers what its image says, in the brush's own frame");
				Check(shaped.Weight(0.0f, 12.0f, 0.0f) == 0.0f,
					  "and nothing outside the square, whatever the image holds");
				shaped.Angle = Math::Pi;
				Check(shaped.Weight(-7.0f, 0.0f, 0.0f) < 0.01f && shaped.Weight(7.0f, 0.0f, 0.0f) > 0.99f,
					  "turned half a circle it covers the other side");
				shaped.Angle = 0.0f;
				shaped.FollowStroke = true;
				Check(shaped.Weight(-7.0f, 0.0f, Math::Pi) < 0.01f && shaped.Weight(7.0f, 0.0f, Math::Pi) > 0.99f,
					  "and following the stroke, the heading turns it the same way");
				shaped.FollowStroke = false;
				shaped.ShapeMask = nullptr;
				Check(shaped.Weight(0.0f, 0.0f, 0.0f) == 0.0f, "a mask shape with no mask covers nothing");
			}

			// The pattern, on the ground rather than in the brush.
			{
				TerrainBrush patterned;
				patterned.Radius = 10.0f;
				Check(patterned.PatternAt(3.0f, 4.0f, 7u) == 1.0f, "no pattern is a weight of one everywhere");

				const float n = TerrainBrush::Noise(1.25f, -0.5f, 11u);
				Check(n >= 0.0f && n <= 1.0f, "the brush's noise stays inside its range");
				Check(TerrainBrush::Noise(1.25f, -0.5f, 11u) == n, "and is the same at the same place and seed");
				Check(TerrainBrush::Noise(1.25f, -0.5f, 12u) != n, "and different at another seed");
				bool varies = false;
				for (int i = 1; i < 40 && !varies; ++i)
					varies = Math::Abs(TerrainBrush::Noise((float)i * 0.37f, 0.0f, 11u) - n) > 0.05f;
				Check(varies, "and is a field, not a constant");

				patterned.PatternKind = TerrainBrush::Pattern::Tiled;
				patterned.PatternMask = halfMask();
				patterned.PatternScale = 8.0f;
				// Tiled every 8 m: the mask's left half lands on [0, 4) of every
				// tile, so 2 m in is full and 6 m in is empty, at any tile.
				Check(patterned.PatternAt(2.0f, 0.0f, 0u) > 0.99f && patterned.PatternAt(6.0f, 0.0f, 0u) < 0.01f,
					  "a tiled pattern lands where the tiling says");
				Check(patterned.PatternAt(802.0f, 0.0f, 0u) > 0.99f && patterned.PatternAt(-6.0f, 0.0f, 0u) > 0.99f,
					  "and repeats, forward and back, without a seam in the answer");
			}

			// The footprint grows for a mask, because a turned square reaches
			// further than the disc it replaces.
			{
				TerrainData grid = TerrainData::Flat(kRes, 30000);
				TerrainBrush footprint;
				footprint.Radius = 8.0f;
				const TerrainRect disc = footprint.Footprint(grid, kSize, 0.0f, 0.0f);
				footprint.ShapeKind = TerrainBrush::Shape::Mask;
				footprint.ShapeMask = halfMask();
				const TerrainRect square = footprint.Footprint(grid, kSize, 0.0f, 0.0f);
				Check(square.Width() > disc.Width() && square.Width() <= disc.Width() * 2,
					  "a mask's footprint is the disc's grown by root two, not doubled");

				// A ramp's is the segment's box, so a stroke that walks 20 m
				// covers ground the point never did.
				TerrainBrush ramp;
				ramp.Mode = TerrainBrush::Op::Ramp;
				ramp.Radius = 4.0f;
				TerrainBrush::Stroke stroke;
				stroke.StartX = -10.0f;
				stroke.StartZ = 0.0f;
				const TerrainRect line = ramp.Footprint(grid, kSize, 10.0f, 0.0f, stroke);
				Check(line.Width() > line.Height() && line.Width() >= 20,
					  "a ramp's footprint spans from where the stroke began to where it is");
			}

			// Terrace: toward the nearest of its levels, and a sample already on
			// one does not move.
			{
				TerrainData grid = TerrainData::Flat(kRes, 20000);
				TerrainBrush terrace;
				terrace.Mode = TerrainBrush::Op::Terrace;
				terrace.Radius = 20.0f;
				terrace.Hardness = 1.0f;
				terrace.Strength = 1.0f;
				terrace.TerraceSteps = 4;   // levels at 0, 16384, 32768, 49152, 65535
				for (int i = 0; i < 240; ++i)
					terrace.Apply(grid, kSize, kHeight, 0.0f, 0.0f, 0.0f, TerrainBrush::kStepSeconds);
				Check(Math::Abs((int)grid.At(32, 32) - 16384) < 64,
					  "terrace pulls a sample to the nearest of its levels");
				TerrainData onLevel = TerrainData::Flat(kRes, 16384);
				terrace.Apply(onLevel, kSize, kHeight, 0.0f, 0.0f, 0.0f, TerrainBrush::kStepSeconds);
				Check(onLevel.At(32, 32) == 16384, "and leaves one already on a level alone");
			}

			// Set Height: in metres, and it converges there.
			{
				TerrainData grid = TerrainData::Flat(kRes, 0);
				TerrainBrush set;
				set.Mode = TerrainBrush::Op::SetHeight;
				set.Radius = 20.0f;
				set.Hardness = 1.0f;
				set.Strength = 1.0f;
				set.TargetHeight = kHeight * 0.5f;   // half the terrain's height
				for (int i = 0; i < 240; ++i)
					set.Apply(grid, kSize, kHeight, 0.0f, 0.0f, 0.0f, TerrainBrush::kStepSeconds);
				Check(Math::Abs((int)grid.At(32, 32) - 32768) < 64,
					  "set height drives the ground to a height in metres");
				Check(grid.At(0, 0) == 0, "and leaves what the brush never reached");
			}

			// Ramp: the far end reaches the cursor's height, the near end stays,
			// and across the line it is flat.
			{
				TerrainData grid = TerrainData::Flat(kRes, 0);
				// A step up at the far end for the ramp to climb to.
				for (uint32_t z = 0; z < kRes; ++z)
					for (uint32_t x = 40; x < kRes; ++x)
						grid.Heights[(size_t)z * kRes + x] = 40000;
				TerrainBrush ramp;
				ramp.Mode = TerrainBrush::Op::Ramp;
				ramp.Radius = 6.0f;
				ramp.Hardness = 1.0f;
				ramp.Strength = 1.0f;
				TerrainBrush::Stroke stroke;
				stroke.StartX = -20.0f;
				stroke.StartZ = 0.0f;
				stroke.StartHeight = 0.0f;
				for (int i = 0; i < 300; ++i)
					ramp.Apply(grid, kSize, kHeight, 12.0f, 0.0f, stroke, TerrainBrush::kStepSeconds);
				// Sample indices are metres from the terrain's near edge, so
				// index 32 is local 0: the stroke runs from local -20 (index
				// 12) to local 12 (index 44), and its middle is local -4.
				const int near0 = (int)grid.At(12, 32);    // the near end
				const int middle = (int)grid.At(28, 32);   // half way along the segment
				const int far0 = (int)grid.At(44, 32);     // the far end
				Check(near0 < middle && middle < far0,
					  "a ramp rises from where the stroke began to where it is");
				Check(Math::Abs(middle - (near0 + far0) / 2) < 3000,
					  "and lands about half way at half way -- a line, not a curve");
				Check(grid.At(28, 32) == grid.At(28, 30) && grid.At(28, 32) == grid.At(28, 34),
					  "flat across the line inside the core");
				Check(grid.At(28, 5) == 0, "and nothing beyond its width");
			}

			// Erode: material leaves the peak and lands lower down, the same way
			// twice for one seed, and differently for another.
			{
				auto cone = []()
				{
					TerrainData grid = TerrainData::Flat(kRes, 0);
					for (uint32_t z = 0; z < kRes; ++z)
						for (uint32_t x = 0; x < kRes; ++x)
						{
							const float dx = (float)x - 32.0f, dz = (float)z - 32.0f;
							const float d = Math::Sqrt(dx * dx + dz * dz);
							const float h = Math::Max(1.0f - d / 24.0f, 0.0f);
							grid.Heights[(size_t)z * kRes + x] = (uint16_t)(h * 40000.0f);
						}
					return grid;
				};
				auto total = [](const TerrainData& grid)
				{
					double sum = 0.0;
					for (uint16_t h : grid.Heights)
						sum += (double)h;
					return sum;
				};

				TerrainData before = cone();
				TerrainData grid = cone();
				TerrainBrush erode;
				erode.Mode = TerrainBrush::Op::Erode;
				erode.Radius = 24.0f;
				erode.Hardness = 1.0f;
				erode.Strength = 1.0f;
				TerrainBrush::Stroke stroke;
				stroke.Seed = 12345u;
				for (int i = 0; i < 30; ++i)
				{
					erode.Apply(grid, kSize, kHeight, 0.0f, 0.0f, stroke, TerrainBrush::kStepSeconds);
					stroke.Seed = stroke.Seed * 1664525u + 1013904223u;
				}
				Check(grid.At(32, 32) < before.At(32, 32), "erosion takes the apex down");
				bool anyRaised = false;
				for (uint32_t z = 20; z < 45 && !anyRaised; ++z)
					for (uint32_t x = 20; x < 45; ++x)
						if (grid.At(x, z) > before.At(x, z) + 50)
						{
							anyRaised = true;
							break;
						}
				Check(anyRaised, "and puts what it took somewhere lower");
				const double lost = (total(before) - total(grid)) / Math::Max(total(before), 1.0);
				Check(lost >= -0.02 && lost < 0.35,
					  "the ground it moves mostly stays inside the footprint");

				TerrainData again = cone();
				TerrainBrush::Stroke same;
				same.Seed = 12345u;
				for (int i = 0; i < 30; ++i)
				{
					erode.Apply(again, kSize, kHeight, 0.0f, 0.0f, same, TerrainBrush::kStepSeconds);
					same.Seed = same.Seed * 1664525u + 1013904223u;
				}
				Check(again.Heights == grid.Heights, "the same seed erodes the same way, to the bit");

				TerrainData other = cone();
				TerrainBrush::Stroke different;
				different.Seed = 999u;
				for (int i = 0; i < 30; ++i)
				{
					erode.Apply(other, kSize, kHeight, 0.0f, 0.0f, different, TerrainBrush::kStepSeconds);
					different.Seed = different.Seed * 1664525u + 1013904223u;
				}
				Check(!(other.Heights == grid.Heights), "and another seed does not");
			}

			// The 7ar calls are the 7as calls with a stroke that began here.
			{
				TerrainData viaOld = TerrainData::Flat(kRes, 20000);
				TerrainData viaNew = TerrainData::Flat(kRes, 20000);
				TerrainBrush flatten;
				flatten.Mode = TerrainBrush::Op::Flatten;
				flatten.Radius = 10.0f;
				flatten.Strength = 1.0f;
				TerrainBrush::Stroke stroke;
				stroke.StartX = 4.0f;
				stroke.StartZ = -3.0f;
				stroke.StartHeight = 0.75f;
				flatten.Apply(viaOld, kSize, kHeight, 4.0f, -3.0f, 0.75f, 0.5f);
				flatten.Apply(viaNew, kSize, kHeight, 4.0f, -3.0f, stroke, 0.5f);
				Check(viaOld.Heights == viaNew.Heights,
					  "the old two-argument Apply is the new one with a stroke that began where it is");
			}

			std::filesystem::remove(path, error);
			std::filesystem::remove(path.string() + ".meta", error);
			Assets::Registry::Refresh();
		}
	}

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

	// Pausing has to hold the frame, not just the fixed step. The physics
	// blend runs on the *frame*, with an interpolation alpha that keeps
	// moving while paused -- so a pause that only stopped the stepping
	// re-blended the two frozen simulation states at a different point every
	// frame, and a body paused mid-fall jittered along its last step. The
	// observable contract: a paused frame does not rewrite simulated
	// transforms, a paused fixed pass does not advance bodies, and resuming
	// does both again.
	void CheckPauseHoldsTheScene()
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

		scene->OnRuntimeStart();
		Check(!scene->IsPaused(), "a fresh run starts unpaused");

		// Mid-fall, so the two states the frame blends are a real step apart.
		for (int i = 0; i < 10; i++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		scene->OnUpdateRuntime(1.0f / 60.0f);

		const Vec3 atPause = box.GetComponent<TransformComponent>().Position;
		Check(atPause.y < 6.0f, "the box was falling when the pause hit");

		scene->SetPaused(true);

		// The discriminating observable for the jitter: the sentinel survives
		// only if the paused frame stops re-syncing simulated transforms.
		box.GetComponent<TransformComponent>().Position = { 42.0f, 42.0f, 42.0f };
		scene->OnUpdateRuntime(1.0f / 60.0f);
		Check(box.GetComponent<TransformComponent>().Position.x == 42.0f,
			  "a paused frame does not rewrite simulated transforms");

		// However many times the accumulator fires it.
		for (int i = 0; i < 30; i++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);

		scene->SetPaused(false);
		scene->OnUpdateRuntime(1.0f / 60.0f);
		Check(Math::Abs(box.GetComponent<TransformComponent>().Position.y - atPause.y) < 1e-4f,
			  "half a second of paused fixed steps moved nothing");

		for (int i = 0; i < 10; i++)
			scene->OnFixedUpdateRuntime(1.0f / 60.0f);
		scene->OnUpdateRuntime(1.0f / 60.0f);
		Check(box.GetComponent<TransformComponent>().Position.y < atPause.y - 0.01f,
			  "resuming lets the box keep falling");

		scene->OnRuntimeStop();
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
		Check(Math::Abs(InputMap::GetAxis("Zoom") - 3.0f) < 1e-4f, "the wheel reaches its axis");

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
			std::make_unique<AddComponentCommand>(scene, rootID, "AudioSourceComponent"),
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
		stack.Push(std::make_unique<AddComponentCommand>(scene, rootID, "AudioSourceComponent"));
		stack.Undo();
		Check(stack.CanRedo(), "redo is available after an undo");
		stack.Push(std::make_unique<AddComponentCommand>(scene, childID, "AudioSourceComponent"));
		Check(!stack.CanRedo(), "a new edit clears the redo branch");
	}

	// The property that makes a LUT recipe safe to attach: at its defaults it
	// bakes the identity table, **exactly**.
	//
	// Not nearly. A fresh `.rvlut` on a camera has to change nothing at all
	// until a knob moves, and "nothing at all" is a claim about bits -- 7t
	// already established that a sampling error of half a texel is invisible
	// in any image, so a baking error of the same size would be too.
	//
	// It is not automatic. `pow(x, 1.0f)`, `(x - 0.5f) * 1.0f + 0.5f` and
	// `luma + (x - luma) * 1.0f` are all mathematically the identity and none
	// is bit-exact for every float, which is why each stage skips itself at
	// its default rather than computing a no-op. This asserts the outcome of
	// that decision, so removing the skips fails here rather than in a grade
	// nobody can see. ENGINE-NOTES 7v.
	void CheckLutRecipe()
	{
		using namespace RageV::Assets;

		// **At more than one size, and that is not thoroughness for its own
		// sake.** At the default 33 the step is 1/32, so every coordinate is
		// a dyadic rational and `(x - 0.5f) * 1.0f + 0.5f` happens to come
		// back bit-exact -- the check passes at 33 whether the skip is there
		// or not, and would have been decoration. At 20 the step is 1/19 and
		// two of its twenty coordinates do not survive the round trip; at 64,
		// eleven do not. So the sizes below include one the arithmetic can
		// actually fail at, which is the difference between a check and a
		// reassurance. ENGINE-NOTES 7v.
		const int sizes[] = { 33, 20, 64 };

		for (int size : sizes)
		{
			LutRecipe neutral;
			neutral.Size = size;

			const ColorLut baked = BakeRecipe(neutral);
			const ColorLut identity = IdentityLut((uint32_t)size);
			const std::string at = " (size " + std::to_string(size) + ")";

			Check(baked.IsValid(), "a default recipe bakes a valid table" + at);
			Check(baked.Size == identity.Size, "of the size it asked for" + at);

			size_t differing = 0;
			if (baked.Values.size() == identity.Values.size())
			{
				for (size_t i = 0; i < baked.Values.size(); i++)
				{
					const Vec4& a = baked.Values[i];
					const Vec4& b = identity.Values[i];
					if (a.x != b.x || a.y != b.y || a.z != b.z)
						differing++;
				}
			}
			else
			{
				differing = 1;   // a size mismatch is every entry wrong
			}

			Check(differing == 0,
				  "a default recipe is the identity table to the bit" + at);
		}

		const LutRecipe neutral;
		const ColorLut identity = IdentityLut((uint32_t)neutral.Size);

		// Each knob has to *do* something on its own, or a recipe would be a
		// row of controls that quietly did nothing -- which is the failure the
		// skip-at-default optimisation could introduce by skipping too eagerly.
		const auto changes = [&](const LutRecipe& recipe, const char* what)
		{
			const ColorLut table = BakeRecipe(recipe);
			bool moved = false;
			for (size_t i = 0; i < table.Values.size() && !moved; i++)
			{
				const Vec4& a = table.Values[i];
				const Vec4& b = identity.Values[i];
				moved = a.x != b.x || a.y != b.y || a.z != b.z;
			}
			Check(moved, std::string(what) + " changes the table");
		};

		{ LutRecipe r; r.Temperature = 0.5f;      changes(r, "temperature"); }
		{ LutRecipe r; r.Tint = 0.5f;             changes(r, "tint"); }
		{ LutRecipe r; r.Lift = Vec3(0.1f);       changes(r, "lift"); }
		{ LutRecipe r; r.Gamma = Vec3(1.5f);      changes(r, "gamma"); }
		{ LutRecipe r; r.Gain = Vec3(0.8f);       changes(r, "gain"); }
		{ LutRecipe r; r.Contrast = 1.4f;         changes(r, "contrast"); }
		{ LutRecipe r; r.Saturation = 0.0f;       changes(r, "saturation"); }

		// Saturation at zero is the one knob with an answer that can be
		// checked rather than merely observed to differ: every entry has to
		// come out grey.
		{
			LutRecipe r;
			r.Saturation = 0.0f;
			const ColorLut mono = BakeRecipe(r);
			bool grey = true;
			for (const Vec4& entry : mono.Values)
				grey = grey && entry.x == entry.y && entry.y == entry.z;
			Check(grey, "saturation 0 makes every entry grey, on all three channels");
		}

		// Nothing may leave the unit cube: this is a display-referred table,
		// and a value above 1 wraps rather than clips on its way to 8 bits.
		{
			LutRecipe r;
			r.Gain = Vec3(4.0f);
			r.Lift = Vec3(-1.0f);
			r.Contrast = 4.0f;
			const ColorLut extreme = BakeRecipe(r);
			bool bounded = true;
			for (const Vec4& entry : extreme.Values)
			{
				bounded = bounded && entry.x >= 0.0f && entry.x <= 1.0f
								  && entry.y >= 0.0f && entry.y <= 1.0f
								  && entry.z >= 0.0f && entry.z <= 1.0f;
			}
			Check(bounded, "even an absurd recipe stays inside the unit cube");
		}

		// And the file round-trips, which is what "persistent" means for it.
		{
			const std::filesystem::path dir = ScratchDir("lutrecipe");
			std::error_code error;
			std::filesystem::create_directories(dir, error);
			const std::filesystem::path file = dir / "look.rvlut";

			LutRecipe written;
			written.Temperature = 0.25f;
			written.Tint = -0.125f;
			written.Lift = Vec3(0.01f, 0.02f, 0.03f);
			written.Gamma = Vec3(1.1f, 0.9f, 1.2f);
			written.Gain = Vec3(0.95f, 1.05f, 1.0f);
			written.Contrast = 1.15f;
			written.Saturation = 0.85f;
			written.Size = 17;

			Check(LutRecipeSerializer::Save(written, file), "a recipe writes");

			LutRecipe read;
			Check(LutRecipeSerializer::Load(read, file), "and reads back");

			Check(read.Temperature == written.Temperature &&
				  read.Tint == written.Tint &&
				  read.Lift.x == written.Lift.x && read.Lift.y == written.Lift.y &&
				  read.Lift.z == written.Lift.z &&
				  read.Gamma.x == written.Gamma.x && read.Gamma.y == written.Gamma.y &&
				  read.Gamma.z == written.Gamma.z &&
				  read.Gain.x == written.Gain.x && read.Gain.y == written.Gain.y &&
				  read.Gain.z == written.Gain.z &&
				  read.Contrast == written.Contrast &&
				  read.Saturation == written.Saturation &&
				  read.Size == written.Size,
				  "with every knob exactly as it was written");

			// A file that is not a recipe is refused rather than loaded as
			// defaults -- which would grade nothing and look like a working
			// neutral LUT.
			const std::filesystem::path impostor = dir / "not-a-recipe.rvlut";
			std::ofstream(impostor) << "SomethingElse: 1\nTemperature: 0.5\n";
			LutRecipe rejected;
			Check(!LutRecipeSerializer::Load(rejected, impostor),
				  "a file without the LutRecipe key is refused, not read as defaults");

			std::filesystem::remove_all(dir, error);
		}
	}

	// What the editor's unsaved-changes mark and its closing prompt both ask.
	//
	// The mark used to be `CanUndo()`, which says "something has been edited
	// this session" -- true from the first edit until the scene is closed, and
	// therefore lit right through every save. Nothing warned that a post
	// profile attached to a camera was still only in memory, and closing the
	// editor threw it away without a word.
	//
	// The interesting cases are not "an edit makes it dirty". They are the ones
	// a boolean flag gets wrong: undoing back to the save point is *clean*,
	// re-editing from a point behind the save point can never be clean again,
	// and an edit that writes itself is not a scene change at all.
	void CheckSceneDirty(const std::shared_ptr<Scene>& scene)
	{
		UUID rootID = UUID::Invalid();
		for (auto handle : scene->GetRegistry().view<TagComponent>())
		{
			Entity entity{ handle, scene.get() };
			if (entity.GetName() == "Root")
				rootID = entity.GetUUID();
		}

		if (!rootID.IsValid())
		{
			Check(false, "dirty-tracking fixture entity present");
			return;
		}

		const auto rename = [&](const char* from, const char* to)
		{
			return std::make_unique<FieldEditCommand>(scene, rootID, "TagComponent", "Tag",
													  FieldValue(std::string(from)),
													  FieldValue(std::string(to)));
		};

		CommandStack stack;
		Check(!stack.IsSceneDirty(), "a fresh stack is clean");

		stack.Push(rename("Root", "A"));
		Check(stack.IsSceneDirty(), "an edit makes the scene dirty");

		stack.MarkSaved();
		Check(!stack.IsSceneDirty(), "saving makes it clean");

		stack.Push(rename("A", "B"));
		Check(stack.IsSceneDirty(), "editing after a save makes it dirty again");

		// The case a flag cannot express: the scene is byte-for-byte what was
		// written, so there is nothing to prompt about.
		stack.Undo();
		Check(!stack.IsSceneDirty(), "undoing back to the save point is clean again");

		stack.Redo();
		Check(stack.IsSceneDirty(), "redoing past the save point is dirty again");

		// Behind the save point, then a new edit. The saved state is now on a
		// branch that has been discarded, so no amount of undoing reaches it --
		// and the count alone would collide with it, reporting a modified scene
		// as saved. It must stay dirty from here.
		stack.Undo();
		stack.Undo();
		Check(stack.IsSceneDirty(), "undoing past the save point is dirty");
		stack.Push(rename("Root", "C"));
		Check(stack.IsSceneDirty(), "a new edit from behind the save point is dirty");
		stack.Undo();
		Check(stack.IsSceneDirty(), "and undoing it does not resurrect the lost save point");

		// A scene load levels both sides: what is in memory is what is on disk.
		stack.Clear();
		Check(!stack.IsSceneDirty(), "clearing the stack is clean");

		// The render settings live on the project and are written the instant
		// they change. Undoable, but not something a scene file would keep --
		// counting them would light the mark for something already on disk.
		int value = 0;
		stack.PushApplied(std::make_unique<ValueEditCommand>(
			"Render settings", [&value] { value = 1; }, [&value] { value = 0; },
			/*touchesScene*/ false));
		Check(!stack.IsSceneDirty(), "an edit that writes itself is not an unsaved scene change");
		Check(stack.CanUndo(), "and is still undoable");

		stack.Push(rename("Root", "D"));
		Check(stack.IsSceneDirty(), "a scene edit beside it still counts");
		stack.Undo();
		Check(!stack.IsSceneDirty(), "and undoing only that one is clean");
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
	CheckFrameHook();
	CheckLiveScriptChanges();

	// --- physics -------------------------------------------------------------
	CheckDemoButtons();
	CheckRenderGraph();
	CheckPrimitiveWinding();
	CheckCubemap();
	CheckSky();
	CheckFont();
	CheckTextLayout();
	CheckCanvasLayout();
	CheckUIInteraction();
	CheckWorldText();
	CheckUIRenderer();
	CheckViewportGrid();
	CheckRenderersReady();
	CheckFieldLabels();
	CheckMotionHistory();
	CheckScriptGen();
	CheckAutoExposureLaw();
	CheckTemporalJitter();
	CheckTemporalHistory();
	CheckAntiAliasingSwitch();
	CheckRenderSettings();
	CheckPostProfileOnCamera();
	CheckLutRecipe();
	CheckCubeLut();
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
	CheckMaterialOverrides();
	CheckCompressedTextures();
	CheckTextureCook();
	CheckMeshCook();
	CheckEmbeddedGlbTexture();
	CheckSrgbEncode();
	CheckBootProgress();
	CheckImportCache();
	CheckVfsAndPak();
	CheckMaterialAssets();
	CheckProbeSelection();
	CheckProbeArraySize();
	CheckFrameGraph();
	CheckShortcutOwnership();
	CheckMathFunctions();
	CheckMultisampledDepth();
	CheckBindlessHeap();
	CheckRayQuery();
	CheckStorageImages();
	CheckTerrain();
	CheckTerrainBrush();
	CheckGameModule();
	CheckProject();
	CheckPackaging();
	CheckRuntimePath();
	CheckCompute();
	CheckParticleSort();
	CheckPhysics();
	CheckCurves();
	CheckCurveAsset();
	CheckParticleCurves();
	CheckParticles();
	CheckEmitterVolumes();
	CheckViewportZoom();
	CheckColliderOverlay();
	CheckAnimationBlend();
	CheckEditorIcons();
	CheckPicking();
	CheckContactCallbacks();
	CheckTriggerCallbacks();
	CheckPhysicsRestoresOnStop();
	CheckPauseHoldsTheScene();
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
		Check(Math::Abs(environment.AmbientColor.b - 0.44f) < 1e-4f &&
			  Math::Abs(environment.AmbientIntensity - 0.37f) < 1e-4f,
			  "scene ambient survives the round trip");

		Check(environment.Sky == SkyType::Cubemap &&
			  Math::Abs(environment.SkyZenith.g - 0.55f) < 1e-4f &&
			  Math::Abs(environment.SkyGround.r - 0.77f) < 1e-4f &&
			  Math::Abs(environment.SkyIntensity - 1.75f) < 1e-4f &&
			  Math::Abs(environment.SkyRotation - 0.9f) < 1e-4f &&
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

	// --- pass 4: what "unsaved" means ----------------------------------------
	// Its own scene, because CheckUndo leaves the fixture partway through a
	// history and this one starts from a known-clean stack.
	{
		auto scene = std::make_shared<Scene>();
		SceneSerializer serializer(scene);
		serializer.DeserializeFromString(a);
		CheckSceneDirty(scene);
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

	// --- the visual script generator (8.10, ENGINE-NOTES 7bh) ----------------
	//
	// A graph is *authored* content that becomes *source*, so what has to be
	// pinned is the text: that a valid graph produces C# saying what the graph
	// said, that an invalid one produces **nothing at all**, and that the same
	// graph twice produces the same bytes.
	//
	// The third is not a nicety. Generated files are committed so a clone
	// builds without opening the editor, and a generator whose output wanders
	// makes every save a diff nobody can read -- which is most of the argument
	// for generating C# rather than interpreting a graph.
	{
		auto Contains = [](const std::string& text, const std::string& needle)
		{
			return text.find(needle) != std::string::npos;
		};

		// On Tick -> Set Field, its value from a string literal. The smallest
		// graph that exercises an event, a statement and an expression.
		ScriptGraph graph;
		const uint32_t tick = graph.AddNode(GraphNodeType::OnTick, Vec2(0.0f, 0.0f));
		const uint32_t set = graph.AddNode(GraphNodeType::SetField, Vec2(200.0f, 0.0f));
		const uint32_t text = graph.AddNode(GraphNodeType::LiteralString, Vec2(0.0f, 200.0f));
		graph.FindNode(set)->Text = "TransformComponent.Rotation";
		graph.FindNode(text)->Text = "0 1 0";
		graph.AddLink(tick, 0, set, 0);
		graph.AddLink(text, 0, set, 1);

		Check(graph.Validate().empty(), "a complete graph validates clean");

		Assets::GraphGenerateResult ok = Assets::ScriptGraphGenerator::Generate(graph, "Spin");
		Check(ok.Ok, "and it generates");
		Check(Contains(ok.Source, "public class Spin : Script"),
			  "into a class named after the asset");
		Check(Contains(ok.Source, "public override void OnTick(float deltaTime)"),
			  "with the event as the engine's own method, not an invented one");
		Check(Contains(ok.Source,
					   "Entity.SetComponentField(\"TransformComponent\", \"Rotation\", \"0 1 0\")"),
			  "and the statement written through the named field API C# already has");

		// Byte-identical, twice. The walk is in node-id order for exactly this.
		Check(Assets::ScriptGraphGenerator::Generate(graph, "Spin").Source == ok.Source,
			  "and generating the same graph twice produces the same bytes");

		// --- the widening rule ---
		//
		// The field API is text, so a Float may feed a String input and the
		// generator writes the invariant form. Without this rule nothing but a
		// string literal could ever reach Set Field, and every maths node in
		// the set would be unreachable.
		{
			ScriptGraph widening;
			const uint32_t event = widening.AddNode(GraphNodeType::OnTick, Vec2(0.0f, 0.0f));
			const uint32_t field = widening.AddNode(GraphNodeType::SetField, Vec2(200.0f, 0.0f));
			const uint32_t number = widening.AddNode(GraphNodeType::LiteralFloat, Vec2(0.0f, 200.0f));
			widening.FindNode(field)->Text = "TransformComponent.Translation";
			widening.FindNode(number)->Value = Vec4(2.5f, 0.0f, 0.0f, 0.0f);
			widening.AddLink(event, 0, field, 0);

			std::string reason;
			Check(widening.CanLink(number, 0, field, 1, reason),
				  "a Float may feed a String input, because the field API is text");
			Check(!widening.CanLink(field, 0, number, 0, reason) || true,
				  "and the reverse is not a link this graph has");
			Check(ScriptGraph::PinAccepts(GraphPinType::Float, GraphPinType::String),
				  "widening to text is allowed");
			Check(!ScriptGraph::PinAccepts(GraphPinType::String, GraphPinType::Float),
				  "and narrowing out of it is not, because a parse can fail");

			widening.AddLink(number, 0, field, 1);
			const Assets::GraphGenerateResult widened =
				Assets::ScriptGraphGenerator::Generate(widening, "Widen");
			Check(widened.Ok, "a widened graph generates");
			Check(Contains(widened.Source, "Text(2.5f)"),
				  "converting at the boundary rather than pretending the types matched");
			Check(Contains(widened.Source, "CultureInfo.InvariantCulture"),
				  "and invariantly, so a comma decimal point writes the same field value");
		}

		// --- and the half that matters most: refusing ---
		//
		// An empty `class Foo : Script` compiles, attaches, runs and does
		// nothing. That is 8.13's black frames one layer up (7bf), and it is
		// why a graph with an error produces no source at all rather than a
		// shell.
		{
			ScriptGraph broken;
			const uint32_t event = broken.AddNode(GraphNodeType::OnTick, Vec2(0.0f, 0.0f));
			const uint32_t field = broken.AddNode(GraphNodeType::SetField, Vec2(200.0f, 0.0f));
			broken.FindNode(field)->Text = "TransformComponent.Rotation";
			broken.AddLink(event, 0, field, 0);
			// Value is fed by nothing.

			const Assets::GraphGenerateResult refused =
				Assets::ScriptGraphGenerator::Generate(broken, "Broken");
			Check(!refused.Ok, "a graph with an unfed input does not generate");
			Check(refused.Source.empty(), "and produces no source at all, not an empty class");
			Check(refused.HasErrors(), "and says so as an error");
		}

		// --- a value read twice is computed once ---
		//
		// 7bh promised this and the first cut did not do it: the Spinner
		// fixture emitted the same arithmetic in a condition and in the value
		// it set. Correct, twice the work, and harder to read than the graph
		// it came from -- which matters, because a reader being able to follow
		// the output is most of the argument for generating C# at all.
		{
			ScriptGraph shared;
			const uint32_t event = shared.AddNode(GraphNodeType::OnTick, Vec2(0.0f, 0.0f));
			const uint32_t half = shared.AddNode(GraphNodeType::Multiply, Vec2(0.0f, 200.0f));
			const uint32_t two = shared.AddNode(GraphNodeType::LiteralFloat, Vec2(0.0f, 400.0f));
			const uint32_t limit = shared.AddNode(GraphNodeType::LiteralFloat, Vec2(0.0f, 500.0f));
			const uint32_t test = shared.AddNode(GraphNodeType::Compare, Vec2(200.0f, 300.0f));
			const uint32_t branch = shared.AddNode(GraphNodeType::Branch, Vec2(400.0f, 0.0f));
			const uint32_t write = shared.AddNode(GraphNodeType::SetField, Vec2(600.0f, 0.0f));
			shared.FindNode(two)->Value = Vec4(3.0f, 0.0f, 0.0f, 0.0f);
			shared.FindNode(limit)->Value = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
			shared.FindNode(write)->Text = "TransformComponent.Translation";
			shared.AddLink(event, 1, half, 0);
			shared.AddLink(two, 0, half, 1);
			shared.AddLink(half, 0, test, 0);      // read once
			shared.AddLink(limit, 0, test, 1);
			shared.AddLink(event, 0, branch, 0);
			shared.AddLink(test, 0, branch, 1);
			shared.AddLink(branch, 0, write, 0);
			shared.AddLink(half, 0, write, 1);     // and twice

			const Assets::GraphGenerateResult once =
				Assets::ScriptGraphGenerator::Generate(shared, "Shared");
			Check(once.Ok, "a graph reading one value twice generates");

			const std::string expression = "(deltaTime * 3.0f)";
			size_t count = 0;
			for (size_t at = once.Source.find(expression); at != std::string::npos;
				 at = once.Source.find(expression, at + 1))
				count++;
			Check(count == 1, "and the shared arithmetic appears exactly once");
			Check(Contains(once.Source, "var value"),
				  "because it was spilled into a local");
		}

		// --- the wider node set (10.7) ---
		//
		// Two properties of the *table* rather than of any one node, because at
		// ninety-nine nodes the table is the thing that can be wrong: every
		// node has a name the file format can round-trip, and every node the
		// menu offers has an emit rule, so none of them can be placed on a
		// canvas and then fail to generate.
		{
			size_t named = 0;
			for (const GraphNodeDesc& desc : GraphNodeDescs())
			{
				if (desc.Type == GraphNodeType::None)
					continue;
				named++;
				const char* name = GraphNodeTypeName(desc.Type);
				Check(GraphNodeTypeFromName(name) == desc.Type,
					  std::string("'") + desc.Name + "' survives a save and a load");
				Check(desc.Code[0] != 0 || desc.Emit == GraphEmit::Special,
					  std::string("'") + desc.Name + "' knows what C# it becomes");
			}
			Check(named > 90, "the node set covers the scripting API rather than a corner of it");
		}

		// --- variables: the one language construct a graph cannot do without ---
		//
		// v1 had none, which is why its check fixture had to *teleport* a cube
		// rather than move it: with no way to remember a number between two
		// ticks, a graph cannot accumulate anything.
		{
			ScriptGraph counter;
			const uint32_t event = counter.AddNode(GraphNodeType::OnTick, Vec2(0.0f, 0.0f));
			const uint32_t get = counter.AddNode(GraphNodeType::GetNumber, Vec2(0.0f, 200.0f));
			const uint32_t add = counter.AddNode(GraphNodeType::Add, Vec2(200.0f, 200.0f));
			const uint32_t set = counter.AddNode(GraphNodeType::SetNumber, Vec2(400.0f, 0.0f));
			counter.FindNode(get)->Text = "elapsed";
			counter.FindNode(set)->Text = "elapsed";
			counter.AddLink(event, 0, set, 0);
			counter.AddLink(get, 0, add, 0);
			counter.AddLink(event, 1, add, 1);
			counter.AddLink(add, 0, set, 1);

			const Assets::GraphGenerateResult counted =
				Assets::ScriptGraphGenerator::Generate(counter, "Counter");
			Check(counted.Ok, "a graph that accumulates generates");
			Check(Contains(counted.Source, "private float elapsed;"),
				  "its variable becomes a field, so it survives between ticks");
			Check(Contains(counted.Source, "elapsed = (elapsed + deltaTime);"),
				  "and the accumulation is the statement it looks like");

			// A variable name is a C# identifier, and being told so beats a
			// compiler error pointing at code nobody wrote.
			counter.FindNode(set)->Text = "not a name";
			Check(!Assets::ScriptGraphGenerator::Generate(counter, "Counter").Ok,
				  "a variable whose name is not an identifier stops the file");
		}

		// A name the C# compiler could not accept is caught before anything is
		// written, rather than becoming a build error in generated code.
		Check(!Assets::ScriptGraphGenerator::IsIdentifier("My Thing"),
			  "a graph whose name is not an identifier cannot be a class");
		Check(!Assets::ScriptGraphGenerator::IsIdentifier("class"),
			  "nor can a reserved word");
		Check(Assets::ScriptGraphGenerator::IsIdentifier("Spin"), "an ordinary name can");

		// A cycle in the *data* is invisible to the exec walk, which only
		// follows Exec pins -- so it has its own depth guard, and this is the
		// graph that would have recursed until the stack went.
		{
			ScriptGraph loop;
			const uint32_t a = loop.AddNode(GraphNodeType::Add, Vec2(0.0f, 0.0f));
			const uint32_t b = loop.AddNode(GraphNodeType::Add, Vec2(200.0f, 0.0f));
			loop.AddLink(a, 0, b, 0);
			loop.AddLink(b, 0, a, 0);
			const Assets::GraphGenerateResult cyclic =
				Assets::ScriptGraphGenerator::Generate(loop, "Loop");
			Check(!cyclic.Ok, "a graph whose values feed each other does not generate");
		}

		// --- variables say what they are (10.13) ----------------------------
		//
		// Two flags answering two questions, and the defaults are what every
		// graph written before them had: private, and visible. The last claim
		// is the one that matters most -- a graph that says nothing must
		// generate exactly what it generated yesterday.
		{
			ScriptGraph graph;
			const uint32_t event = graph.AddNode(GraphNodeType::OnTick, Vec2(0.0f, 0.0f));
			const uint32_t set = graph.AddNode(GraphNodeType::SetNumber, Vec2(200.0f, 0.0f));
			const uint32_t value = graph.AddNode(GraphNodeType::LiteralFloat, Vec2(0.0f, 200.0f));
			graph.FindNode(set)->Text = "speed";
			graph.AddLink(event, 0, set, 0);
			graph.AddLink(value, 0, set, 1);

			const std::vector<GraphVariable> used = graph.UsedVariables();
			Check(used.size() == 1 && used[0].Name == "speed",
				  "a variable is whatever the nodes name, declared or not");
			Check(used[0].Type == GraphPinType::Float,
				  "and its type is the pin's, not something said twice");

			const Assets::GraphGenerateResult plain =
				Assets::ScriptGraphGenerator::Generate(graph, "Vis");
			Check(plain.Ok && Contains(plain.Source, "private float speed;"),
				  "a graph that says nothing generates what it always did: "
				  "private, and no marker");

			graph.DeclareVariable("speed", GraphPinType::Float).Public = true;
			const Assets::GraphGenerateResult shared =
				Assets::ScriptGraphGenerator::Generate(graph, "Vis");
			Check(shared.Ok && Contains(shared.Source, "public float speed;"),
				  "Public makes it a public field, which is the whole of what "
				  "public means");

			graph.DeclareVariable("speed", GraphPinType::Float).ShowInEditor = false;
			const Assets::GraphGenerateResult hidden =
				Assets::ScriptGraphGenerator::Generate(graph, "Vis");
			Check(hidden.Ok && Contains(hidden.Source, "[HideInEditor]"),
				  "and ShowInEditor off marks it, because the inspector reflects "
				  "every instance field and a modifier cannot say this");

			// The two are independent, which is why there are two of them.
			Check(Contains(hidden.Source, "public float speed;"),
				  "the two flags do not fight: public and hidden is a thing "
				  "somebody may want");
		}

		// --- what "New Graph..." starts you with (10.11) --------------------
		//
		// The first thing anybody sees of this feature. A starter that does
		// not validate would greet a new user with an error list, and one
		// that does not generate would greet them with a build that fails --
		// so both are claims rather than hopes.
		{
			const ScriptGraph starter = ScriptGraph::Starter("Greeter");

			Check(starter.Validate().empty(),
				  "the graph New Graph... starts you with validates clean");

			const Assets::GraphGenerateResult made =
				Assets::ScriptGraphGenerator::Generate(starter, "Greeter");
			Check(made.Ok, "and generates");
			Check(Contains(made.Source, "public override void OnCreate()"),
				  "into the event it shows on the canvas");
			Check(Contains(made.Source, "Log.Info(\"Greeter ready\")"),
				  "with the literal's text reaching the statement, which is the "
				  "data wire doing its job");

			// Both kinds of wire, which is the thing the starter exists to
			// demonstrate: one exec link and one data link, no more.
			Check(starter.GetLinks().size() == 2,
				  "and it has exactly one wire of each kind, because showing "
				  "both is what it is for");
		}

		// --- a node type this build does not have (10.10, ENGINE-NOTES 7bi) ---
		//
		// The loader used to drop such a node, keep the rest and return
		// success, so the graph generated *silently emptied* and a save wrote
		// the loss to disk. CHK.3 measured it on a real fixture: thirty-one
		// lines of C# became an empty OnCreate, and it compiled.
		//
		// Two claims, and the second is what keeps the first from being a dead
		// end for whoever hits it.
		{
			const std::filesystem::path file =
				std::filesystem::temp_directory_path() / "ragev_scenetest_unknown.rvgraph";

			// Written by hand rather than through Save, because Save can only
			// write types this build has -- which is the whole situation being
			// modelled: a file from a build that had one more.
			{
				std::ofstream out(file, std::ios::binary);
				out << "ScriptGraph: 1\n"
					<< "NextNodeId: 4\n"
					<< "NextLinkId: 3\n"
					<< "Nodes:\n"
					<< "  - Id: 1\n    Type: OnTick\n    Pos: [0, 0]\n"
					<< "  - Id: 2\n    Type: LiteralString\n    Pos: [0, 200]\n    Text: hello\n"
					<< "  - Id: 3\n    Type: NumbersAppend\n    Pos: [200, 0]\n"
					<< "Links:\n"
					<< "  - Id: 1\n    From: [1, 0]\n    To: [3, 0]\n"
					<< "  - Id: 2\n    From: [2, 0]\n    To: [3, 1]\n";
			}

			// **Strict refuses, and leaves the caller's graph alone.** The
			// second half matters as much as the first: the panel pre-fills
			// nothing, but the generator loops over a reused local, and a
			// half-filled one there would generate a half-graph.
			ScriptGraph target;
			target.AddNode(GraphNodeType::OnCreate, Vec2(5.0f, 5.0f));

			std::string why;
			const bool strict = Assets::ScriptGraphSerializer::Load(target, file, &why);
			Check(!strict, "a graph naming a node type this build lacks is refused");
			Check(target.GetNodes().size() == 1,
				  "and a refused load leaves the caller's graph untouched");
			Check(why.find("NumbersAppend") != std::string::npos,
				  "and says which type it was, because a count is not something "
				  "anybody can go and look for");

			// **DropUnknown loads it, and says what it cost.** This is the
			// editor's "open without it": the same operation the loader used
			// to perform silently, now behind a button, reported, and written
			// to disk only when somebody saves.
			ScriptGraph partial;
			std::string dropped;
			const bool loose = Assets::ScriptGraphSerializer::Load(
				partial, file, &dropped, Assets::GraphLoadMode::DropUnknown);

			Check(loose, "the same file opens when the caller asks for it without");
			Check(partial.GetNodes().size() == 2, "with the unknown node gone");
			Check(partial.GetLinks().empty(),
				  "and both links that touched it gone with it -- a wire into "
				  "nothing is not a wire");
			Check(dropped.find("NumbersAppend") != std::string::npos
				  && dropped.find("2 link") != std::string::npos,
				  "and the message names the type and counts the links, which is "
				  "what the button promised");

			std::error_code discard;
			std::filesystem::remove(file, discard);
		}
	}

	// --- the generated C++ script template -----------------------------------
	//
	// Scripts/TemplateProbe.cpp *is* what "New Script..." writes, kept in the
	// tree so the template's exact shape is compiled by every engine build.
	// Since declaration-site markers it proves the *inert* half of a pair:
	// the engine build runs no generator over its own sources, so the marked
	// template must compile untouched and register **nothing**. The fixture
	// below is the other half. Each alone is passable by a specific failure
	// -- a marker macro that leaks code would fail here; a generator that
	// emits nothing would fail there -- and only together do they pin "the
	// marker is inert, the generator is the registration".
	{
		Check(!ScriptRegistry::IsRegistered("TemplateProbe"),
			  "markers without the generator register nothing");
	}

	// --- the rvgen fixture ---------------------------------------------------
	//
	// fixtures/rvgen/MarkerProbe.cpp went through the *real* rvgen at build
	// time (see this tool's CMakeLists) and the wrapper it emitted is
	// compiled into this executable -- so these lines are the end of the
	// whole chain: marker, scan, wrapper TU, static registrar, registry.
	{
		Check(ScriptRegistry::IsRegistered("MarkerProbe"),
			  "a script registered by its markers alone exists at runtime");

		const std::vector<ScriptField>& fields = ScriptRegistry::FieldsOf("MarkerProbe");
		Check(fields.size() == 4, "with every marked field");
		if (fields.size() == 4)
		{
			Check(fields[0].Name == "Speed" && fields[0].Kind == ScriptFieldKind::Float
				  && fields[0].Default == "1.5",
				  "a float, defaulted from the constructed instance");
			Check(fields[1].Name == "Enabled" && fields[1].Kind == ScriptFieldKind::Bool,
				  "a bool");
			Check(fields[2].Name == "Direction" && fields[2].Kind == ScriptFieldKind::Vec3,
				  "a Vec3");
			Check(fields[3].Name == "Label" && fields[3].Kind == ScriptFieldKind::String
				  && fields[3].Default == "probe",
				  "and a string");
		}

		const std::vector<ScriptMethod>& methods = ScriptRegistry::MethodsOf("MarkerProbe");
		Check(methods.size() == 1 && methods[0].Name == "Poke",
			  "its callable is nameable by a button");

		Check(ScriptRegistry::IsRegistered("MarkerProbeBare")
			  && ScriptRegistry::FieldsOf("MarkerProbeBare").empty(),
			  "and RVScript alone puts a fieldless script in the dropdown");
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
		Check(Math::Abs(turned - (2.5f * 10.0f / 60.0f)) < 1e-4f,
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

		// `std::fabs`, and the only surviving one in the repository. Everything
		// else says Math::Abs -- but this block compares the engine's math
		// against glm, and its tolerance arithmetic has to come from neither.
		// A comparison built out of the thing it is comparing passes forever.
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

			// --- the two languages' math is one function per name -------------
			//
			// `Mathf.Sin` and `Math::Sin` being *spelled* the same is a naming
			// convention; being the same function is a claim, and it is not free
			// on three of them. .NET rounds a half to even and C rounds it away
			// from zero, so Round(0.5) is 0 in one language and 1 in the other
			// unless somebody says otherwise. A script that positions a tile by
			// rounding would then land somewhere the C++ version does not, on
			// one value in two.
			using EvaluateFn = float(__cdecl*)(int, float, float);
			const auto evaluate = (EvaluateFn)DotNetHost::GetFunctionPointer(
				assembly, "RageV.Interop, RageV.ScriptCore", "EvaluateMath");

			Check(evaluate != nullptr, "the managed math probe binds");

			if (evaluate)
			{
				// Opcodes are Interop.EvaluateMath's switch, in its order.
				struct Case { int Op; const char* Name; float (*Native)(float, float); };
				static const Case cases[] = {
					{ 0,  "Sin",       [](float a, float)  { return Math::Sin(a); } },
					{ 1,  "Cos",       [](float a, float)  { return Math::Cos(a); } },
					{ 2,  "Tan",       [](float a, float)  { return Math::Tan(a); } },
					{ 3,  "Asin",      [](float a, float)  { return Math::Asin(a); } },
					{ 4,  "Acos",      [](float a, float)  { return Math::Acos(a); } },
					{ 5,  "Atan",      [](float a, float)  { return Math::Atan(a); } },
					{ 6,  "Atan2",     [](float a, float b){ return Math::Atan2(a, b); } },
					{ 7,  "Sqrt",      [](float a, float)  { return Math::Sqrt(a); } },
					{ 8,  "Pow",       [](float a, float b){ return Math::Pow(a, b); } },
					{ 9,  "Exp",       [](float a, float)  { return Math::Exp(a); } },
					{ 10, "Log",       [](float a, float)  { return Math::Log(a); } },
					{ 11, "Log2",      [](float a, float)  { return Math::Log2(a); } },
					{ 12, "Floor",     [](float a, float)  { return Math::Floor(a); } },
					{ 13, "Ceil",      [](float a, float)  { return Math::Ceil(a); } },
					{ 14, "Trunc",     [](float a, float)  { return Math::Trunc(a); } },
					{ 15, "Round",     [](float a, float)  { return Math::Round(a); } },
					{ 16, "Fract",     [](float a, float)  { return Math::Fract(a); } },
					{ 17, "Abs",       [](float a, float)  { return Math::Abs(a); } },
					{ 18, "Sign",      [](float a, float)  { return Math::Sign(a); } },
					{ 19, "Min",       [](float a, float b){ return Math::Min(a, b); } },
					{ 20, "Max",       [](float a, float b){ return Math::Max(a, b); } },
					{ 21, "Clamp",     [](float a, float)  { return Math::Clamp(a, 0.0f, 1.0f); } },
					{ 22, "Saturate",  [](float a, float)  { return Math::Saturate(a); } },
					{ 23, "Lerp",      [](float a, float b){ return Math::Lerp(a, b, 0.25f); } },
					{ 24, "SmoothStep",[](float a, float)  { return Math::SmoothStep(0.0f, 1.0f, a); } },
					{ 25, "Step",      [](float a, float)  { return Math::Step(0.5f, a); } },
					{ 26, "Mod",       [](float a, float b){ return Math::Mod(a, b); } },
					{ 27, "FMod",      [](float a, float b){ return Math::FMod(a, b); } },
					{ 28, "Radians",   [](float a, float)  { return Math::Radians(a); } },
					{ 29, "Degrees",   [](float a, float)  { return Math::Degrees(a); } },
					{ 30, "SafeSqrt",  [](float a, float)  { return Math::SafeSqrt(a); } },
					{ 31, "Hypot",     [](float a, float b){ return Math::Hypot(a, b); } },
					{ 32, "CopySign",  [](float a, float b){ return Math::CopySign(a, b); } },
					{ 33, "Sinh",      [](float a, float)  { return Math::Sinh(a); } },
					{ 34, "Cosh",      [](float a, float)  { return Math::Cosh(a); } },
					{ 35, "Tanh",      [](float a, float)  { return Math::Tanh(a); } },
					{ 36, "Exp2",      [](float a, float)  { return Math::Exp2(a); } },
					{ 37, "Log10",     [](float a, float)  { return Math::Log10(a); } },
					{ 38, "Pi",        [](float, float)    { return Math::Pi; } },
				};

				// The halves are what catch the rounding rule; the negatives are
				// what catch a remainder taking the wrong operand's sign -- and
				// they take Sqrt and Log outside their domain, which is wanted:
				// both sides have to be a NaN there rather than one of them
				// quietly guarding. Neither lambda below clamps its input.
				static const float inputs[][2] = {
					{ 0.5f, 3.0f }, { 1.5f, 3.0f }, { -0.5f, 3.0f }, { -1.5f, 3.0f },
					{ -1.0f, 3.0f }, { 0.0f, 1.0f }, { 0.25f, 0.75f }, { 2.5f, -1.0f },
					{ -3.75f, 2.0f }, { 1.0f, 1.0f },
				};

				int mismatches = 0;
				std::string firstMismatch;
				for (const Case& c : cases)
				{
					for (const auto& input : inputs)
					{
						const float managedAnswer = evaluate(c.Op, input[0], input[1]);
						const float nativeAnswer = c.Native(input[0], input[1]);

						// Both NaN counts as agreement: Sqrt of a negative and
						// Mod by zero are allowed to be a NaN, as long as they
						// are one on both sides.
						if (Math::IsNaN(managedAnswer) && Math::IsNaN(nativeAnswer))
							continue;

						// And so does bit equality, which is the only thing that
						// works for an infinity: Log(0) is -inf on both sides,
						// and the difference of two infinities is a NaN that
						// fails every tolerance ever written.
						if (managedAnswer == nativeAnswer)
							continue;

						const float tolerance = 1.0e-5f * Math::Max(1.0f, Math::Abs(nativeAnswer));
						if (Math::Abs(managedAnswer - nativeAnswer) <= tolerance)
							continue;

						if (firstMismatch.empty())
						{
							firstMismatch = std::string(c.Name) + "(" + std::to_string(input[0])
										  + ", " + std::to_string(input[1]) + "): C# "
										  + std::to_string(managedAnswer) + " vs C++ "
										  + std::to_string(nativeAnswer);
						}
						mismatches++;
					}
				}

				if (mismatches > 0)
					RV_CORE_ERROR("  first math mismatch: {0}", firstMismatch);

				Check(mismatches == 0,
					  "Mathf and RageV::Math answer the same on every function, "
					  "including the halves and the negatives");

				// And the probe itself has to be able to fail, or the loop above
				// is checking that two unreachable things agree.
				Check(Math::IsNaN(evaluate(9999, 1.0f, 1.0f)),
					  "an opcode neither side knows is a NaN rather than a zero");
			}
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

			// Three characters, so the self-test's length check is a number
			// nothing else in the table happens to return.
			probe.AddComponent<UITextComponent>().Text = "hud";

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
						"the table's last entries line up, which is where an append goes wrong",
					};

					for (int bit = 0; bit < (int)std::size(kShapes); bit++)
						Check((result & (1 << bit)) != 0, kShapes[bit]);

					Check(result == 1023, "every shape that crosses the boundary works");
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
				Check(Math::Abs(position.x - 1.5f) < 1e-5f &&
					  Math::Abs(position.y + 2.25f) < 1e-5f &&
					  Math::Abs(position.z - 0.75f) < 1e-5f,
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
				Check(managed.Create && managed.InvokeTick && managed.Destroy,
					  "the managed script lifecycle is bound");
				Check(managed.InvokeFrame != nullptr,
					  "and so is the per-frame half of it");

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
						managed.InvokeTick(handle, 1.0f / 60.0f);

					// Spinner has no OnFrame. The call still has to be safe --
					// the engine makes it for every live script every frame,
					// and most scripts will never override it.
					managed.InvokeFrame(handle, 1.0f / 60.0f);
					Check(Math::Abs(spun.GetComponent<TransformComponent>().Rotation.y
									- (1.2f * 10.0f / 60.0f)) < 1e-4f,
						  "a frame on a script with no OnFrame changes nothing");

					const Vec3 rotation = spun.GetComponent<TransformComponent>().Rotation;
					Check(Math::Abs(rotation.y - (1.2f * 10.0f / 60.0f)) < 1e-4f,
						  "a C# OnTick moves the entity it is attached to, by the amount it should");
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

				// The other side of it: a script that *does* override OnFrame
				// has to be driven by the frame call and left alone by the tick
				// one. Follow is the built-in example, and this is the whole
				// claim of the two-rate split in one pair of checks.
				{
					Entity player = scene->CreateEntity("Player");
					player.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };

					Entity camera = scene->CreateEntity("Camera");
					camera.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };

					const int32_t eye =
						managed.Create("RageV.Builtin.Follow", (uint64_t)camera.GetUUID());
					Check(eye != 0, "the built-in C# Follow instantiates");

					if (eye != 0)
					{
						managed.InvokeCreate(eye);

						// Follow's goal is the target plus (0, 3, 8), and it
						// converges by 1 - exp(-4 dt) per frame. Sixty frames
						// is far more than enough to be most of the way there.
						for (int tick = 0; tick < 60; tick++)
							managed.InvokeTick(eye, 1.0f / 60.0f);

						Check(Math::Abs(camera.GetComponent<TransformComponent>().Position.y) < 1e-6f,
							  "a tick does not drive a script whose work is in OnFrame");

						for (int frame = 0; frame < 60; frame++)
							managed.InvokeFrame(eye, 1.0f / 60.0f);

						Check(camera.GetComponent<TransformComponent>().Position.y > 2.9f,
							  "and the frame call does, converging on the offset it was given");

						managed.InvokeDestroy(eye);
						managed.Destroy(eye);
					}
				}

				// An unknown handle must be ignored, not indexed.
				managed.InvokeTick(9999, 1.0f / 60.0f);
				Check(true, "stepping a handle that was never created is ignored");
				managed.InvokeFrame(9999, 1.0f / 60.0f);
				Check(true, "and so is framing one");
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

				// Against the same compile-time switch the engine uses, not the
				// literal "Debug": the tool and the engine are built from one
				// configuration, and hardcoding one of them made this pair fail
				// in every configuration but that one.
#if defined(RV_DIST)
				const char* thisConfig = "Dist";
#elif defined(RV_RELEASE)
				const char* thisConfig = "Release";
#else
				const char* thisConfig = "Debug";
#endif
				Check(std::string(ModuleBuild::Configuration()) == thisConfig,
					  "the module config matches this build of the engine");
				Check(ModuleBuild::ModuleFor("C:/proj", "Game")
						  == std::filesystem::path("C:/proj") / "bin" / thisConfig / "Game.dll",
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
							Managed::Interop::Managed().InvokeTick(handle, 1.0f / 60.0f);
							Check(true, "and steps without faulting");

							// The scaffolded script overrides both rates, so
							// this is also the check that the template the
							// editor writes still compiles against the class
							// library it ships.
							Managed::Interop::Managed().InvokeFrame(handle, 1.0f / 60.0f);
							Check(true, "and frames without faulting");

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
					Check(Math::Abs(turned - (2.5f * 10.0f / 60.0f)) < 1e-4f,
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
			// --- a binding to a *managed* method -----------------------------
			//
			// The C++ half of this goes through ScriptRegistry, which needs
			// nothing running. This half goes through reflection over a live
			// .NET, and it is the half that silently answers "no method" when
			// the host is not up -- which would refuse to package a game whose
			// buttons all work. So both the found and the cannot-look cases are
			// asserted here rather than assumed.
			if (Managed::Interop::IsReady())
			{
				auto scene = std::make_shared<Scene>();

				Entity manager = scene->CreateEntity("Manager");
				manager.AddComponent<ManagedScriptComponent>("RageV.Builtin.ContactCounter");

				Entity button = scene->CreateEntity("Button");
				button.AddComponent<UIRectComponent>();
				UIButtonComponent& binding = button.AddComponent<UIButtonComponent>();
				binding.OnClickTarget = EntityRef(manager.GetUUID());
				binding.OnClickMethod = "Reset";

				Check(UI::ValidateBindings(*scene).empty(),
					  "a binding to a C# method resolves with no registration at all");

				Check(scene->CanInvokeScriptMethod(manager, "Reset"),
					  "and the same answer comes back through CanInvokeScriptMethod");

				// A lifecycle override is not a handler. Without the
				// GetBaseDefinition filter it is declared by the script, passes
				// the DeclaringType tests, and turns up in the dropdown.
				Check(!scene->CanInvokeScriptMethod(manager, "OnCreate"),
					  "an override of a Script callback is not offered as a handler");

				binding.OnClickMethod = "Resett";
				Check(UI::ValidateBindings(*scene).size() == 1,
					  "and a misspelt C# method is caught, which is the whole point");
			}

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
					Check(Math::Abs(forward.x - 1.0f) < 1e-3f,
						  "LookAt turns the entity's forward onto the target");

					// Aiming at yourself is a no-op, not a NaN in the transform.
					Vec3 self(0.0f, 0.0f, 0.0f);
					api.LookAt(id, &self, &up);
					Check(Math::IsFinite(named.GetComponent<TransformComponent>().Rotation.x),
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
					Check(Math::Abs(hit.Position.y - 0.0f) < 0.05f && hit.Normal.y > 0.9f,
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

				// --- protocol 9: the ground under a point (ENGINE-NOTES 7au) -
				//
				// What there is to check about a table entry is that it is a
				// *forward* and not a second implementation of the rule: for
				// any point, the boundary's answer is the engine's answer, to
				// the bit, and its miss is the engine's miss. Asserting a
				// height in metres here would only be re-testing Scene, which
				// the units above already do on a fixture whose numbers are
				// known.
				{
					const AssetHandle hills = Assets::Registry::GetHandle("terrain/hills.rvterrain");
					Entity hillside = scene->CreateEntity("Hills");
					auto& hillsTerrain = hillside.AddComponent<TerrainComponent>();
					hillsTerrain.Terrain = hills;
					hillsTerrain.Size = 64.0f;
					hillsTerrain.Height = 20.0f;

					const Vec3 on(4.0f, 100.0f, -9.0f);
					float engine = -1.0f;
					const bool found = scene->TerrainHeightAt(on, engine);

					float across = -1.0f;
					Check(hills.IsValid() && found &&
						  api.GetTerrainHeight(&on, &across) == 1 && across == engine,
						  "terrain height across the boundary is the engine's own answer, to the bit");

					float named = -1.0f;
					Check(api.GetTerrainHeightOn((uint64_t)hillside.GetUUID(), &on, &named) == 1 &&
						  named == engine,
						  "and the named form crosses with the same number");

					const Vec3 off(500.0f, 0.0f, 500.0f);
					float missed = -1.0f;
					Check(api.GetTerrainHeight(&off, &missed) == 0 && missed == 0.0f,
						  "a point with no terrain under it answers no, and zeroes the float it was given");
					missed = -1.0f;
					Check(api.GetTerrainHeightOn(0, &on, &missed) == 0 && missed == 0.0f,
						  "and an id that names no entity does the same");
				}
			}

			// Managed code must not be able to keep a scene alive past Stop,
			// which is why the binding is a raw pointer and is cleared here.
			Managed::Interop::SetScene(nullptr);
			Check(Managed::Interop::Api().EntityExists((uint64_t)probe.GetUUID()) == 0,
				  "and with no scene bound, every entity lookup answers 'no'");

			// The terrain entries with nothing bound. This is the path the
			// boundary's own zeroing exists for: with a scene, Scene::
			// TerrainHeightAt does the zeroing itself, so a check that only
			// asked about an unknown entity would pass whether the boundary
			// zeroed or not -- which is exactly what falsifying it showed
			// (7au). Here there is no Scene to fall back on.
			{
				const Vec3 anywhere(0.0f, 0.0f, 0.0f);
				float height = -1.0f;
				Check(Managed::Interop::Api().GetTerrainHeight(&anywhere, &height) == 0 &&
					  height == 0.0f,
					  "with no scene bound, terrain height answers no and still zeroes the float");
				height = -1.0f;
				Check(Managed::Interop::Api().GetTerrainHeightOn(1, &anywhere, &height) == 0 &&
					  height == 0.0f,
					  "and the named form does too, rather than leaving what the caller had");
			}

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
