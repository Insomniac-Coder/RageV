#include <rvpch.h>
#include "AssetManager.h"
#include "ImportCache.h"
#include "ModelImporter.h"
#include "RageV/IO/VFS.h"
#include "RageV/Core/Log.h"
#include "RageV/Renderer/TextureLoader.h"
#include "RageV/Renderer/EnvironmentIBL.h"
#include "RageV/Renderer/ProbeArray.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/SceneSerializer.h"
#include "CurveSerializer.h"
#include "ScriptGraphSerializer.h"
#include "FontSerializer.h"
#include "TerrainSerializer.h"
#include "PostProfileSerializer.h"
#include "CubeLut.h"
// The import splits glTF's packed metallic-roughness texture into the two
// greyscale maps the shader samples, which means reading a PNG and writing two.
#include "stb_image.h"
#include "stb_write_image.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace RageV::Assets
{
	namespace
	{
		RHI::RHIDevice* s_Device = nullptr;

		// Loaded meshes, by handle. A model with several primitives produces
		// several meshes, so the cache is keyed on the sub-handle rather than
		// on the file.
		std::unordered_map<AssetHandle, RHI::Ref<Mesh>> s_Meshes;
		// Cached beside the meshes and cleared with them: they come out of the
		// same parse, so fetching them separately would read the file twice.
		std::unordered_map<AssetHandle, Skeleton> s_Skeletons;
		std::unordered_map<AssetHandle, std::vector<Anim::Clip>> s_Clips;

		// Environment maps, keyed on the handle rather than the path, so a scene
		// that reloads keeps the cube it already paid to build.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_Cubemaps;
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_Irradiance;

		// Plain 2D textures -- particle sprites today, material maps whenever
		// materials become assets. Failures cache as null like the cube's do.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_Textures;
		// The same files read linearly: normal, roughness, metallic and
		// occlusion maps are numbers stored in an image, not pictures.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_DataTextures;
		// Materials, by handle. Shared on purpose: two entities pointing at one
		// `.rmat` must get the same object, or their batch keys differ and the
		// draws never merge -- which is the whole reason materials became
		// assets. Failures cache as null like the textures do.
		std::unordered_map<AssetHandle, RHI::Ref<Material>> s_Materials;
		// Curves are small and pure data, so they cache by value. A failure
		// caches as an empty curve for the same reason a texture caches as
		// null: a missing file must not be retried sixty times a second.
		std::unordered_map<AssetHandle, Curve> s_Curves;
		// A failed load is an absent entry plus its reason, the shape
		// s_FontFailed uses below -- not an empty graph, which the panel
		// cannot tell from a graph somebody has just created (10.10).
		std::unordered_map<AssetHandle, ScriptGraph> s_ScriptGraphs;
		std::unordered_map<AssetHandle, std::string> s_ScriptGraphErrors;
		std::unordered_map<AssetHandle, Curve::Baked> s_BakedCurves;

		// Post profiles cache by value for the same reason curves do: half a
		// dozen numbers, read once per frame by whichever camera names them.
		// A failure caches as the *defaults*, which is deliberate -- a camera
		// pointing at a profile that will not load renders the neutral grade,
		// which is what a camera with no profile at all renders, so a broken
		// reference degrades to "no grading" rather than to a black frame.
		std::unordered_map<AssetHandle, PostSettings> s_PostProfiles;

		// Grading LUTs, as 3D textures. Failures cache as null like every
		// other GPU resource here.
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_ColorLuts;

		// And the recipes behind the `.rvlut` ones, by value like post
		// profiles: eleven numbers, edited in the inspector and baked into the
		// texture above. A `.cube` never appears here -- it has no recipe.
		std::unordered_map<AssetHandle, LutRecipe> s_LutRecipes;

		// Fonts cache by value like curves -- a metrics table is a few thousand
		// numbers. A failure caches as an *absent* entry marked in s_FontFailed
		// rather than as an empty font, because "no glyphs" and "no font" want
		// different answers from the caller and an empty table cannot say which
		// it is.
		std::unordered_map<AssetHandle, Font> s_Fonts;
		std::unordered_set<AssetHandle> s_FontFailed;

		// Terrain heights, the same shape as the fonts: numbers, cached by
		// handle, a failure remembered so a missing file is not reopened
		// every frame.
		std::unordered_map<AssetHandle, TerrainData> s_Terrains;
		std::unordered_set<AssetHandle> s_TerrainFailed;
		std::unordered_set<AssetHandle> s_TerrainDirty;
		std::unordered_map<AssetHandle, RHI::Ref<RHI::RHITexture>> s_FontAtlases;

		constexpr PrimitiveType kPrimitives[] = {
			PrimitiveType::Cube, PrimitiveType::Sphere, PrimitiveType::Plane,
			PrimitiveType::Cylinder, PrimitiveType::Quad,
		};
	}

	AssetHandle Manager::GetPrimitiveHandle(PrimitiveType type)
	{
		return AssetHandle(BuiltinAssets::kPrimitiveBase + (uint64_t)type);
	}

	bool Manager::IsPrimitive(AssetHandle handle, PrimitiveType& out)
	{
		const uint64_t value = handle;
		if (value < BuiltinAssets::kPrimitiveBase ||
			value >= BuiltinAssets::kPrimitiveBase + std::size(kPrimitives))
			return false;

		out = (PrimitiveType)(value - BuiltinAssets::kPrimitiveBase);
		return true;
	}

	void Manager::Init(RHI::RHIDevice& device)
	{
		s_Device = &device;

		for (PrimitiveType type : kPrimitives)
		{
			Registry::RegisterVirtual(GetPrimitiveHandle(type), AssetType::Mesh,
										   PrimitiveTypeName(type));
		}
	}

	void Manager::Shutdown()
	{
		ClearCache();
		s_Device = nullptr;
	}

	namespace
	{
		// Why an asset was wanted, which decides how it is uploaded.
		//
		// The registry's AssetType is not enough on its own: a texture named
		// by a material must be resolved *through* the material, so that each
		// map gets the colour space its slot needs. Resolving one directly as
		// a standalone texture would build an sRGB view of a normal map and
		// cache it -- correct-looking, and twice the VRAM for a texture
		// nothing samples.
		enum class WantKind
		{
			Mesh,
			Material,
			StandaloneTexture,   // a UI image or a particle sprite: a picture
			Font,
			Curve,
			Cubemap,
			Terrain,             // the heights; the meshes come on first draw
			Nothing,             // audio streams; there is no upload to do
		};

		// One asset the scene refers to, and what it will cost.
		struct Pending
		{
			AssetHandle Handle = AssetHandle::Invalid();
			WantKind Kind = WantKind::Nothing;
			std::filesystem::path Path;
			uint64_t Bytes = 0;
			std::string Label;
		};

		// What PrepareScene found, kept for UploadPrepared to walk. One boot
		// loads one scene, so a single list with a cursor is the whole state
		// this needs.
		std::vector<Pending> s_Prepared;
		size_t s_UploadCursor = 0;
		uint64_t s_PreparedBytes = 0;
		uint64_t s_UploadedBytes = 0;

		// Adds a handle's source file once. Handles repeat constantly -- five
		// entities sharing a material name the same four maps -- and cooking
		// one twice is the exact waste this whole feature exists to remove.
		void Want(AssetHandle handle, WantKind kind, std::vector<Pending>& out,
				  std::unordered_set<AssetHandle>& seen)
		{
			if (!handle.IsValid() || !seen.insert(handle).second)
				return;

			const std::filesystem::path path = Registry::GetAbsolutePath(handle);
			if (path.empty())
				return;   // a primitive or a virtual asset: nothing to read

			Pending pending;
			pending.Handle = handle;
			pending.Kind = kind;
			pending.Path = path;
			pending.Label = Registry::GetMetadata(handle).Path;

			// Size on disk is the weight. Wrong in detail -- a megabyte of
			// glTF and a megabyte of PNG do not cost the same -- and right in
			// shape, which is all a bar needs to be.
			std::error_code error;
			pending.Bytes = std::filesystem::exists(path, error)
						  ? (uint64_t)std::filesystem::file_size(path, error) : 0;
			if (error)
				pending.Bytes = 0;

			out.push_back(std::move(pending));
		}

		// A material's own file, plus every map it names. The `.rmat` is
		// parsed here rather than resolved through GetMaterial because that
		// one builds a GPU material, and this runs on the worker.
		//
		// The maps are added as WantKind::Nothing: they must be *cooked*, so
		// they belong in the list the worker walks, but they must not be
		// *uploaded* individually -- GetMaterial pulls each one with the
		// colour space its slot needs, and that is the only correct way to.
		void WantMaterial(AssetHandle handle, std::vector<Pending>& out,
						  std::unordered_set<AssetHandle>& seen)
		{
			if (!handle.IsValid() || seen.count(handle))
				return;

			const std::filesystem::path path = Registry::GetAbsolutePath(handle);
			Want(handle, WantKind::Material, out, seen);
			if (path.empty())
				return;

			MaterialDesc desc;
			if (!MaterialSerializer::Load(desc, path))
				return;

			for (AssetHandle map : { desc.BaseColorMap, desc.NormalMap, desc.OcclusionMap,
									 desc.EmissiveMap, desc.RoughnessMap, desc.MetallicMap,
									 desc.SpecularMap, desc.HeightMap })
			{
				Want(map, WantKind::Nothing, out, seen);
			}
		}
	}

	void Manager::PrepareScene(Scene& scene, Boot::Progress& progress)
	{
		std::vector<Pending> pending;
		std::unordered_set<AssetHandle> seen;

		// Said out loud, because the gather is not instant: it opens every
		// material in the scene to find the maps each one names. Without this
		// the screen sits on a phase with a blank second line for as long as
		// that takes, which reads as having stopped.
		progress.SetDetail("Reading what the scene refers to");

		auto& registry = scene.GetRegistry();

		for (auto handle : registry.GetView<MeshComponent>())
		{
			const auto& mesh = registry.Get<MeshComponent>(handle);
			Want(mesh.Mesh, WantKind::Mesh, pending, seen);
			WantMaterial(mesh.Material, pending, seen);
		}

		for (auto handle : registry.GetView<UIImageComponent>())
		{
			Want(registry.Get<UIImageComponent>(handle).Texture,
				 WantKind::StandaloneTexture, pending, seen);
		}

		for (auto handle : registry.GetView<ParticleEmitterComponent>())
		{
			const auto& emitter = registry.Get<ParticleEmitterComponent>(handle);
			Want(emitter.Texture, WantKind::StandaloneTexture, pending, seen);
			Want(emitter.SizeCurve, WantKind::Curve, pending, seen);
			Want(emitter.ColorGradient, WantKind::Curve, pending, seen);
			Want(emitter.AlphaCurve, WantKind::Curve, pending, seen);
		}

		for (auto handle : registry.GetView<UITextComponent>())
			Want(registry.Get<UITextComponent>(handle).Font, WantKind::Font, pending, seen);

		for (auto handle : registry.GetView<WorldTextComponent>())
			Want(registry.Get<WorldTextComponent>(handle).Font, WantKind::Font, pending, seen);

		// A terrain's heights, and the four layers it is drawn with (7aq). The
		// chunk meshes are built on the first frame that draws it -- they need
		// the component's dimensions, which the preloader does not resolve --
		// but the file, which is the bytes, is read here.
		for (auto handle : registry.GetView<TerrainComponent>())
		{
			const auto& terrain = registry.Get<TerrainComponent>(handle);
			Want(terrain.Terrain, WantKind::Terrain, pending, seen);
			WantMaterial(terrain.Material, pending, seen);
			WantMaterial(terrain.Layer1, pending, seen);
			WantMaterial(terrain.Layer2, pending, seen);
			WantMaterial(terrain.Layer3, pending, seen);
		}

		// Audio is streamed from disk over a sound's lifetime, so there is
		// nothing to upload -- but it is listed anyway, because a clip still
		// has to be found and a scene whose audio is missing should say so
		// during loading rather than the first time something plays.
		for (auto handle : registry.GetView<AudioSourceComponent>())
			Want(registry.Get<AudioSourceComponent>(handle).Clip, WantKind::Nothing, pending, seen);

		// The sky. Not cookable today -- an `.hdr` still becomes cube faces
		// and an irradiance convolution on every load -- but naming it here
		// keeps it in the bar's denominator, so the phase does not appear to
		// stall on work the bar is pretending does not exist. Uploading it
		// during loading is what moves that convolution off the first frame.
		Want(AssetHandle(scene.GetEnvironment().SkyTexture), WantKind::Cubemap, pending, seen);

		uint64_t total = 0;
		for (const Pending& item : pending)
			total += item.Bytes;

		// Largest first.
		//
		// Longest-processing-time-first: with several workers pulling from
		// one queue, the finish time is decided by whatever is still running
		// at the end, and the worst case is a 40 MB normal map being handed
		// out last while three workers sit idle behind it. Sorting big to
		// small leaves only small pieces for the tail. Free, and it costs
		// nothing on a warm cache where every item is a file read.
		std::sort(pending.begin(), pending.end(),
				  [](const Pending& a, const Pending& b) { return a.Bytes > b.Bytes; });

		// Handed over before the cooking loop, so that a cancelled boot still
		// leaves UploadPrepared with a consistent (if unfinished) list rather
		// than one from the previous scene.
		s_Prepared = pending;
		s_UploadCursor = 0;
		s_PreparedBytes = total;
		s_UploadedBytes = 0;

		if (pending.empty())
		{
			progress.Advance(1.0f);
			return;
		}

		// Cooked in parallel, because that is the slow case and it is
		// embarrassingly parallel: every asset is an independent decode and
		// encode, and the cookers hold nothing but `constexpr` state.
		//
		// **Bounded by memory, not by cores.** Cooking one 4K map holds its
		// mip 0 as float -- 4096x4096x4 floats, 256 MB -- plus the byte
		// buffer it encodes from, so each worker peaks in the hundreds of
		// megabytes. Running one per hardware thread on a 16-thread machine
		// would ask for several gigabytes to save a few seconds, and a
		// machine that starts swapping is slower than the serial version it
		// replaced. Four is the compromise, and the better optimisation is
		// the peak rather than the count.
		//
		// Warm boots do none of this work -- every Fetch is a file read --
		// so the threads cost a spawn and nothing else.
		const unsigned hardware = std::thread::hardware_concurrency();
		const size_t workers = Math::Min<size_t>(
			pending.size(), Math::Max(1u, Math::Min(hardware ? hardware : 1u, 4u)));

		std::atomic<size_t> next{ 0 };
		std::atomic<uint64_t> done{ 0 };
		std::atomic<size_t> finished{ 0 };

		auto cook = [&]()
		{
			for (;;)
			{
				const size_t index = next.fetch_add(1, std::memory_order_relaxed);
				if (index >= pending.size() || progress.Cancelled())
					return;

				const Pending& item = pending[index];

				// Several workers write this, so it flickers between their
				// current files. That is honest -- several files really are
				// being cooked at once -- and it is the only shared state
				// here that is not a counter.
				progress.SetDetail(item.Label);

				// The one call that does the work: cooked bytes if the cache
				// has them, and a decode plus a cook if not. The result is
				// discarded -- what matters is that it is now on disk, so
				// the main thread's upload is a read.
				std::vector<uint8_t> bytes;
				ImportCache::Fetch(item.Path, bytes);

				const uint64_t soFar =
					done.fetch_add(item.Bytes, std::memory_order_relaxed) + item.Bytes;
				const size_t count = finished.fetch_add(1, std::memory_order_relaxed) + 1;

				// Weighted by bytes when the sizes are known, by count when
				// they are not: a project served entirely from a pak has no
				// file sizes to ask for, and an all-zero denominator would
				// pin the bar at zero for the whole phase. Advance is
				// monotonic, so workers finishing out of order cannot make
				// the bar retreat.
				progress.Advance(total > 0 ? (float)((double)soFar / (double)total)
										   : (float)count / (float)pending.size());
			}
		};

		std::vector<std::thread> pool;
		pool.reserve(workers - 1);
		for (size_t i = 1; i < workers; i++)
			pool.emplace_back(cook);

		// This thread takes a share too, rather than waiting on the others.
		cook();

		for (std::thread& worker : pool)
			worker.join();
	}

	bool Manager::UploadPrepared(Boot::Progress& progress, float budgetSeconds)
	{
		if (s_UploadCursor >= s_Prepared.size())
		{
			// Freed rather than left lying around: this is one entry per
			// asset in the scene, and it has no reader after boot.
			s_Prepared.clear();
			s_Prepared.shrink_to_fit();
			return false;
		}

		const auto started = std::chrono::steady_clock::now();

		while (s_UploadCursor < s_Prepared.size())
		{
			const Pending& item = s_Prepared[s_UploadCursor];

			progress.SetDetail(item.Label);

			switch (item.Kind)
			{
			case WantKind::Mesh:
				GetMesh(item.Handle);
				break;
			// Every map with the colour space its slot asks for, which is
			// why the maps are not uploaded one by one.
			case WantKind::Material:
				GetMaterial(item.Handle);
				break;
			case WantKind::StandaloneTexture:
				GetTexture(item.Handle, ColorSpace::Srgb);
				break;
			case WantKind::Font:
				GetFont(item.Handle);
				GetFontAtlas(item.Handle);
				break;
			case WantKind::Curve:
				GetCurve(item.Handle);
				GetBakedCurve(item.Handle);
				break;
			case WantKind::Terrain:
				GetTerrain(item.Handle);
				break;
			// The expensive one: six faces converted from a panorama, plus
			// the irradiance convolution. Doing it here rather than on the
			// first frame is most of what this step is worth.
			case WantKind::Cubemap:
				GetCubemap(item.Handle);
				GetIrradiance(item.Handle);
				break;
			case WantKind::Nothing:
				break;
			}

			s_UploadedBytes += item.Bytes;
			s_UploadCursor++;

			progress.Advance(s_PreparedBytes > 0
				? (float)((double)s_UploadedBytes / (double)s_PreparedBytes)
				: (float)s_UploadCursor / (float)s_Prepared.size());

			// Checked after at least one asset, so a budget smaller than a
			// single upload still makes progress instead of spinning.
			const std::chrono::duration<float> spent =
				std::chrono::steady_clock::now() - started;
			if (spent.count() >= budgetSeconds)
				break;
		}

		return s_UploadCursor < s_Prepared.size();
	}

	void Manager::ClearCache()
	{
		s_Meshes.clear();
		s_Skeletons.clear();
		s_Clips.clear();
		s_Cubemaps.clear();
		s_Irradiance.clear();
		s_Textures.clear();
		s_DataTextures.clear();
		s_Materials.clear();
		s_Curves.clear();
		s_ScriptGraphs.clear();
		s_ScriptGraphErrors.clear();
		s_BakedCurves.clear();
		s_Fonts.clear();
		s_FontFailed.clear();
		s_FontAtlases.clear();
		s_Terrains.clear();
		s_TerrainFailed.clear();
		s_TerrainDirty.clear();

		// The three phase-9 caches, which were added one at a time and each
		// missed this function.
		//
		// `s_ColorLuts` is the one that shows: it holds a 3D texture, so a
		// scene that graded anything ended the process with a live VkImage and
		// Vulkan's validation layer said so at `vkDestroyDevice`. It had been
		// true since 9.1 and nothing caught it, because a leak at shutdown
		// costs nothing a user can see and the checks only look at exit codes.
		// The demo scene is what surfaced it -- the first one to hold a grade
		// at exit *and* be looked at.
		//
		// The other two carry no GPU memory, and are cleared for the reason
		// the note below gives: a cache that survives a project switch hands
		// the new project the old one's assets.
		s_ColorLuts.clear();
		s_LutRecipes.clear();
		s_PostProfiles.clear();

		// The loader and the filter hold the same textures by path and by
		// pointer, and both used to be cleared only at shutdown -- so changing
		// project kept every old project's environment maps alive, and the
		// filter's map was keyed on addresses whose owners it did not know the
		// lifetime of. Cleared together or not at all.
		TextureLoader::ClearCache();
		EnvironmentIBL::ClearCache();
		// The probe arrays hold slices convolved from those same textures, and
		// remember which source filled each slot by address. Keeping them would
		// be keeping a filtered copy of a freed cube, and a slot that believes
		// it is current.
		ProbeArray::ClearCache();
	}

	size_t Manager::Invalidate(const std::filesystem::path& absoluteSource)
	{
		std::error_code error;
		const std::filesystem::path wanted =
			std::filesystem::weakly_canonical(absoluteSource, error);
		const std::filesystem::path& target = error ? absoluteSource : wanted;

		size_t dropped = 0;

		// Whether a cached handle came out of this file. Asked of the registry
		// rather than remembered here, because the registry is the only thing
		// that knows -- and the alternative is a second index to keep in step
		// with it, which is a second thing to get wrong.
		auto isOurs = [&](AssetHandle handle)
		{
			std::error_code cmp;
			const std::filesystem::path path = Registry::GetAbsolutePath(handle);
			if (path.empty())
				return false;
			return std::filesystem::equivalent(path, target, cmp) && !cmp;
		};

		// Erase-if over every cache a source file can end up in. Written out
		// rather than looped over a list of type-erased maps: they hold
		// different types, and the day one of them needs different treatment
		// is the day a clever loop has to be unpicked.
		// Three of these are sets of handles rather than maps -- the "this one
		// failed" and "this one is dirty" notes -- so the key is the element
		// itself. One helper either way, because forgetting a set is how a
		// failure outlives the file that caused it.
		auto sweep = [&](auto& cache)
		{
			for (auto it = cache.begin(); it != cache.end();)
			{
				const AssetHandle handle = [&]
				{
					if constexpr (requires { it->first; })
						return it->first;
					else
						return *it;
				}();

				if (!isOurs(handle))
				{
					++it;
					continue;
				}
				it = cache.erase(it);
				dropped++;
			}
		};

		sweep(s_Meshes);
		sweep(s_Skeletons);
		sweep(s_Clips);
		sweep(s_Cubemaps);
		sweep(s_Irradiance);
		sweep(s_Textures);
		sweep(s_DataTextures);
		sweep(s_Materials);
		sweep(s_Curves);
		sweep(s_BakedCurves);
		sweep(s_ScriptGraphs);
		sweep(s_ScriptGraphErrors);
		sweep(s_Fonts);
		sweep(s_FontFailed);
		sweep(s_FontAtlases);
		sweep(s_Terrains);
		sweep(s_TerrainFailed);
		sweep(s_TerrainDirty);
		sweep(s_ColorLuts);
		sweep(s_LutRecipes);
		sweep(s_PostProfiles);

		// The by-path and by-pointer caches underneath cannot be swept one
		// entry at a time -- the filter and the probe arrays remember their
		// sources by address, and an address whose owner has just been dropped
		// is exactly the dangling case ClearCache's comment describes. A
		// changed environment map is rare enough that clearing all three is
		// the honest answer.
		if (dropped > 0)
		{
			TextureLoader::ClearCache();
			EnvironmentIBL::ClearCache();
			ProbeArray::ClearCache();
		}

		return dropped;
	}

	std::string Manager::GetDisplayName(AssetHandle handle)
	{
		if (!handle.IsValid())
			return "(none)";

		PrimitiveType primitive;
		if (IsPrimitive(handle, primitive))
			return PrimitiveTypeName(primitive);

		const AssetMetadata& metadata = Registry::GetMetadata(handle);
		if (!metadata.IsValid())
			return "(missing)";

		const size_t slash = metadata.Path.find_last_of('/');
		return slash == std::string::npos ? metadata.Path : metadata.Path.substr(slash + 1);
	}

	namespace
	{
		// One primitive of a parsed model, uploaded.
		//
		// Pulled out of GetMesh because two callers need it now -- the cold
		// path and the import -- and because the skinned case is the whole
		// reason it is not two lines.
		RHI::Ref<Mesh> BuildPrimitive(const ImportedModel& model, size_t index)
		{
			const Assets::ImportedPrimitive& source = model.Primitives[index];

			if (!source.IsSkinned())
			{
				return std::make_shared<Mesh>(*s_Device, source.Vertices,
											  source.Indices, source.Name);
			}

			std::vector<SkinnedVertex> skinned;
			skinned.reserve(source.Vertices.size());

			for (size_t i = 0; i < source.Vertices.size(); i++)
			{
				const MeshVertex& vertex = source.Vertices[i];

				SkinnedVertex out;
				out.Position = vertex.Position;
				out.Normal = vertex.Normal;
				out.TexCoord = vertex.TexCoord;
				out.Joints = source.Joints[i];
				out.Weights = source.Weights[i];

				skinned.push_back(out);
			}

			auto mesh = std::make_shared<Mesh>(*s_Device, skinned, source.Indices, source.Name);

			// Bounds that cover the animation rather than the bind pose (7.6).
			// Done here because this is the only place that has the mesh, the
			// skeleton and the clips at once -- the mesh cannot compute it and
			// the animation module cannot reach the mesh.
			std::vector<Vec3> positions;
			positions.reserve(source.Vertices.size());
			for (const MeshVertex& vertex : source.Vertices)
				positions.push_back(vertex.Position);

			AABB animated;
			Anim::SkinnedBounds(model.Skeleton, model.Clips, positions,
								source.Joints, source.Weights,
								animated.Min, animated.Max);

			// Only when it produced something: a model whose weights this could
			// not read keeps the box its vertices gave it.
			if (animated.Min.x <= animated.Max.x)
				mesh->SetBounds(animated);

			return mesh;
		}
	}

	namespace
	{
		// The model a derived mesh handle belongs to, and which primitive of it.
		//
		// A model file holds several primitives, and only the first wears the
		// file's own handle -- the rest are `modelHandle + 1 + index`, minted by
		// InstantiateModel. **A saved scene stores exactly those numbers**, so
		// they have to resolve on a cold load or reopening a scene loses every
		// part of an imported model except one piece.
		//
		// The owner is the nearest registered mesh *below* the handle. Handles
		// are hashes, so "below" is not "nearby" in general -- but a derived
		// handle is its model's plus a small offset, and no other asset can have
		// been minted into that gap. The candidate is still checked by importing
		// it: a model with fewer primitives than the offset asks for is not the
		// owner, and the answer is then a miss rather than a guess.
		AssetHandle OwningModel(AssetHandle handle, uint32_t& primitiveIndex)
		{
			AssetHandle best = AssetHandle::Invalid();

			for (const auto& [path, metadata] : Registry::All())
			{
				if (metadata.Type != AssetType::Mesh)
					continue;

				const uint64_t candidate = (uint64_t)metadata.Handle;
				if (candidate >= (uint64_t)handle)
					continue;

				if (!best.IsValid() || candidate > (uint64_t)best)
					best = metadata.Handle;
			}

			if (!best.IsValid())
				return AssetHandle::Invalid();

			const uint64_t offset = (uint64_t)handle - (uint64_t)best;

			// Offset 0 is the model itself and would not be here; the ceiling
			// is what stops an unrelated model half the handle space away from
			// being imported to prove it is unrelated.
			constexpr uint64_t kMaxPrimitives = 1u << 20;
			if (offset == 0 || offset > kMaxPrimitives)
				return AssetHandle::Invalid();

			primitiveIndex = (uint32_t)(offset - 1);
			return best;
		}
	}

	RHI::Ref<Mesh> Manager::GetMesh(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		PrimitiveType primitive;
		if (IsPrimitive(handle, primitive))
			return Mesh::GetPrimitive(*s_Device, primitive);

		const auto cached = s_Meshes.find(handle);
		if (cached != s_Meshes.end())
			return cached->second;

		// The model's own handle addresses its first primitive; anything else
		// is a handle *inside* a model and has to be traced back to it.
		AssetHandle model = handle;
		uint32_t wanted = 0;

		std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			model = OwningModel(handle, wanted);
			path = model.IsValid() ? Registry::GetAbsolutePath(model)
								   : std::filesystem::path();
		}

		if (path.empty())
		{
			s_Meshes[handle] = nullptr;
			return nullptr;
		}

		ImportedModel imported;
		if (!ModelImporter::Import(path, imported) || imported.Primitives.empty())
		{
			// Cached as null so a broken file is not re-parsed every frame.
			s_Meshes[handle] = nullptr;
			return nullptr;
		}

		if (wanted >= imported.Primitives.size())
		{
			// The nearest model below is not the owner after all.
			s_Meshes[handle] = nullptr;
			return nullptr;
		}

		// The skeleton and the clips hang off the *model's* handle, since that
		// is what an AnimatorComponent names.
		if (imported.HasSkeleton())
		{
			s_Skeletons[model] = imported.Skeleton;
			s_Clips[model] = imported.Clips;
		}

		// **Every primitive, in one parse.** A car is a hundred and fifty of
		// them, and building only the one that was asked for would re-read a
		// thirty-megabyte file once per piece.
		for (size_t i = 0; i < imported.Primitives.size(); i++)
		{
			const RHI::Ref<Mesh> built = BuildPrimitive(imported, i);

			s_Meshes[AssetHandle((uint64_t)model + 1 + i)] = built;
			if (i == 0)
				s_Meshes[model] = built;
		}

		const auto found = s_Meshes.find(handle);
		return found != s_Meshes.end() ? found->second : nullptr;
	}


	// --- a rig belongs to a model, and a model is several meshes ------------
	//
	// **The skeleton is stored once, under the model's own handle; every part
	// of that model has a *derived* handle.** `InstantiateModel` mints
	// `modelHandle + 1 + index` for each primitive, so not one of them equals
	// the handle the skeleton is filed under -- and these two lookups asked for
	// the part's handle directly and were answered "no rig" every single time.
	//
	// What that cost is worth spelling out, because it is not "animation did
	// not play". Scene's skinned draw takes a bind pose from this when there is
	// no animator, and when it comes back null there is no branch left: the
	// mesh is not drawn at all. A rigged model imported into a scene therefore
	// *disappeared*, silently, part by part -- and only its skinned parts, so a
	// model whose parts were mostly static looked like it had holes in it
	// rather than like something broken.
	//
	// GetMesh has resolved derived handles through OwningModel since scenes
	// started saving them; these two never learned.
	template <typename Map>
	static const typename Map::mapped_type* FindForModel(Map& map, AssetHandle handle)
	{
		// GetMesh first: the skeleton and the clips are cached by the same
		// parse, and asking before anything has loaded the model would report
		// that a perfectly good rig has none.
		Manager::GetMesh(handle);

		if (const auto found = map.find(handle); found != map.end())
			return &found->second;

		uint32_t primitive = 0;
		const AssetHandle owner = OwningModel(handle, primitive);
		if (!owner.IsValid())
			return nullptr;

		Manager::GetMesh(owner);

		const auto found = map.find(owner);
		return found != map.end() ? &found->second : nullptr;
	}

	const Skeleton* Manager::GetSkeleton(AssetHandle handle)
	{
		return FindForModel(s_Skeletons, handle);
	}

	const std::vector<Anim::Clip>* Manager::GetClips(AssetHandle handle)
	{
		return FindForModel(s_Clips, handle);
	}

	RHI::Ref<RHI::RHITexture> Manager::GetTexture(AssetHandle handle, ColorSpace space)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		// One cache per colour space. The same PNG decoded both ways is two
		// different textures, and a single cache would return whichever was
		// asked for first -- so a normal map would come back sRGB if some
		// sprite had loaded it earlier.
		auto& cache = space == ColorSpace::Srgb ? s_Textures : s_DataTextures;

		const auto cached = cache.find(handle);
		if (cached != cache.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			cache[handle] = nullptr;
			return nullptr;
		}

		auto texture = TextureLoader::Load2D(*s_Device, path.string(),
											 space == ColorSpace::Srgb);
		cache[handle] = texture;
		return texture;
	}

	RHI::Ref<Material> Manager::GetMaterial(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_Materials.find(handle);
		if (cached != s_Materials.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		MaterialDesc desc;
		if (path.empty() || !MaterialSerializer::Load(desc, path))
		{
			// Cached as null so a broken path is not reopened per frame, and
			// the caller falls back to the default material -- the same place
			// an unassigned handle lands.
			s_Materials[handle] = nullptr;
			return nullptr;
		}

		auto material = std::make_shared<Material>(*s_Device, desc.Name);
		material->GetParams() = desc.Params;
		material->SetBlendMode(desc.Blend);

		// The maps, and with them the flags. Assigning a null texture clears
		// the slot and its flag, so a handle that resolves to nothing lands on
		// the scalar parameter rather than on a missing binding.
		auto assign = [&](AssetHandle map, void (Material::*setter)(const RHI::Ref<RHI::RHITexture>&),
						  ColorSpace space)
		{
			if (map.IsValid())
				(material.get()->*setter)(GetTexture(map, space));
		};

		// Two of these are pictures and five are data. Reading a normal map
		// through sRGB pulls its X and Y toward the centre and flattens every
		// surface using it -- which looks like the normal maps not being
		// applied at all, and is how this was found.
		assign(desc.BaseColorMap, &Material::SetBaseColorMap, ColorSpace::Srgb);
		assign(desc.EmissiveMap, &Material::SetEmissiveMap, ColorSpace::Srgb);

		assign(desc.NormalMap, &Material::SetNormalMap, ColorSpace::Linear);
		assign(desc.OcclusionMap, &Material::SetOcclusionMap, ColorSpace::Linear);
		assign(desc.RoughnessMap, &Material::SetRoughnessMap, ColorSpace::Linear);
		assign(desc.MetallicMap, &Material::SetMetallicMap, ColorSpace::Linear);
		assign(desc.SpecularMap, &Material::SetSpecularMap, ColorSpace::Linear);
		assign(desc.HeightMap, &Material::SetHeightMap, ColorSpace::Linear);

		material->Invalidate();

		s_Materials[handle] = material;
		return material;
	}

	AssetHandle Manager::CreateMaterial(const MaterialDesc& material,
										const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!MaterialSerializer::Save(material, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreateCurve and CreatePrefab.
		Registry::Refresh();

		// Deliberately *not* seeded into the cache from `material` here, unlike
		// CreateCurve. A curve is the value it was created from; a Material is
		// a GPU object that has to be built from the desc, and building it here
		// would duplicate GetMaterial's resolve step -- which is where the
		// texture handles turn into textures and the map flags get set.
		return Registry::GetHandle(relativePath.generic_string());
	}

	void Manager::ReloadMaterial(AssetHandle handle)
	{
		s_Materials.erase(handle);
	}

	const Curve* Manager::GetCurve(AssetHandle handle)
	{
		// No device check, unlike the textures either side of this: a curve is
		// numbers on the CPU. That is what lets the suite exercise it, and what
		// will let a headless tool bake one.
		if (!handle.IsValid())
			return nullptr;

		const auto cached = s_Curves.find(handle);
		if (cached != s_Curves.end())
			return &cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		Curve curve;
		if (path.empty() || !CurveSerializer::Load(curve, path))
		{
			// Cached even on failure, and returned rather than null: an emitter
			// pointing at a missing curve should fall back to a value, not to a
			// branch every caller has to remember. An empty curve evaluates to
			// its stated fallback everywhere.
			s_Curves[handle] = Curve();
			return &s_Curves[handle];
		}

		s_Curves[handle] = std::move(curve);
		return &s_Curves[handle];
	}

	const Font* Manager::GetFont(AssetHandle handle)
	{
		// No device check: a metrics table is numbers, and keeping it that way
		// is what lets the suite test text layout with no GPU at all.
		if (!handle.IsValid())
			return nullptr;

		if (s_FontFailed.count(handle) != 0)
			return nullptr;

		const auto cached = s_Fonts.find(handle);
		if (cached != s_Fonts.end())
			return &cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		Font font;
		if (path.empty() || !FontSerializer::Load(font, path))
		{
			// Remembered, so a scene pointing at a font that is not there does
			// not reopen the file once per frame for the rest of the session.
			s_FontFailed.insert(handle);
			return nullptr;
		}

		s_Fonts[handle] = std::move(font);
		return &s_Fonts[handle];
	}

	const TerrainData* Manager::GetTerrain(AssetHandle handle)
	{
		// No device check, for the reason GetFont has none: heights are
		// numbers, and keeping them that way is what lets the suite build and
		// sample a terrain with no GPU.
		if (!handle.IsValid())
			return nullptr;

		if (s_TerrainFailed.count(handle) != 0)
			return nullptr;

		const auto cached = s_Terrains.find(handle);
		if (cached != s_Terrains.end())
			return &cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		TerrainData data;
		if (path.empty() || !TerrainSerializer::Load(data, path))
		{
			s_TerrainFailed.insert(handle);
			return nullptr;
		}

		s_Terrains[handle] = std::move(data);
		return &s_Terrains[handle];
	}

	TerrainData* Manager::EditTerrain(AssetHandle handle)
	{
		if (!GetTerrain(handle))
			return nullptr;
		s_TerrainDirty.insert(handle);
		return &s_Terrains[handle];
	}

	bool Manager::IsTerrainDirty(AssetHandle handle)
	{
		return s_TerrainDirty.count(handle) != 0;
	}

	bool Manager::HasDirtyTerrains()
	{
		return !s_TerrainDirty.empty();
	}

	bool Manager::SaveTerrain(AssetHandle handle)
	{
		const auto cached = s_Terrains.find(handle);
		if (cached == s_Terrains.end())
			return false;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty() || !TerrainSerializer::Save(cached->second, path))
		{
			RV_CORE_ERROR("Terrain {0} could not be saved to {1}", (uint64_t)handle, path.string());
			return false;
		}

		// The sidecar's hash follows the bytes, so the next scan finds nothing
		// to rewrite; one file's, not the project's.
		Registry::Reindex(handle);
		s_TerrainDirty.erase(handle);
		RV_CORE_INFO("Saved terrain {0}", path.filename().string());
		return true;
	}

	void Manager::SaveDirtyTerrains()
	{
		// Copied first: SaveTerrain erases from the set it would be walking.
		const std::vector<AssetHandle> dirty(s_TerrainDirty.begin(), s_TerrainDirty.end());
		for (AssetHandle handle : dirty)
			SaveTerrain(handle);
	}

	RHI::Ref<RHI::RHITexture> Manager::GetFontAtlas(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_FontAtlases.find(handle);
		if (cached != s_FontAtlases.end())
			return cached->second;

		const Font* font = GetFont(handle);
		if (!font)
			return nullptr;

		// The `.rvfont` names its atlas by filename, and it is resolved beside
		// the metrics rather than against the asset root. The two are one
		// output of one tool run, so a project that moves a font into a
		// subfolder moves both and nothing has to be edited.
		const std::filesystem::path metrics = Registry::GetAbsolutePath(handle);
		const std::filesystem::path atlas = metrics.parent_path() / font->AtlasFile;

		// Through the VFS, like every content read: an atlas inside a pak does
		// not exist on disk, and this check saying so cost the packaged HUD
		// its font -- the E2E's pixel diff is what caught it.
		if (!IO::VFS::Exists(atlas))
		{
			RV_CORE_ERROR("Font {0} names an atlas that is not there: {1}",
						  metrics.filename().string(), font->AtlasFile);
			s_FontAtlases[handle] = nullptr;
			return nullptr;
		}

		// Linear, and no mips. Both matter and both are silent when wrong:
		// treating a distance field as sRGB bends the distances so the edge
		// lands somewhere else, and a mip chain averages distances from
		// opposite sides of a stroke into a number that describes nothing.
		// Either one renders as text that is soft for no visible reason.
		RHI::Ref<RHI::RHITexture> texture =
			TextureLoader::Load2D(*s_Device, atlas.string(), /*srgb*/ false,
								  /*generateMips*/ false);

		s_FontAtlases[handle] = texture;
		return texture;
	}

	const Curve::Baked* Manager::GetBakedCurve(AssetHandle handle)
	{
		if (!handle.IsValid())
			return nullptr;

		const auto cached = s_BakedCurves.find(handle);
		if (cached != s_BakedCurves.end())
			return &cached->second;

		// Through GetCurve, so the "a valid handle always answers something"
		// contract is stated once. A missing file bakes an empty curve, which
		// is a table of zeroes -- the fallback, sampled rather than branched on.
		const Curve* curve = GetCurve(handle);
		if (!curve)
			return nullptr;

		s_BakedCurves[handle] = curve->BakeTable();
		return &s_BakedCurves[handle];
	}

	AssetHandle Manager::CreateCurve(const Curve& curve, const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!CurveSerializer::Save(curve, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreatePrefab.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
			s_Curves[handle] = curve;

		return handle;
	}

	void Manager::ReloadCurve(AssetHandle handle)
	{
		// The editor calls this when somebody has finished dragging a point.
		// Without it the cache above is exactly the stale-data bug the probe's
		// mip chain was: the file changes and the picture does not.
		s_Curves.erase(handle);
		s_BakedCurves.erase(handle);
	}

	const ScriptGraph* Manager::GetScriptGraph(AssetHandle handle)
	{
		// No device check, like the curve above and for the same reason: a
		// graph is data on the CPU, which is what lets a headless tool
		// generate from one.
		if (!handle.IsValid())
			return nullptr;

		if (s_ScriptGraphErrors.count(handle) != 0)
			return nullptr;

		const auto cached = s_ScriptGraphs.find(handle);
		if (cached != s_ScriptGraphs.end())
			return &cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		ScriptGraph graph;
		std::string error;
		if (path.empty() || !ScriptGraphSerializer::Load(graph, path, &error))
		{
			// The failure is remembered, not the graph: re-reading a file that
			// will not read, every frame the panel is open, would say so
			// several hundred times. The message is kept because the caller
			// has to show it -- and `path.empty()` has none of its own.
			s_ScriptGraphErrors[handle] = error.empty()
				? std::string("The graph's file could not be found.")
				: error;
			return nullptr;
		}

		s_ScriptGraphs[handle] = std::move(graph);
		return &s_ScriptGraphs[handle];
	}

	const std::string& Manager::GetScriptGraphError(AssetHandle handle)
	{
		static const std::string none;
		const auto found = s_ScriptGraphErrors.find(handle);
		return found == s_ScriptGraphErrors.end() ? none : found->second;
	}

	bool Manager::LoadScriptGraphWithoutUnknown(AssetHandle handle, ScriptGraph& out,
												std::string* outMessage)
	{
		if (!handle.IsValid())
			return false;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
			return false;

		return ScriptGraphSerializer::Load(out, path, outMessage,
										   GraphLoadMode::DropUnknown);
	}

	AssetHandle Manager::CreateScriptGraph(const ScriptGraph& graph,
										   const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!ScriptGraphSerializer::Save(graph, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreateCurve.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
		{
			s_ScriptGraphs[handle] = graph;
			s_ScriptGraphErrors.erase(handle);
		}

		return handle;
	}

	bool Manager::SaveScriptGraph(AssetHandle handle, const ScriptGraph& graph)
	{
		if (!handle.IsValid())
			return false;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty() || !ScriptGraphSerializer::Save(graph, path))
			return false;

		// The cache holds what the file holds, so a later Get answers the
		// saved graph rather than the one loaded before the edits -- and a
		// handle that would not load has just been written over by one that
		// will, so its refusal goes with it.
		s_ScriptGraphs[handle] = graph;
		s_ScriptGraphErrors.erase(handle);
		return true;
	}

	void Manager::ReloadScriptGraph(AssetHandle handle)
	{
		// The refusal too, or a graph fixed outside the editor stays refused
		// for the life of the process.
		s_ScriptGraphs.erase(handle);
		s_ScriptGraphErrors.erase(handle);
	}

	PostSettings Manager::GetPostSettings(AssetHandle handle)
	{
		// By value, and never a failure. A camera with no profile and a camera
		// pointing at a profile that will not load both want the same thing --
		// the neutral grade -- and returning it from one place means no caller
		// carries a null branch whose two arms have to be kept identical.
		//
		// No device check: this is numbers on the CPU, which is what lets the
		// suite exercise it headlessly.
		if (!handle.IsValid())
			return PostSettings{};

		const auto cached = s_PostProfiles.find(handle);
		if (cached != s_PostProfiles.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		PostSettings settings;
		if (!path.empty())
			PostProfileSerializer::Load(settings, path);

		// Cached even when the load failed, so a camera pointing at a missing
		// profile does not reopen the file sixty times a second.
		s_PostProfiles[handle] = settings;
		return settings;
	}

	PostSettings* Manager::GetPostProfile(AssetHandle handle)
	{
		if (!handle.IsValid())
			return nullptr;

		// Through GetPostSettings, so "cached, including the failure" is
		// stated once. The lookup that follows cannot miss: that call inserts.
		GetPostSettings(handle);
		return &s_PostProfiles[handle];
	}

	AssetHandle Manager::CreatePostProfile(const PostSettings& settings,
										   const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!PostProfileSerializer::Save(settings, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreateCurve and CreatePrefab.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
			s_PostProfiles[handle] = settings;

		return handle;
	}

	void Manager::ReloadPostProfile(AssetHandle handle)
	{
		s_PostProfiles.erase(handle);
	}

	RHI::Ref<RHI::RHITexture> Manager::GetColorLut(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_ColorLuts.find(handle);
		if (cached != s_ColorLuts.end())
			return cached->second;

		ColorLut lut;
		const std::filesystem::path path = Registry::GetAbsolutePath(handle);

		// One asset type, two files behind it: a `.cube` is a baked table and
		// a `.rvlut` is the recipe that bakes one. Dispatched here so that
		// nothing above this line -- the profile, the picker, the shader --
		// has to know which it got. ENGINE-NOTES 7v.
		bool loaded = false;
		if (!path.empty())
		{
			if (IsLutRecipePath(path))
			{
				LutRecipe recipe;
				if (LutRecipeSerializer::Load(recipe, path))
				{
					lut = BakeRecipe(recipe);
					loaded = true;
				}
			}
			else
			{
				loaded = LoadCubeLut(lut, path);
			}
		}

		if (!loaded || !lut.IsValid())
		{
			s_ColorLuts[handle] = nullptr;
			return nullptr;
		}

		RHI::TextureDesc desc;
		desc.Type = RHI::TextureType::Texture3D;
		desc.Width = lut.Size;
		desc.Height = lut.Size;
		desc.Depth = lut.Size;
		desc.MipLevels = 1;
		// 16-bit float rather than 8-bit unorm. A LUT is filtered and *then*
		// written to an 8-bit target, so 8-bit entries quantise before the
		// interpolation and band in exactly the smooth gradients a grade is
		// judged on. At 33 cubed the float version is 143 KB.
		desc.Format = RHI::Format::R16G16B16A16_SFLOAT;
		desc.Usage = RHI::TextureUsage::Sampled;
		desc.DebugName = "ColorLut";

		RHI::Ref<RHI::RHITexture> texture = s_Device->CreateTexture(desc);
		if (!texture)
		{
			s_ColorLuts[handle] = nullptr;
			return nullptr;
		}

		const std::vector<uint16_t> halves = ToHalfRGBA(lut);
		texture->UploadLayer(halves.data(), halves.size() * sizeof(uint16_t), 0);

		s_ColorLuts[handle] = texture;
		return texture;
	}

	void Manager::ReloadAllPostProfiles()
	{
		s_PostProfiles.clear();
	}

	LutRecipe* Manager::GetLutRecipe(AssetHandle handle)
	{
		if (!handle.IsValid())
			return nullptr;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty() || !IsLutRecipePath(path))
			return nullptr;   // a `.cube` has no recipe, and never will

		const auto cached = s_LutRecipes.find(handle);
		if (cached != s_LutRecipes.end())
			return &cached->second;

		// Cached even when the load failed, so a broken file is not reopened
		// every frame the inspector draws it. Defaults bake the identity, so
		// the failure shows as "this grades nothing" rather than as a crash.
		LutRecipe recipe;
		LutRecipeSerializer::Load(recipe, path);
		s_LutRecipes[handle] = recipe;
		return &s_LutRecipes[handle];
	}

	AssetHandle Manager::CreateLutRecipe(const LutRecipe& recipe,
										 const std::filesystem::path& relativePath)
	{
		if (!Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;
		if (!LutRecipeSerializer::Save(recipe, absolute))
			return AssetHandle::Invalid();

		// After writing, so the file exists by the time the registry hashes it
		// and mints its sidecar. Same order as CreatePostProfile.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
			s_LutRecipes[handle] = recipe;

		return handle;
	}

	void Manager::ReloadColorLut(AssetHandle handle)
	{
		// Both caches: the recipe is what the inspector edits and the texture
		// is what the shader samples, and a knob moved without dropping the
		// second is a grade that changed everywhere except on screen.
		s_LutRecipes.erase(handle);
		s_ColorLuts.erase(handle);
	}

	RHI::Ref<RHI::RHITexture> Manager::GetCubemap(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		const auto cached = s_Cubemaps.find(handle);
		if (cached != s_Cubemaps.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			s_Cubemaps[handle] = nullptr;
			return nullptr;
		}

		auto cube = TextureLoader::LoadCube(*s_Device, path.string());
		s_Cubemaps[handle] = cube;
		return cube;
	}

	RHI::Ref<RHI::RHITexture> Manager::GetIrradiance(AssetHandle handle)
	{
		if (!s_Device || !handle.IsValid())
			return nullptr;

		// Cached by handle like the cube is. Without this it resolved a handle
		// to an absolute path and built a std::string every frame a scene was
		// drawn, to reach a cache that was already holding the answer.
		const auto cached = s_Irradiance.find(handle);
		if (cached != s_Irradiance.end())
			return cached->second;

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			s_Irradiance[handle] = nullptr;
			return nullptr;
		}

		auto irradiance = TextureLoader::LoadIrradiance(*s_Device, path.string());
		s_Irradiance[handle] = irradiance;
		return irradiance;
	}

	AssetHandle Manager::CreatePrefab(Scene& scene, Entity root,
										   const std::filesystem::path& relativePath)
	{
		if (!root || !Registry::IsInitialised())
			return AssetHandle::Invalid();

		const std::filesystem::path absolute = Registry::Root() / relativePath;

		std::error_code error;
		std::filesystem::create_directories(absolute.parent_path(), error);

		// The same subtree snapshot undo uses. A prefab is not a new format --
		// it is a scene file holding one tree, which is why instantiating one
		// and restoring a deleted one share a reader.
		auto shared = std::shared_ptr<Scene>(&scene, [](Scene*) {});
		SceneSerializer serializer(shared);
		const std::string yaml = serializer.SerializeSubtree(root);

		std::ofstream file(absolute);
		if (!file)
		{
			RV_CORE_ERROR("Could not write prefab '{0}'", absolute.string());
			return AssetHandle::Invalid();
		}
		file << yaml;
		file.close();

		// Picks up the new file and mints its sidecar.
		Registry::Refresh();

		const AssetHandle handle = Registry::GetHandle(relativePath.generic_string());
		if (handle.IsValid())
		{
			// The source tree becomes an instance of the prefab it just made,
			// which is what makes "create prefab" feel like extracting rather
			// than copying.
			if (!root.HasComponent<PrefabComponent>())
				root.AddComponent<PrefabComponent>(handle);
			else
				root.GetComponent<PrefabComponent>().Source = handle;
		}

		return handle;
	}

	Entity Manager::InstantiatePrefab(Scene& scene, AssetHandle handle)
	{
		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
			return {};

		std::ifstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not read prefab '{0}'", path.string());
			return {};
		}

		std::stringstream buffer;
		buffer << file.rdbuf();

		auto shared = std::shared_ptr<Scene>(&scene, [](Scene*) {});
		SceneSerializer serializer(shared);

		// Fresh ids: two instances that shared identities would mean every
		// reference to one resolved to both.
		Entity root = serializer.Instantiate(buffer.str());
		if (!root)
			return {};

		if (!root.HasComponent<PrefabComponent>())
			root.AddComponent<PrefabComponent>(handle);
		else
			root.GetComponent<PrefabComponent>().Source = handle;

		scene.UpdateWorldTransforms();
		return root;
	}

	Entity Manager::InstantiateModel(Scene& scene, AssetHandle handle)
	{
		if (!s_Device)
			return {};

		const std::filesystem::path path = Registry::GetAbsolutePath(handle);
		if (path.empty())
		{
			RV_CORE_ERROR("No source file for asset {0}", (uint64_t)handle);
			return {};
		}

		ImportedModel model;
		if (!ModelImporter::Import(path, model))
			return {};

		// Textures resolve relative to the model, which is where glTF's URIs
		// point.
		const std::filesystem::path directory = path.parent_path();

		// The model's textures, as *handles* rather than loaded images.
		//
		// A glTF names its textures by relative URI; the registry addresses
		// everything by handle. Importing one therefore means registering the
		// file it points at, which is what makes the material below storable:
		// a `.rmat` can hold a handle and cannot hold a loaded texture.
		//
		// A texture already in the project keeps the handle it has -- the
		// registry is keyed on path -- so re-importing a model does not mint a
		// second handle for a file two models share.
		Registry::Refresh();

		std::vector<AssetHandle> textures(model.Textures.size());
		for (size_t i = 0; i < model.Textures.size(); i++)
		{
			const std::filesystem::path texturePath = directory / model.Textures[i].Path;

			std::error_code error;
			const std::filesystem::path relative =
				std::filesystem::relative(texturePath, Registry::Root(), error);

			if (error || relative.empty())
			{
				RV_CORE_WARN("Model texture '{0}' is outside the project and cannot be "
							 "addressed by handle", texturePath.string());
				continue;
			}

			textures[i] = Registry::GetHandle(relative.generic_string());
			if (!textures[i].IsValid())
			{
				RV_CORE_WARN("Model texture '{0}' has no asset handle; its material will "
							 "fall back to the scalar parameter", relative.generic_string());
			}
		}

		// glTF's packed metallic-roughness texture, split into the two greyscale
		// maps the engine actually samples.
		//
		// The shader takes roughness and metallic separately, because that is
		// how every texture library ships them. glTF is the exception: it packs
		// roughness into green and metallic into blue of one image. Rather than
		// carry a second code path through the material, the descriptor set and
		// the shader for the sake of one file format, the import splits it once.
		//
		// Written to disk rather than split in memory, and that is not
		// incidental: a material stores *handles*, and a texture that exists
		// only in memory has none -- so an in-memory split would render
		// correctly and vanish on save, which is the exact bug this whole task
		// was about.
		auto splitMetallicRoughness = [&](int textureIndex,
										  AssetHandle& roughnessOut, AssetHandle& metallicOut)
		{
			if (textureIndex < 0 || textureIndex >= (int)model.Textures.size())
				return;

			const std::filesystem::path packed = directory / model.Textures[textureIndex].Path;

			int width = 0, height = 0, channels = 0;
			stbi_uc* pixels = stbi_load(packed.string().c_str(), &width, &height, &channels, 4);
			if (!pixels)
			{
				RV_CORE_WARN("Could not read '{0}' to split its metallic-roughness channels",
							 packed.string());
				return;
			}

			std::vector<stbi_uc> roughness((size_t)width * height);
			std::vector<stbi_uc> metallic((size_t)width * height);
			for (size_t i = 0; i < roughness.size(); i++)
			{
				roughness[i] = pixels[i * 4 + 1];   // green
				metallic[i]  = pixels[i * 4 + 2];   // blue
			}
			stbi_image_free(pixels);

			const std::filesystem::path stem = packed.parent_path() / packed.stem();

			auto write = [&](const char* suffix, const std::vector<stbi_uc>& channel,
							 AssetHandle& out)
			{
				const std::filesystem::path path = stem.string() + suffix + ".png";
				if (stbi_write_png(path.string().c_str(), width, height, 1,
								   channel.data(), width) == 0)
				{
					RV_CORE_WARN("Could not write '{0}'", path.string());
					return;
				}

				std::error_code error;
				const std::filesystem::path relative =
					std::filesystem::relative(path, Registry::Root(), error);
				if (error || relative.empty())
					return;

				Registry::Refresh();
				out = Registry::GetHandle(relative.generic_string());
			};

			write("_roughness", roughness, roughnessOut);
			write("_metallic", metallic, metallicOut);
		};

		// Each imported material becomes a real `.rmat` beside the model.
		//
		// This is the part that makes an import survive being saved. The maps
		// used to be set on a Material object hanging off the component, which
		// no scene file could write -- so a textured model looked correct until
		// the first save and was untextured from then on. Now the maps live in
		// an asset and the component stores its handle.
		std::vector<AssetHandle> materials(model.Materials.size());
		for (size_t i = 0; i < model.Materials.size(); i++)
		{
			const ImportedMaterial& source = model.Materials[i];

			MaterialDesc desc;
			desc.Name = source.Name.empty() ? "Material" : source.Name;
			desc.Params = source.Params;
			desc.Blend = source.Blend;

			auto assign = [&](int index, AssetHandle& slot)
			{
				if (index >= 0 && index < (int)textures.size())
					slot = textures[index];
			};

			assign(source.BaseColorTexture, desc.BaseColorMap);
			assign(source.NormalTexture, desc.NormalMap);
			assign(source.OcclusionTexture, desc.OcclusionMap);
			assign(source.EmissiveTexture, desc.EmissiveMap);

			// Not assigned -- split. See above.
			splitMetallicRoughness(source.MetallicRoughnessTexture,
								   desc.RoughnessMap, desc.MetallicMap);

			// Named after the model and the material's index, not just its
			// name: a glTF may carry two materials called "Material", and two
			// models certainly may.
			std::error_code error;
			const std::filesystem::path modelRelative =
				std::filesystem::relative(path, Registry::Root(), error);
			const std::string stem = error ? path.stem().string()
										   : modelRelative.stem().string();

			const std::filesystem::path materialPath =
				modelRelative.parent_path() /
				(stem + "_" + std::to_string(i) + "_" + desc.Name + ".rmat");

			materials[i] = CreateMaterial(desc, materialPath);
		}

		// Every primitive gets a handle derived from the file's, so a mesh
		// inside a model is addressable and the cache can hold it. GetMesh
		// mints the same numbers from the other direction, for a scene that
		// stored them and is being opened without this having run.
		std::vector<RHI::Ref<Mesh>> meshes(model.Primitives.size());
		for (size_t i = 0; i < model.Primitives.size(); i++)
		{
			meshes[i] = BuildPrimitive(model, i);
			s_Meshes[AssetHandle((uint64_t)handle + 1 + i)] = meshes[i];
		}

		// The skeleton and its clips under the model's own handle, which is
		// what an AnimatorComponent names. GetMesh does the same on its own
		// path; without it here, importing a rig and *then* asking for its
		// clips answers that a perfectly good one has none.
		if (model.HasSkeleton())
		{
			s_Skeletons[handle] = model.Skeleton;
			s_Clips[handle] = model.Clips;
		}

		// **What the thing actually measures, in the space it lands in.**
		//
		// Every scene that places an imported model needs two numbers -- how
		// tall it is, and where its origin sits inside it -- and neither was
		// reported anywhere. Both were being recovered by putting the model in
		// a room, rendering it and counting pixels, which is how the Mark 85's
		// pivot turned out to be level with the top of its helmet with the
		// whole body hanging below it. Nothing in a file listing says that.
		//
		// Composed through the node hierarchy, so it is the space the entities
		// are built in -- which is what a generator's scale and lift are
		// expressed in.
		{
			std::vector<Mat4> world(model.Nodes.size(), Mat4(1.0f));
			Vec3 low(FLT_MAX), high(-FLT_MAX);
			bool any = false;

			for (size_t i = 0; i < model.Nodes.size(); i++)
			{
				const ImportedNode& node = model.Nodes[i];

				const Mat4 local = Math::Translate(Mat4(1.0f), node.Position)
								 * Math::ToMat4(Math::FromEuler(node.Rotation))
								 * Math::Scale(Mat4(1.0f), node.Scale);

				// Parents always precede their children, so one forward pass
				// composes the tree -- the invariant ImportedModel promises.
				world[i] = node.Parent >= 0 ? world[node.Parent] * local : local;

				for (int index : node.Primitives)
				{
					if (index < 0 || index >= (int)model.Primitives.size())
						continue;

					for (const MeshVertex& vertex : model.Primitives[index].Vertices)
					{
						const Vec3 at = Vec3(world[i] * Vec4(vertex.Position, 1.0f));
						low = Math::Min(low, at);
						high = Math::Max(high, at);
						any = true;
					}
				}
			}

			if (any)
			{
				RV_CORE_INFO("  '{0}' measures {1:.4f} x {2:.4f} x {3:.4f}, "
							 "its origin {4:.4f} above its own floor",
							 model.Name, high.x - low.x, high.y - low.y, high.z - low.z,
							 -low.y);

				for (size_t i = 0; i < model.Primitives.size(); i++)
				{
					const ImportedPrimitive& piece = model.Primitives[i];
					RV_CORE_INFO("    part {0} '{1}': {2} vertices, {3} indices, "
								 "{4} joints, {5} weights",
								 i, piece.Name, piece.Vertices.size(), piece.Indices.size(),
								 piece.Joints.size(), piece.Weights.size());
				}
			}
		}

		// One root, so the import is a single thing to move, delete or undo.
		Entity root = scene.CreateEntity(model.Name);

		std::vector<Entity> entities(model.Nodes.size());
		for (size_t i = 0; i < model.Nodes.size(); i++)
		{
			const ImportedNode& node = model.Nodes[i];

			Entity entity = scene.CreateEntity(node.Name);
			auto& transform = entity.GetComponent<TransformComponent>();
			transform.Position = node.Position;
			transform.Rotation = node.Rotation;
			transform.Scale = node.Scale;

			entities[i] = entity;

			// A node may carry several primitives with different materials.
			// The first rides on the node itself; the rest become children, so
			// each keeps one mesh and one material.
			for (size_t p = 0; p < node.Primitives.size(); p++)
			{
				const int index = node.Primitives[p];
				if (index < 0 || index >= (int)meshes.size())
					continue;

				Entity target = entity;
				if (p > 0)
				{
					target = scene.CreateEntity(model.Primitives[index].Name);
					scene.SetParent(target, entity);
				}

				auto& mesh = target.AddComponent<MeshComponent>();
				mesh.Mesh = AssetHandle((uint64_t)handle + 1 + index);

				const int materialIndex = model.Primitives[index].Material;
				if (materialIndex >= 0 && materialIndex < (int)materials.size())
					mesh.Material = materials[materialIndex];
			}
		}

		// Parented after every entity exists, since a node's parent index
		// always precedes it but SetParent needs both to be real.
		//
		// **Without keeping the world transform, and that is the whole of a
		// bug that flattened every imported hierarchy.** The transforms
		// written above are the model's own, already expressed relative to
		// each node's parent -- which is exactly what a local transform is.
		// SetParent's default is to preserve the child's *world* placement,
		// because that is what dragging a row in the hierarchy panel should
		// do; applied here it re-derived every local transform as the inverse
		// of its parent's, so the composed result was the identity for every
		// node in the file.
		//
		// It went unnoticed for as long as it did because a flattened
		// hierarchy is only visible on a model whose node transforms do
		// something: the sample car's chain happens to compose to nothing, so
		// cancelling it changed no pixel. The first model that carried a real
		// axis conversion on its root arrived lying on its back.
		for (size_t i = 0; i < model.Nodes.size(); i++)
		{
			const int parent = model.Nodes[i].Parent;
			scene.SetParent(entities[i], parent >= 0 ? entities[parent] : root,
							/*keepWorldTransform*/ false);
		}

		scene.UpdateWorldTransforms();
		return root;
	}
}
