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

	RV_CORE_INFO("Scene round-trip test on {0}", device->GetCaps().APIName);

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
