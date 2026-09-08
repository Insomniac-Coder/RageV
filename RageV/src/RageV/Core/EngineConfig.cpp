#include <rvpch.h>
#include "EngineConfig.h"
#include "RageV/Renderer/RenderSettings.h"

#include <fstream>
#include <algorithm>
#include <cstdlib>

namespace RageV
{
	namespace
	{
		EngineConfig s_Config;
		bool s_Initialized = false;

		std::string Trim(std::string value)
		{
			const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
			value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
			return value;
		}

		std::string ToLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
			return value;
		}

		bool ParseBool(const std::string& value, bool& out)
		{
			const std::string lowered = ToLower(value);
			if (lowered == "1" || lowered == "on" || lowered == "true" || lowered == "yes")  { out = true;  return true; }
			if (lowered == "0" || lowered == "off" || lowered == "false" || lowered == "no") { out = false; return true; }
			return false;
		}
	}

	const char* EngineConfig::BackendName(RHI::Backend backend)
	{
		switch (backend)
		{
			case RHI::Backend::Vulkan: return "Vulkan";
			case RHI::Backend::OpenGL: return "OpenGL";
		}
		return "unknown";
	}

	bool EngineConfig::ApplyKeyValue(EngineConfig& config, std::string key, std::string value)
	{
		key = ToLower(Trim(key));
		value = Trim(value);

		if (key == "rhi" || key == "backend" || key == "api")
		{
			const std::string lowered = ToLower(value);
			if (lowered == "vulkan" || lowered == "vk")
			{
				config.Backend = RHI::Backend::Vulkan;
				return true;
			}
			if (lowered == "opengl" || lowered == "gl")
			{
				config.Backend = RHI::Backend::OpenGL;
				return true;
			}
			RV_CORE_WARN("Unknown graphics backend '{0}'; expected 'vulkan' or 'opengl'", value);
			return false;
		}

		// The packaged fallback list, "vulkan, opengl". Order is meaning.
		if (key == "backends")
		{
			config.Backends.clear();

			std::stringstream stream(value);
			std::string entry;
			while (std::getline(stream, entry, ','))
			{
				const std::string lowered = ToLower(Trim(entry));
				if (lowered.empty())
					continue;

				if (lowered == "vulkan" || lowered == "vk")
					config.Backends.push_back(RHI::Backend::Vulkan);
				else if (lowered == "opengl" || lowered == "gl")
					config.Backends.push_back(RHI::Backend::OpenGL);
				else
					RV_CORE_WARN("Unknown graphics backend '{0}' in backends", entry);
			}
			return true;
		}

		if (key == "vsync")
			return ParseBool(value, config.VSync);

		if (key == "reveal-after-build" || key == "revealafterbuild")
			return ParseBool(value, config.RevealAfterBuild);

		if (key == "validation")
		{
			// "gpu" is the layers plus GPU-assisted validation: the only thing
			// that can catch an out-of-range index into a bindless array,
			// which ordinary validation cannot see (ENGINE-NOTES 7al).
			if (value == "gpu")
			{
				config.EnableValidation = true;
				config.ValidationGpuAssisted = true;
				return true;
			}
			config.ValidationGpuAssisted = false;
			return ParseBool(value, config.EnableValidation);
		}

		if (key == "audio")
			return ParseBool(value, config.EnableAudio);

		if (key == "bake")
		{
			// "force" is "on" plus "ignore what is already on disk". Spelled as
			// a third value of the same flag rather than as a second flag,
			// because `--bake=force --bake=off` should not be a thing anybody
			// can write.
			if (ToLower(value) == "force")
			{
				config.BakeLighting = true;
				config.ForceLightingBake = true;
				return true;
			}
			config.ForceLightingBake = false;
			return ParseBool(value, config.BakeLighting);
		}

		if (key == "import-cache" || key == "importcache")
			return ParseBool(value, config.UseImportCache);

		if (key == "depth-sort" || key == "depthsort")
			return ParseBool(value, config.DepthSortOpaque);

		if (key == "pass-timings" || key == "passtimings")
		{
			if (!ParseBool(value, config.PassTimings))
				return false;
			config.HasPassTimingsOverride = true;
			return true;
		}

		if (key == "gpu-cull" || key == "gpucull")
			return ParseBool(value, config.GpuCull);

		if (key == "gpu-lit" || key == "gpulit")
			return ParseBool(value, config.GpuLit);

		if (key == "meshlets" || key == "mesh-shading" || key == "meshshading")
			return ParseBool(value, config.Meshlets);

		if (key == "bindless")
			return ParseBool(value, config.Bindless);

		if (key == "raytracing" || key == "rt")
		{
			config.HasRayTracingOverride = true;
			return ParseBool(value, config.RayTracingOverride);
		}

		if (key == "rt-reflections" || key == "rtreflections")
		{
			config.HasRayReflectionsOverride = true;
			return ParseBool(value, config.RayReflectionsOverride);
		}

		if (key == "reflection-pass" || key == "reflectionpass")
		{
			config.HasReflectionPassOverride = true;
			return ParseBool(value, config.ReflectionPassOverride);
		}

		if (key == "reflection-history" || key == "reflectionhistory")
		{
			config.HasReflectionHistoryOverride = true;
			return ParseBool(value, config.ReflectionHistoryOverride);
		}

		if (key == "rt-ao" || key == "rtao")
		{
			config.HasRayAoOverride = true;
			return ParseBool(value, config.RayAoOverride);
		}

		if (key == "rt-gi" || key == "rtgi" || key == "raygi")
		{
			config.HasRayGiOverride = true;
			return ParseBool(value, config.RayGiOverride);
		}

		if (key == "slow-frames" || key == "slowframes")
		{
			try
			{
				config.SlowFrameMs = Math::Max(std::stof(value), 0.0f);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("slow-frames expects milliseconds, got '{0}'", value);
				return false;
			}
		}

		if (key == "gi-source" || key == "gisource")
		{
			const std::string wanted = ToLower(value);
			if (wanted != "baked" && wanted != "realtime")
			{
				RV_CORE_WARN("gi-source expects 'baked' or 'realtime', got '{0}'",
					value);
				return false;
			}
			config.GiSourceBaked = wanted == "baked";
			config.HasGiSourceOverride = true;
			return true;
		}

		if (key == "voxel-gi" || key == "voxelgi")
		{
			config.HasVoxelGiOverride = true;
			return ParseBool(value, config.VoxelGiOverride);
		}

		if (key == "render-defaults" || key == "renderdefaults")
		{
			config.HasRenderDefaults = true;
			return ParseBool(value, config.RenderDefaults);
		}

		if (key == "gi-bounces" || key == "gibounces")
		{
			config.GiBouncesOverride = std::atoi(value.c_str());
			// Rejected rather than clamped: `--gi-bounces=4` is a person
			// asking for something this does not do, and silently giving them
			// two would have them measure two and write down four.
			return config.GiBouncesOverride == 1 || config.GiBouncesOverride == 2;
		}

		if (key == "msaa" || key == "samples")
		{
			// Refused here rather than snapped later. A sample count is a bit
			// flag, so an illegal one is not "a bit off" -- it is a value the
			// enum does not have, and the frame graph would have to pick
			// something else on the caller's behalf. Somebody who typed
			// --msaa=5 wants to hear that five is not a thing, not to measure
			// four while believing they measured five.
			const int requested = std::atoi(value.c_str());
			for (int count : MsaaCounts)
			{
				if (count == requested)
				{
					config.MsaaOverride = requested;
					return true;
				}
			}

			RV_CORE_ERROR("--msaa expects 2, 4 or 8; got '{0}'. A sample count is a "
						  "bit flag in both graphics APIs, so those are the only "
						  "values that exist.", value);
			return false;
		}

		if (key == "ssaa" || key == "supersample")
		{
			config.SupersampleOverride = std::atoi(value.c_str());
			return config.SupersampleOverride > 0;
		}

		if (key == "aa" || key == "anti-aliasing" || key == "antialiasing")
		{
			const std::string lowered = ToLower(value);
			if (lowered == "none" || lowered == "off")
				config.AAOverride = AntiAliasing::None;
			else if (lowered == "fxaa")
				config.AAOverride = AntiAliasing::FXAA;
			else if (lowered == "smaa")
				config.AAOverride = AntiAliasing::SMAA;
			else if (lowered == "ssaa")
				config.AAOverride = AntiAliasing::SSAA;
			else if (lowered == "msaa")
				config.AAOverride = AntiAliasing::MSAA;
			else if (lowered == "taa")
				config.AAOverride = AntiAliasing::TAA;
			else
			{
				RV_CORE_WARN("Unknown anti-aliasing mode '{0}'; expected 'none', 'fxaa', "
							 "'smaa', 'ssaa', 'msaa' or 'taa'",
							 value);
				return false;
			}
			config.HasAAOverride = true;
			return true;
		}

		if (key == "project")
		{
			config.ProjectPath = value;
			return true;
		}

		if (key == "play")
			return ParseBool(value, config.StartPlaying);

		if (key == "graph-barrier" || key == "graphbarrier")
			return ParseBool(value, config.DebugGraphBarrier);

		if (key == "screenshot")
		{
			config.ScreenshotPath = value;
			return true;
		}

		if (key == "loading-screenshot" || key == "loadingscreenshot")
		{
			config.LoadingScreenshotPath = value;
			return true;
		}

		if (key == "ui-scale" || key == "uiscale")
		{
			if (ToLower(value) == "auto")
			{
				config.UIScale = 0.0f;   // resolved against the monitor later
				return true;
			}

			try
			{
				config.UIScale = Math::Clamp(std::stof(value), 0.5f, 4.0f);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("ui-scale expects a number or 'auto', got '{0}'", value);
				return false;
			}
		}

		if (key == "generate-graphs" || key == "generategraphs")
			return ParseBool(value, config.GenerateGraphs);

		if (key == "graph-drop-unknown" || key == "graphdropunknown")
			return ParseBool(value, config.GraphDropUnknown);

		if (key == "graph-zoom" || key == "graphzoom")
		{
			try
			{
				config.GraphZoom = std::stof(value);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("graph-zoom expects a number, got '{0}'", value);
				return false;
			}
		}

		if (key == "graph")
		{
			config.GraphPath = value;
			return true;
		}

		if (key == "scene")
		{
			config.ScenePath = value;
			return true;
		}

		if (key == "select")
		{
			config.SelectEntity = value;
			return true;
		}

		if (key == "brush")
		{
			config.BrushScript = value;
			return true;
		}

		if (key == "hit-lights" || key == "hitlights")
			return ParseBool(value, config.HitLights);

		if (key == "casting-lights" || key == "castinglights")
		{
			try
			{
				config.CastingLights = Math::Max(std::stoi(value), 0);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("casting-lights expects a whole number; got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "water-lamp-pass" || key == "waterlamppass")
			return ParseBool(value, config.WaterLampPass);

		if (key == "direct-signal" || key == "directsignal")
			return ParseBool(value, config.DirectSignal);

		if (key == "ao-signal" || key == "aosignal")
			return ParseBool(value, config.AoSignal);

		// RT-3: the reference arm for the traced bounce as a signal.
		if (key == "gi-signal" || key == "gisignal")
			return ParseBool(value, config.GiSignal);

		// RT-6: the reference arm for the resolve's geometric test.
		if (key == "taa-geometry" || key == "taageometry")
			return ParseBool(value, config.TaaGeometry);

		if (key == "taa-box-geometry" || key == "taaboxgeometry")
			return ParseBool(value, config.TaaBoxGeometry);

		// The reflection signal's young blur, in texels, for a sweep.
		if (key == "reflection-blur" || key == "reflectionblur")
		{
			try { config.ReflectionBlurRadius = std::stof(value); }
			catch (...) { RV_CORE_WARN("reflection-blur expects a number of texels; got '{0}'", value); return false; }
			return true;
		}

		if (key == "terrain-lod-error" || key == "terrainloderror")
		{
			try
			{
				const float ratio = std::stof(value);
				config.TerrainLevelError = ratio > 0.0f ? ratio : 0.0f;
			}
			catch (...)
			{
				RV_CORE_WARN("terrain-lod-error expects a ratio, got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "water-lamp-reuse" || key == "waterlampreuse")
			return ParseBool(value, config.WaterLampReuse);

		if (key == "water-ablate" || key == "waterablate")
		{
			config.WaterAblate = 0;
			const std::string lowered = ToLower(value);
			size_t at = 0;
			while (at <= lowered.size())
			{
				const size_t comma = lowered.find(',', at);
				const std::string name = lowered.substr(
					at, comma == std::string::npos ? std::string::npos : comma - at);
				if (name == "reflection" || name == "mirror")
					config.WaterAblate |= 1;
				else if (name == "refraction" || name == "seethrough")
					config.WaterAblate |= 2;
				else if (name == "lamps")
					config.WaterAblate |= 4;
				else if (name == "sun")
					config.WaterAblate |= 8;
				else if (name == "screenlamps")
					config.WaterAblate |= 16;
				else if (name == "materials")
					config.WaterAblate |= 32;
				else if (!name.empty() && name != "none" && name != "off")
				{
					RV_CORE_WARN("water-ablate expects reflection, refraction, lamps, sun or screenlamps, "
								 "comma separated; got '{0}'", name);
					return false;
				}
				if (comma == std::string::npos)
					break;
				at = comma + 1;
			}
			return true;
		}

		// --water-reflection=full|half|quarter: at what size the sea's mirror
		// ray is traced. Named for what it is: only the reflection moves, and
		// the see-through is still traced inside the water draw.
		if (key == "water-reflection" || key == "waterreflection")
		{
			const std::string lowered = ToLower(value);
			if (lowered == "full" || lowered == "1")
				config.WaterReflectionScale = 0;
			else if (lowered == "half" || lowered == "2")
				config.WaterReflectionScale = 2;
			else if (lowered == "quarter" || lowered == "4")
				config.WaterReflectionScale = 4;
			else
			{
				RV_CORE_WARN("water-reflection expects full, half or quarter; got '{0}'",
							 value);
				return false;
			}
			return true;
		}

		if (key == "world-grid" || key == "worldgrid")
			return ParseBool(value, config.WorldLightGrid);

		if (key == "water-lamp-clamp" || key == "waterlampclamp")
		{
			try
			{
				config.WaterLampClamp = Math::Clamp(std::stof(value), 0.0f, 64.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("water-lamp-clamp expects a number from 0 to 64; got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "water-lamp-accumulate" || key == "waterlampaccumulate")
			return ParseBool(value, config.WaterLampAccumulate);
		// RT-8 job 3: the sea's averaging on the contract, or its own.
		if (key == "water-contract" || key == "watercontract")
			return ParseBool(value, config.WaterContract);
		if (key == "water-ray-contract" || key == "waterraycontract")
			return ParseBool(value, config.WaterRayContract);
		if (key == "water-direct" || key == "waterdirect")
			return ParseBool(value, config.WaterDirect);
		if (key == "water-lamp-slack" || key == "waterlampslack")
		{
			try
			{
				config.WaterLampSlack = Math::Clamp(std::stof(value), 0.25f, 64.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("water-lamp-slack expects a number from 0.25 to 64; got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "water-lamp-memory" || key == "waterlampmemory")
		{
			std::string scatter = value;
			const size_t comma = value.find(',');
			if (comma != std::string::npos)
			{
				scatter = value.substr(0, comma);
				try
				{
					config.WaterLampMemoryGlint =
						Math::Clamp(std::stoi(value.substr(comma + 1)), 1, 64);
				}
				catch (const std::exception&)
				{
					RV_CORE_WARN("water-lamp-memory's glint is a whole number from 1 to 64; "
								 "got '{0}'", value);
					return false;
				}
			}
			try
			{
				config.WaterLampMemoryScatter = Math::Clamp(std::stoi(scatter), 1, 64);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("water-lamp-memory expects scatter[,glint], each from 1 to 64; "
							 "got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "water-lamp-history" || key == "waterlamphistory")
			return ParseBool(value, config.WaterLampHistory);

		if (key == "water-lamp-neighbours" || key == "waterlampneighbours"
			|| key == "water-lamp-neighbors")
			return ParseBool(value, config.WaterLampNeighbours);

		if (key == "water-lamp-tuning" || key == "waterlamptuning")
		{
			std::string taps = value;
			const size_t comma = value.find(',');
			if (comma != std::string::npos)
			{
				taps = value.substr(0, comma);
				try
				{
					config.WaterLampCap = Math::Clamp(std::stoi(value.substr(comma + 1)), 1, 64);
				}
				catch (const std::exception&)
				{
					RV_CORE_WARN("water-lamp-tuning's cap is a whole number from 1 to 64; "
								 "got '{0}'", value);
					return false;
				}
			}
			try
			{
				config.WaterLampTaps = Math::Clamp(std::stoi(taps), 0, 8);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("water-lamp-tuning expects taps[,cap], taps from 0 to 8; "
							 "got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "lamp-probe" || key == "lampprobe")
		{
			const size_t comma = value.find(',');
			if (comma == std::string::npos)
			{
				RV_CORE_WARN("lamp-probe expects x,y in pixels; got '{0}'", value);
				return false;
			}
			try
			{
				config.LampProbeX = std::stoi(value.substr(0, comma));
				config.LampProbeY = std::stoi(value.substr(comma + 1));
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("lamp-probe expects x,y in pixels; got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "rays-per-pixel" || key == "raysperpixel"
			|| key == "light-sampling" || key == "lightsampling")   // the old name, still read (RT-1)
		{
			std::string count = value;
			const size_t comma = value.find(',');
			if (comma != std::string::npos)
			{
				const std::string target = ToLower(value.substr(comma + 1));
				count = value.substr(0, comma);
				if (target == "exact")
					config.RaysPerPixelTarget = 2;
				else if (target == "term" || target == "peak")
					config.RaysPerPixelTarget = 1;
				else if (target == "irradiance" || target == "cheap")
					config.RaysPerPixelTarget = 0;
				else
				{
					RV_CORE_WARN("rays-per-pixel's target is 'term' or 'irradiance'; got '{0}'",
								 target);
					return false;
				}
			}
			try
			{
				config.RaysPerPixel = Math::Clamp(std::stoi(count), 0, 8);
				config.HasRaysPerPixelOverride = true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("rays-per-pixel expects a whole number from 0 to 8; got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "debug-view-mix" || key == "debugviewmix")
		{
			try
			{
				config.DebugViewMix = Math::Clamp(std::stof(value), 0.0f, 1.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("debug-view-mix expects a number in [0, 1]; got '{0}'", value);
				return false;
			}
			return true;
		}

		// **WR-16 S3's two stillness levers**, for one run. Both zero is the
		// undamped allocator: every tile's count re-derived from scratch each
		// frame, which is what tools/scripts/tile_transitions.py grades
		// against. The dwell's ceiling is the shader's, not a taste: both
		// lanes' counters are packed into one half-float lane and 31 is where
		// the pair stays exactly representable.
		if (key == "tile-dead-band" || key == "tiledeadband")
		{
			try
			{
				config.TileDeadBandOverride = Math::Clamp(std::stof(value), 0.0f, 8.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("tile-dead-band expects a number of rays in [0, 8]; got '{0}'",
							 value);
				return false;
			}
			config.HasTileDeadBandOverride = true;
			return true;
		}

		if (key == "tile-dwell" || key == "tiledwell")
		{
			try
			{
				config.TileDwellOverride = Math::Clamp(std::stof(value), 0.0f, 31.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("tile-dwell expects a number of frames in [0, 31]; got '{0}'",
							 value);
				return false;
			}
			config.HasTileDwellOverride = true;
			return true;
		}

		if (key == "tile-smooth" || key == "tilesmooth")
		{
			try
			{
				// Above zero, not at it: a weight of zero believes nothing of
				// any frame, so every tile holds whatever the first frame gave
				// it for ever and the allocator is not allocating at all.
				config.TileSmoothOverride = Math::Clamp(std::stof(value), 0.001f, 1.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("tile-smooth expects a weight in (0, 1]; got '{0}'", value);
				return false;
			}
			config.HasTileSmoothOverride = true;
			return true;
		}

		if (key == "debug-view-log" || key == "debugviewlog")
			return ParseBool(value, config.DebugViewLog);

		if (key == "debug-view" || key == "debugview")
		{
			const std::string lowered = ToLower(value);
			if (lowered == "none" || lowered == "off")
				config.DebugView = EngineConfig::DebugViewMode::None;
			else if (lowered == "rays")
				config.DebugView = EngineConfig::DebugViewMode::Rays;
			else if (lowered == "lights")
				config.DebugView = EngineConfig::DebugViewMode::Lights;
			else if (lowered == "confidence")
				config.DebugView = EngineConfig::DebugViewMode::Confidence;
			else if (lowered == "importance" || lowered == "allocation")
				config.DebugView = EngineConfig::DebugViewMode::Importance;
			else if (lowered == "importance-gi" || lowered == "importancegi"
					 || lowered == "allocation-gi" || lowered == "gi")
				config.DebugView = EngineConfig::DebugViewMode::GiImportance;
			// The traced reflection accumulator's memory: how many frames
			// stand behind each glossy texel, black for none and white for
			// its full 24. A parked camera should read white on every glossy
			// surface; where it reads less, the history is being refused or
			// shortened, which is what the mode is for finding.
			else if (lowered == "reflection" || lowered == "reflections")
				config.DebugView = EngineConfig::DebugViewMode::Reflection;
			// The image distance the accumulator reprojects by, metres up
			// to twenty as the ramp; and which history each texel took:
			// black none, the ramp's middle the surface's old place, white
			// the image's.
			else if (lowered == "reflection-image" || lowered == "reflectionimage")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionImage;
			// The integer part is why the texel's own history was refused (0
			// kept, 1 off screen, 2 none, 3 normal, 4 plane, 5 roughness), the
			// fraction which candidate served (0 none, .25 surface, .5 image);
			// the ramp runs 0..6 (RT-first T4).
			else if (lowered == "reflection-choice" || lowered == "reflectionchoice"
					 || lowered == "reflection-refusal" || lowered == "reflectionrefusal")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionChoice;
			// The accumulated reflection picture itself, radiance over four,
			// straight to the output: the reflection layer alone, for tests
			// that need it apart from everything TAA and GI do to the frame.
			else if (lowered == "reflection-picture" || lowered == "reflectionpicture")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;
			// **RT-12.** The temporal resolve's refusal, in the reflection
			// accumulator's encoding: the clause in the integer part (0 kept,
			// 1 off screen, 2 no history, 3 sky crossing, 4 object id, 5 depth,
			// 6 normal), and a half added where the nine-tap search found
			// nothing either -- so a recovered pixel and a disoccluded one are
			// half a band apart rather than indistinguishable.
			else if (lowered == "taa-refusal" || lowered == "taarefusal")
				config.DebugView = EngineConfig::DebugViewMode::TaaRefusal;
			// The frames behind each texel, per signal. Black is a history just
			// refused; white is the signal's full memory -- so this doubles as
			// the confidence *as applied*, because a shortened memory is
			// precisely what RT-6.3's direction test and RT-6.4's match
			// confidence do to a pixel.
			else if (lowered == "direct-history" || lowered == "directhistory")
				config.DebugView = EngineConfig::DebugViewMode::DirectHistory;
			else if (lowered == "gi-history" || lowered == "gihistory")
				config.DebugView = EngineConfig::DebugViewMode::GiHistory;
			else if (lowered == "ao-history" || lowered == "aohistory")
				config.DebugView = EngineConfig::DebugViewMode::AoHistory;
			// The pixel's own temporal spread, per signal.
			else if (lowered == "reflection-sigma" || lowered == "reflectionsigma")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionSigma;
			else if (lowered == "direct-sigma" || lowered == "directsigma")
				config.DebugView = EngineConfig::DebugViewMode::DirectSigma;
			else if (lowered == "gi-sigma" || lowered == "gisigma")
				config.DebugView = EngineConfig::DebugViewMode::GiSigma;
			else if (lowered == "ao-sigma" || lowered == "aosigma")
				config.DebugView = EngineConfig::DebugViewMode::AoSigma;
			else if (lowered == "ao-refusal" || lowered == "aorefusal")
				config.DebugView = EngineConfig::DebugViewMode::AoRefusal;
			// The reflector's stored normal as RGB, and the virtual image's
			// motion in texels: what the direction test reads, and what a
			// swinging reflection does on screen.
			else if (lowered == "reflection-normal" || lowered == "reflectionnormal")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionNormal;
			else if (lowered == "reflection-motion" || lowered == "reflectionmotion")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionMotion;
			// RT-8: the water layer. Grey is no motion; black in the mask view
			// means the layer holds no wave at that pixel, whatever the sea
			// looks like in the picture.
			else if (lowered == "water-motion" || lowered == "watermotion")
				config.DebugView = EngineConfig::DebugViewMode::WaterMotion;
			else if (lowered == "water-mask" || lowered == "watermask")
				config.DebugView = EngineConfig::DebugViewMode::WaterMask;
			// The reflection direction (specification §11), stored
			// octahedrally in the motion lane's two spare channels, and its
			// frame-to-frame difference against the previous history. The
			// difference wants --debug-view-log: a mirror's whole tolerance
			// is cos(1.8 degrees), which is 0.0005 on a 0.1 ramp.
			else if (lowered == "reflection-direction" || lowered == "reflectiondirection")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionDirection;
			else if (lowered == "reflection-direction-delta"
					 || lowered == "reflectiondirectiondelta"
					 || lowered == "reflection-direction-difference")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionDirectionDelta;
			// RT-first T5: the direct light's accumulated diffuse (before the
			// albedo, over four), and the reason its history was refused.
			else if (lowered == "direct-light" || lowered == "directlight")
				config.DebugView = EngineConfig::DebugViewMode::DirectLight;
			else if (lowered == "direct-refusal" || lowered == "directrefusal")
				config.DebugView = EngineConfig::DebugViewMode::DirectRefusal;
			// RT-2: the accumulated occlusion signal, as the lit shader reads it.
			else if (lowered == "ao" || lowered == "occlusion")
				config.DebugView = EngineConfig::DebugViewMode::Occlusion;
			// RT-3: the settled bounce the lit shader adds -- albedo-free
			// irradiance, and dim, so the ramp is one rather than the direct
			// light's sixty-four; and why each texel's history was refused, on
			// the reflections' ramp. `gi` is taken by the budget's importance
			// map, so these are named in full.
			else if (lowered == "gi-light" || lowered == "gilight")
				config.DebugView = EngineConfig::DebugViewMode::GiLight;
			else if (lowered == "gi-refusal" || lowered == "girefusal")
				config.DebugView = EngineConfig::DebugViewMode::GiRefusal;
			else
			{
				RV_CORE_WARN("debug-view expects rays, lights, confidence, importance, "
							 "importance-gi, reflection, reflection-image or "
							 "reflection-choice; got '{0}'",
							 value);
				return false;
			}
			return true;
		}

		if (key == "ray-rate" || key == "rayrate")
		{
			config.HasRayRateOverride = true;
			config.RayRateOverride = value == "2" ? 2.0f : 1.0f;
			if (value != "1" && value != "2")
			{
				RV_CORE_WARN("ray-rate expects 1 or 2; got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "refraction-floor" || key == "refractionfloor")
		{
			try
			{
				config.RefractionFloorOverride = Math::Clamp(std::stof(value), 0.0f, 1.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("refraction-floor expects a number in [0, 1]; got '{0}'", value);
				return false;
			}
			config.HasRefractionFloorOverride = true;
			return true;
		}

		if (key == "rt-optimisation" || key == "rt-optimization" || key == "rtopt")
		{
			static const char* const kLevels[] = { "off", "quality", "balanced", "performance" };
			for (int i = 0; i < 4; i++)
			{
				if (value == kLevels[i])
				{
					config.HasRayOptimisationOverride = true;
					config.RayOptimisationOverride = i;
					return true;
				}
			}
			RV_CORE_WARN("rt-optimisation expects off, quality, balanced or performance; got '{0}'",
						 value);
			return false;
		}

		if (key == "light-cutoff" || key == "lightcutoff")
		{
			// <metres> -- WR-17's cutoff on how far any positional light reaches,
			// for one run; 0 leaves the lights their own ranges.
			try
			{
				config.LightCutoffOverride = Math::Max(std::stof(value), 0.0f);
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("light-cutoff expects metres; got '{0}'", value);
				return false;
			}
			config.HasLightCutoffOverride = true;
			return true;
		}

		if (key == "shadow-rays" || key == "shadowrays")
		{
			// <shape>,<start>,<end>[,<share>] -- WR-17's render settings for one
			// run. The shape is a word, in RenderSettings::ShadowRayFalloff's
			// order; `off` alone needs no numbers.
			static const char* const kShapes[] = { "off", "hard", "linear", "smooth", "log", "share" };

			std::string parts[5];
			size_t count = 0;
			size_t start = 0;
			while (count < 5 && start <= value.size())
			{
				const size_t comma = value.find(',', start);
				parts[count++] = value.substr(start, comma == std::string::npos
														? std::string::npos : comma - start);
				if (comma == std::string::npos)
					break;
				start = comma + 1;
			}

			int shape = -1;
			for (int i = 0; i < 6; i++)
				if (parts[0] == kShapes[i] || (i == 3 && parts[0] == "smoothstep"))
					shape = i;

			if (shape < 0 || (shape > 0 && count < 3))
			{
				RV_CORE_WARN("shadow-rays expects off, or <shape>,<start>,<end>[,<share>] "
							 "with the shape one of hard, linear, smooth, log, share; got '{0}'",
							 value);
				return false;
			}

			// A trailing `lit` makes a skipped ray count its light lit rather
			// than borrow the traced far rays' visibility -- the measurement
			// arm the borrow was judged against. Bit 16 of the shape lane.
			bool lit = false;
			float numbers[3] = { 0.0f, 0.0f, 0.0f };
			try
			{
				size_t number = 0;
				for (size_t i = 1; i < count; i++)
				{
					if (parts[i] == "lit")
						lit = true;
					else if (number < 3)
						numbers[number++] = std::stof(parts[i]);
				}
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("shadow-rays: the distances in '{0}' are not numbers", value);
				return false;
			}

			config.HasShadowRayOverride = true;
			config.ShadowRayShapeOverride = shape + (lit ? 16 : 0);
			config.ShadowRayStartOverride = numbers[0];
			config.ShadowRayEndOverride = numbers[1];
			config.ShadowRayShareOverride = numbers[2];
			return true;
		}

		if (key == "camera")
		{
			// x,y,z,distance,yaw,pitch -- the editor camera's whole state, which
			// is a focal point and an orbit around it rather than a position.
			float parts[6] = { 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f };
			size_t count = 0;
			size_t start = 0;

			while (count < 6 && start <= value.size())
			{
				const size_t comma = value.find(',', start);
				const std::string piece =
					value.substr(start, comma == std::string::npos ? std::string::npos
																   : comma - start);
				try
				{
					parts[count++] = std::stof(piece);
				}
				catch (const std::exception&)
				{
					RV_CORE_WARN("camera expects six numbers "
								 "x,y,z,distance,yaw,pitch; got '{0}'", value);
					return false;
				}

				if (comma == std::string::npos)
					break;
				start = comma + 1;
			}

			if (count != 6)
			{
				RV_CORE_WARN("camera expects six numbers x,y,z,distance,yaw,pitch; "
							 "got {0} in '{1}'", count, value);
				return false;
			}

			config.CameraFocus = Vec3(parts[0], parts[1], parts[2]);
			config.CameraDistance = parts[3];
			config.CameraYaw = parts[4];
			config.CameraPitch = parts[5];
			config.HasCameraPose = true;
			return true;
		}

		if (key == "theme")
		{
			const std::string wanted = ToLower(value);
			if (wanted != "dark" && wanted != "light")
			{
				RV_CORE_WARN("theme expects 'dark' or 'light', got '{0}'", value);
				return false;
			}
			config.Theme = wanted;
			return true;
		}

		if (key == "screenshot-frame" || key == "screenshotframe")
		{
			try
			{
				config.ScreenshotFrame = (uint32_t)Math::Max(std::stoi(value), 1);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("screenshot-frame expects an integer, got '{0}'", value);
				return false;
			}
		}

		if (key == "screenshot-count" || key == "screenshotcount")
		{
			try
			{
				config.ScreenshotCount = (uint32_t)Math::Max(std::stoi(value), 1);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("screenshot-count expects an integer, got '{0}'", value);
				return false;
			}
		}

		if (key == "benchmark")
		{
			try
			{
				config.BenchmarkFrames = (uint32_t)Math::Max(std::stoi(value), 1);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("benchmark expects a frame count, got '{0}'", value);
				return false;
			}
		}

		if (key == "frames-in-flight" || key == "framesinflight")
		{
			try
			{
				const int parsed = std::stoi(value);
				// More than 3 adds latency without adding throughput.
				config.FramesInFlight = (uint32_t)Math::Clamp(parsed, 1, 3);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("frames-in-flight expects an integer, got '{0}'", value);
				return false;
			}
		}

		if (key == "width" || key == "height")
		{
			try
			{
				const int parsed = Math::Clamp(std::stoi(value), 640, 16384);
				(key == "width" ? config.WindowWidth : config.WindowHeight) = (uint32_t)parsed;
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("{0} expects an integer, got '{1}'", key, value);
				return false;
			}
		}

		if (key == "frame-time" || key == "frametime")
		{
			try
			{
				const float seconds = std::stof(value);
				config.FrameTime = seconds > 0.0f ? seconds : 0.0f;
			}
			catch (...)
			{
				RV_CORE_WARN("frame-time expects seconds, got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "fixed-hz" || key == "fixedhz")
		{
			try
			{
				const int parsed = std::stoi(value);
				// Below 20 the simulation is visibly steppy and fast collisions
				// tunnel through thin geometry; above 240 it burns CPU for
				// nothing a display can show.
				config.FixedHz = (uint32_t)Math::Clamp(parsed, 20, 240);
				config.FixedHzExplicit = true;
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("fixed-hz expects an integer, got '{0}'", value);
				return false;
			}
		}

		RV_CORE_WARN("Unknown config key '{0}'", key);
		return false;
	}

	void EngineConfig::LoadFile(EngineConfig& config, const std::filesystem::path& path)
	{
		std::ifstream file(path);
		if (!file)
			return;

		std::string line;
		while (std::getline(file, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
				continue;

			const size_t separator = line.find('=');
			if (separator == std::string::npos)
				continue;

			ApplyKeyValue(config, line.substr(0, separator), line.substr(separator + 1));
		}

		RV_CORE_INFO("Loaded config from {0}", path.string());
	}

	void EngineConfig::LoadCommandLine(EngineConfig& config, int argc, char** argv)
	{
		for (int i = 1; i < argc; i++)
		{
			std::string argument = argv[i];
			if (argument.rfind("--", 0) != 0)
				continue;
			argument = argument.substr(2);

			const size_t separator = argument.find('=');
			if (separator != std::string::npos)
			{
				ApplyKeyValue(config, argument.substr(0, separator), argument.substr(separator + 1));
			}
			else if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				// Also accept "--rhi vulkan".
				ApplyKeyValue(config, argument, argv[++i]);
			}
			else
			{
				RV_CORE_WARN("Config flag '--{0}' has no value", argument);
			}
		}
	}

	void EngineConfig::Init(int argc, char** argv)
	{
		if (s_Initialized)
		{
			RV_CORE_WARN("EngineConfig::Init called more than once; ignoring");
			return;
		}

		EngineConfig config;

		std::error_code ec;
		const std::filesystem::path iniPath = std::filesystem::current_path(ec) / "ragev.ini";
		if (!ec)
			LoadFile(config, iniPath);

		LoadCommandLine(config, argc, argv);

		// A run that writes a screenshot is measuring, and a measurement must
		// not depend on what the owner last saved in the project. This cost a
		// session: `SampleProject.rvproject` had an uncommitted
		// `RayTracing: true`, the probe scripts did not pass
		// `--render-defaults`, and on a ray-capable device that silently
		// selected the traced form -- so every "voxel" number they printed was
		// the ray-traced one, and a shader edit that changed nothing looked
		// like a backend defect. Refusing is better than defaulting either
		// way: `on` would surprise anyone screenshotting their own game with
		// its own look, and `off` is exactly the trap. So the run has to say.
		if (!config.ScreenshotPath.empty() && !config.HasRenderDefaults)
		{
			RV_CORE_ERROR("--screenshot is a measurement, so it needs "
						  "--render-defaults=on|off stated explicitly.");
			RV_CORE_ERROR("  on  = ignore the project's Render Settings, which is what every "
						  "check script wants: the run then depends only on its command line.");
			RV_CORE_ERROR("  off = use the project's saved Render Settings, for screenshotting "
						  "a project as it is actually configured.");
			RV_CORE_ERROR("Without it a saved setting -- RayTracing above all -- silently "
						  "decides which form of an effect you measured.");
			std::exit(2);
		}

		s_Config = config;
		s_Initialized = true;

		RV_CORE_INFO("Graphics backend: {0} (change with --rhi=vulkan|opengl, or ragev.ini)",
					 BackendName(config.Backend));
	}

	bool EngineConfig::SaveBackendPreference(RHI::Backend backend)
	{
		return SaveSetting("rhi", backend == RHI::Backend::Vulkan ? "vulkan" : "opengl");
	}

	bool EngineConfig::SaveVSyncPreference(bool enabled)
	{
		return SaveSetting("vsync", enabled ? "on" : "off");
	}

	bool EngineConfig::SaveRevealAfterBuildPreference(bool enabled)
	{
		return SaveSetting("reveal-after-build", enabled ? "on" : "off");
	}

	void EngineConfig::SetAntiAliasingOverride(AntiAliasing aa)
	{
		// The storage directly, rather than casting the const off what Get
		// hands out. Same object either way; this one does not require the
		// reader to check whether it is defined behaviour.
		s_Config.AAOverride = aa;
		s_Config.HasAAOverride = true;
	}

	bool EngineConfig::SaveAntiAliasingPreference(AntiAliasing aa)
	{
		// The same names --aa= takes, so the file and the flag are one
		// vocabulary rather than two that have to be kept in step.
		const char* name = "fxaa";
		switch (aa)
		{
			case AntiAliasing::None: name = "none"; break;
			case AntiAliasing::FXAA: name = "fxaa"; break;
			case AntiAliasing::SMAA: name = "smaa"; break;
			case AntiAliasing::SSAA: name = "ssaa"; break;
			case AntiAliasing::MSAA: name = "msaa"; break;
			case AntiAliasing::TAA:  name = "taa";  break;
			default: return false;   // a mode from a later version
		}

		return SaveSetting("aa", name);
	}

	bool EngineConfig::SaveWindowSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return false;

		// Both, or neither: a file carrying a new width beside an old height
		// would open at an aspect ratio nothing was ever left at.
		return SaveSetting("width", std::to_string(width)) &&
			   SaveSetting("height", std::to_string(height));
	}

	bool EngineConfig::SaveSetting(const std::string& key, const std::string& value)
	{
		std::error_code ec;
		const std::filesystem::path path = std::filesystem::current_path(ec) / "ragev.ini";
		if (ec)
		{
			RV_CORE_ERROR("Could not resolve the working directory; {0} not saved", key);
			return false;
		}

		// Read, rewrite, replace. Every other setting in the file is somebody
		// else's and has to survive -- a setting that silently discarded the
		// audio or window settings would be a worse bug than the one it fixes.
		std::vector<std::string> lines;
		bool replaced = false;

		if (std::ifstream existing(path); existing)
		{
			std::string line;
			while (std::getline(existing, line))
			{
				const std::string trimmed = Trim(line);
				const size_t equals = trimmed.find('=');

				if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';' &&
					trimmed[0] != '[' && equals != std::string::npos &&
					ToLower(Trim(trimmed.substr(0, equals))) == key)
				{
					lines.push_back(key + "=" + value);
					replaced = true;
					continue;
				}

				lines.push_back(line);
			}
		}

		if (!replaced)
			lines.push_back(key + "=" + value);

		std::ofstream out(path, std::ios::trunc);
		if (!out)
		{
			RV_CORE_ERROR("Could not write {0}; {1} not saved", path.string(), key);
			return false;
		}

		for (const std::string& line : lines)
			out << line << '\n';

		RV_CORE_INFO("Saved to ragev.ini: {0}={1}", key, value);
		return true;
	}

	const EngineConfig& EngineConfig::Get()
	{
		if (!s_Initialized)
			RV_CORE_WARN("EngineConfig::Get called before Init; using defaults");
		return s_Config;
	}
}
