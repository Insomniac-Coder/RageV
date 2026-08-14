#include <rvpch.h>
#include "FrameGraphBuilder.h"
#include "PostProcess.h"
#include "RageV/Core/EngineConfig.h"
#include "Renderer.h"
#include "UIRenderer.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// How far down the bloom chain goes. Five levels at half-resolution
		// steps reaches 1/32 of the frame, which is a wide enough blur to read
		// as a glow rather than a halo, and stops well before the levels get
		// small enough to shimmer.
		constexpr int kBloomLevels = 5;

		// Radius of the tent filter on the way back up, in texels of the level
		// being read. Wider is smoother and starts to look like a box.
		constexpr float kUpsampleRadius = 1.0f;

		// Below this the chain would be sampling a handful of texels, and the
		// filter stops meaning anything.
		constexpr uint32_t kMinBloomSize = 8;

		// The most coverage samples offered. Four is the point where the
		// quality curve flattens on every measurement anyone publishes, and
		// eight doubles the target's memory for the difference.
		constexpr int kMaxMsaaSamples = 8;

		// The largest SSAA factor offered. Four means sixteen times the pixels
		// shaded, and at a 4K output a 16K scene target -- past what a lot of
		// hardware will allocate and all of what is sensible.
		constexpr int kMaxSupersample = 4;
	}

	void BuildFrame(RenderGraph& graph, const FrameDesc& desc)
	{
		if (desc.Output == kRGInvalid || desc.Width == 0 || desc.Height == 0)
			return;

		// Which filter, resolved once. A scene file can name a mode from a
		// later version, and the failure that causes is silent and total: tone
		// mapping goes into an intermediate, nothing writes the real output,
		// and the window is black with no error anywhere.
		const EngineConfig& config = EngineConfig::Get();
		const AntiAliasing requested = config.HasAAOverride ? config.AAOverride
															: desc.Environment.AA;
		const AntiAliasing aa = PostProcess::IsReady() ? requested : AntiAliasing::None;

		// SSAA is decided here rather than with the other two, because it is
		// the only one that changes the size of the scene target -- everything
		// else in the frame reacts to something that has already been drawn.
		const int requestedFactor = config.SupersampleOverride > 0
			? config.SupersampleOverride
			: desc.Environment.SupersampleFactor;
		const int supersample = aa == AntiAliasing::SSAA
			? Math::Clamp(requestedFactor, 1, kMaxSupersample)
			: 1;

		// --- the scene, in linear HDR -----------------------------------------
		// RGBA16F rather than the 11-11-10 alternative: bloom reads this back
		// and the smallest levels accumulate a lot of energy into few texels,
		// where 10 bits of blue starts to show as a colour cast.
		RGTargetDesc sceneDesc;
		sceneDesc.Name = "SceneHDR";
		sceneDesc.Color = Format::R16G16B16A16_SFLOAT;
		sceneDesc.Depth = Format::D32_SFLOAT;
		// Larger for SSAA, which is the whole of what SSAA does on the way in.
		// The camera's aspect is unchanged, so nothing downstream of the
		// projection needs to know.
		sceneDesc.Scale = (float)supersample;

		// And multisampled for MSAA, which is the whole of what *that* does.
		// The RHI resolves each attachment when a pass ends and hands out the
		// resolve, so nothing below this line can tell either -- see
		// ENGINE-NOTES 7q. What cannot be hidden is the pipeline state: a
		// pipeline's sample count has to equal the attachment's, so the
		// renderers are told before anything is recorded.
		const int msaa = aa == AntiAliasing::MSAA
			? Math::Clamp(config.MsaaOverride > 0 ? config.MsaaOverride
												  : desc.Environment.MsaaSamples, 1, kMaxMsaaSamples)
			: 1;
		sceneDesc.Samples = (uint32_t)msaa;
		Renderer::SetTargetFormats(sceneDesc.Color, sceneDesc.Depth, (uint32_t)msaa,
								   Format::R16G16_SFLOAT);
		// The UI renderer's *world* layer draws inside the scene pass -- world
		// text, and the editor's light and camera marks -- so it takes the
		// scene's sample count. Its screen-space layer is set separately, down
		// with the UI pass, and stays at one.
		UIRenderer::SetWorldTargetFormats(sceneDesc.Color, sceneDesc.Depth, (uint32_t)msaa,
										  Format::R16G16_SFLOAT);

		// Accumulation and revealage live on the *scene's* target rather than
		// one of their own, so the transparent pass depth-tests against the
		// opaque geometry it is drawn over. Three separate targets would each
		// own a depth buffer and the particles would ignore the world.
		//
		// Float accumulation because it is a sum that is meant to exceed one;
		// a single channel of revealage because it is one number.
		const bool wantTransparent = desc.DrawTransparent && desc.ResolveTransparent;
		if (wantTransparent)
		{
			sceneDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R8_UNORM };
		}

		// Motion vectors, in screen space, two half floats a pixel.
		//
		// **Appended**, so transparency keeps attachments 1 and 2 and nothing
		// that already binds them has to learn a new number. Half float rather
		// than 8-bit because a velocity is signed and routinely a small
		// fraction of a pixel, which is exactly where 8 bits has nothing left.
		//
		// Always present rather than only when a temporal filter wants it: a
		// target whose *shape* depends on a setting is a target the reflection
		// probes and every pipeline have to agree with about that setting too,
		// and 7q is the record of how that goes.
		const uint32_t velocityIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(Format::R16G16_SFLOAT);

		const RGResource sceneHDR = graph.CreateTarget(sceneDesc);

		graph.AddPass("Scene",
			[&](RGPassBuilder& builder)
			{
				// Colour and velocity. Not the transparency attachments:
				// a pipeline's declared colour formats have to match what
				// the pass binds, and the pass that accumulates transparency
				// binds a different pair -- which is what WriteAttachments is
				// for. Every pipeline drawing here declares both of these.
				builder.WriteAttachments(sceneHDR,
					{ { 0, desc.ClearColor },
					  // Zero is "did not move", which is what anything that
					  // never writes velocity should read back as.
					  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
				builder.SetClearColor(desc.ClearColor);
			},
			[draw = desc.DrawScene](RGPassContext& context)
			{
				if (draw)
					draw(context);
			});

		// The overlay goes into the HDR target rather than over the finished
		// image, because it depth-tests against the scene it annotates. The
		// cost is that its colours go through the tone curve like everything
		// else, which shifts them slightly -- acceptable for a diagnostic, and
		// the alternative needs the depth buffer in a second pass.
		// Transparency goes in before the overlay, so a collider wireframe is
		// still drawn over the smoke it describes rather than under it.
		if (wantTransparent)
		{
			graph.AddPass("Transparent",
				[&](RGPassBuilder& builder)
				{
					// Accumulation starts at zero and revealage at one: what
					// survives is the product of everything that missed, so
					// "nothing has covered this pixel yet" is one, not zero.
					builder.WriteAttachments(sceneHDR,
						{ { 1, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { 2, Vec4(1.0f, 1.0f, 1.0f, 1.0f) } });

					// Clear these two, keep the depth the scene wrote --
					// which is the whole reason they share a target.
					builder.PreserveDepth();
				},
				[draw = desc.DrawTransparent](RGPassContext& context) { draw(context); });

			graph.AddPass("ResolveTransparent",
				[&](RGPassBuilder& builder)
				{
					builder.WriteAttachments(sceneHDR, { { 0, desc.ClearColor } },
											 RGLoad::Preserve);
					builder.Sample(sceneHDR);
					// A fullscreen composite has nothing to test against, and
					// testing would reject it everywhere the scene is nearer
					// than the far plane -- which is everywhere.
					builder.DisableDepth();
				},
				[resolve = desc.ResolveTransparent, sceneHDR](RGPassContext& context)
				{
					resolve(context, context.Color(sceneHDR, 1), context.Color(sceneHDR, 2));
				});
		}

		if (desc.DrawOverlay)
		{
			graph.AddPass("Overlay",
				[&](RGPassBuilder& builder)
				{
					// Preserve: the scene is already in there. Velocity is
					// bound too, because the debug renderer's pipeline is
					// built for the scene target's shape and this is the
					// scene target.
					builder.WriteAttachments(sceneHDR,
						{ { 0, desc.ClearColor },
						  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } },
						RGLoad::Preserve);
				},
				[draw = desc.DrawOverlay](RGPassContext& context) { draw(context); });
		}

		// --- SSAA resolve --------------------------------------------------------
		//
		// Before bloom and before tone mapping, both deliberately. Averaging is
		// only meaningful where the numbers add up, and after the tone curve
		// they no longer do; and bloom thresholding the *supersampled* image
		// would let a single bright subsample light a whole output pixel, which
		// is the firefly SSAA is supposed to remove.
		RGResource shaded = sceneHDR;
		if (supersample > 1)
		{
			RGTargetDesc resolvedDesc;
			resolvedDesc.Name = "SceneResolved";
			resolvedDesc.Color = Format::R16G16B16A16_SFLOAT;
			resolvedDesc.Depth = Format::Undefined;
			shaded = graph.CreateTarget(resolvedDesc);

			graph.AddPass("SSAA resolve",
				[&](RGPassBuilder& builder)
				{
					builder.Write(shaded);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, supersample](RGPassContext& context)
				{
					PostProcess::SsaaResolve(context.Cmd, context.Color(sceneHDR),
											 context.Width * supersample,
											 context.Height * supersample,
											 Format::R16G16B16A16_SFLOAT, supersample);
				});
		}

		// --- bloom -------------------------------------------------------------
		RGResource bloom = kRGInvalid;
		const bool wantBloom = desc.Environment.BloomEnabled && PostProcess::IsReady();

		std::vector<RGResource> levels;
		if (wantBloom)
		{
			for (int i = 0; i < kBloomLevels; i++)
			{
				const float scale = 1.0f / (float)(1 << (i + 1));
				if ((uint32_t)(desc.Width * scale) < kMinBloomSize ||
					(uint32_t)(desc.Height * scale) < kMinBloomSize)
					break;

				RGTargetDesc level;
				level.Name = "Bloom" + std::to_string(i);
				level.Color = Format::R16G16B16A16_SFLOAT;
				// No depth: nothing in the chain tests or writes it, and
				// attaching one would force every post pipeline to declare a
				// matching depth format.
				level.Depth = Format::Undefined;
				level.Scale = scale;

				levels.push_back(graph.CreateTarget(level));
			}
		}

		if (!levels.empty())
		{
			const SceneEnvironment env = desc.Environment;

			// Down: threshold into the first level, then halve repeatedly.
			graph.AddPass("Bloom prefilter",
				[&](RGPassBuilder& builder)
				{
					builder.Write(levels[0]);
					builder.Sample(shaded);
				},
				[shaded, env](RGPassContext& context)
				{
					PostProcess::Prefilter(context.Cmd, context.Color(shaded),
										   context.Width * 2, context.Height * 2,
										   Format::R16G16B16A16_SFLOAT,
										   env.BloomThreshold, env.BloomKnee,
										   env.BloomClamp);
				});

			for (size_t i = 1; i < levels.size(); i++)
			{
				const RGResource source = levels[i - 1];
				graph.AddPass(("Bloom down " + std::to_string(i)).c_str(),
					[&](RGPassBuilder& builder)
					{
						builder.Write(levels[i]);
						builder.Sample(source);
					},
					[source](RGPassContext& context)
					{
						PostProcess::Downsample(context.Cmd, context.Color(source),
												context.Width * 2, context.Height * 2,
												Format::R16G16B16A16_SFLOAT);
					});
			}

			// Up: blur each level onto the one above it, additively, so the
			// chain accumulates in place and needs no second set of targets.
			for (size_t i = levels.size() - 1; i > 0; i--)
			{
				const RGResource source = levels[i];
				const RGResource destination = levels[i - 1];

				graph.AddPass(("Bloom up " + std::to_string(i)).c_str(),
					[&](RGPassBuilder& builder)
					{
						builder.Write(destination, RGLoad::Preserve);
						builder.Sample(source);
					},
					[source](RGPassContext& context)
					{
						PostProcess::Upsample(context.Cmd, context.Color(source),
											  context.Width / 2, context.Height / 2,
											  Format::R16G16B16A16_SFLOAT, kUpsampleRadius);
					});
			}

			bloom = levels[0];
		}

		// --- tone mapping -------------------------------------------------------
		//
		// SSAA is absent from this test on purpose: its work is already done by
		// here, so tone mapping writes the output directly and the frame ends
		// one pass shorter than either morphological filter.
		const bool wantAA = aa == AntiAliasing::FXAA || aa == AntiAliasing::SMAA;

		// With a post filter on, tone mapping lands in an intermediate that the
		// filter then reads. Both filters work on perceived brightness, so they
		// have to run after the transfer function, not before.
		RGResource tonemapped = desc.Output;
		if (wantAA)
		{
			RGTargetDesc ldr;
			ldr.Name = "Tonemapped";
			ldr.Color = desc.OutputFormat;
			ldr.Depth = Format::Undefined;
			tonemapped = graph.CreateTarget(ldr);
		}

		{
			const SceneEnvironment env = desc.Environment;
			const RGResource bloomSource = bloom;
			const Format format = desc.OutputFormat;

			graph.AddPass("Tonemap",
				[&](RGPassBuilder& builder)
				{
					builder.Write(tonemapped);
					builder.Sample(shaded);
					if (bloomSource != kRGInvalid)
						builder.Sample(bloomSource);
					builder.DisableDepth();
				},
				[shaded, bloomSource, env, format](RGPassContext& context)
				{
					PostProcess::Tonemap(context.Cmd, context.Color(shaded),
										 bloomSource != kRGInvalid ? context.Color(bloomSource)
																   : nullptr,
										 format, env.Exposure,
										 bloomSource != kRGInvalid ? env.BloomIntensity : 0.0f);
				});
		}

		if (aa == AntiAliasing::FXAA)
		{
			const RGResource source = tonemapped;
			const Format format = desc.OutputFormat;

			graph.AddPass("FXAA",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output);
					builder.Sample(source);
					builder.DisableDepth();
				},
				[source, format](RGPassContext& context)
				{
					// The thresholds are the reference implementation's
					// defaults: 1/16 of a full-range step is where an edge
					// stops being worth touching, and 1/8 keeps near-black
					// regions -- where contrast is tiny but banding is most
					// visible -- from being smeared.
					PostProcess::FXAA(context.Cmd, context.Color(source),
									  context.Width, context.Height, format,
									  0.0625f, 0.125f);
				});
		}
		else if (aa == AntiAliasing::SMAA)
		{
			const RGResource source = tonemapped;
			const Format format = desc.OutputFormat;

			// Two intermediates, both full resolution and both small: two
			// bytes a pixel for the edge flags and four for the weights. They
			// are 8-bit because what they hold is a classification and a
			// coverage fraction that never leaves [0, 1/2] -- and because the
			// pass that reads them is bandwidth bound, not precision bound.
			RGTargetDesc edgesDesc;
			edgesDesc.Name = "SMAAEdges";
			edgesDesc.Color = Format::R8G8_UNORM;
			edgesDesc.Depth = Format::Undefined;
			const RGResource edges = graph.CreateTarget(edgesDesc);

			RGTargetDesc weightsDesc;
			weightsDesc.Name = "SMAAWeights";
			weightsDesc.Color = Format::R8G8B8A8_UNORM;
			weightsDesc.Depth = Format::Undefined;
			const RGResource weights = graph.CreateTarget(weightsDesc);

			// Cleared to zero, and the edge pass *relies* on it: it discards
			// rather than writing where there is no edge, so the clear value
			// is what the majority of the frame ends up holding. A clear of
			// anything else would read as an edge everywhere flat.
			const Vec4 empty(0.0f, 0.0f, 0.0f, 0.0f);

			graph.AddPass("SMAA edges",
				[&](RGPassBuilder& builder)
				{
					builder.Write(edges);
					builder.SetClearColor(empty);
					builder.Sample(source);
					builder.DisableDepth();
				},
				[source](RGPassContext& context)
				{
					// 0.1 of full-range luma is the reference's default for
					// its quality preset, and 2.0 is its local contrast
					// factor: an edge survives only if it is at least half
					// the strongest edge beside it, which is what keeps a
					// soft gradient next to a hard silhouette from being
					// treated as one.
					PostProcess::SmaaEdges(context.Cmd, context.Color(source),
										   context.Width, context.Height,
										   Format::R8G8_UNORM, 0.1f, 2.0f);
				});

			graph.AddPass("SMAA weights",
				[&](RGPassBuilder& builder)
				{
					builder.Write(weights);
					builder.SetClearColor(empty);
					builder.Sample(edges);
					builder.DisableDepth();
				},
				[edges](RGPassContext& context)
				{
					PostProcess::SmaaWeights(context.Cmd, context.Color(edges),
											 context.Width, context.Height,
											 Format::R8G8B8A8_UNORM);
				});

			graph.AddPass("SMAA blend",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output);
					builder.Sample(source);
					builder.Sample(weights);
					builder.DisableDepth();
				},
				[source, weights, format](RGPassContext& context)
				{
					PostProcess::SmaaBlend(context.Cmd, context.Color(source),
										   context.Color(weights),
										   context.Width, context.Height, format);
				});
		}

		// --- the UI, last ------------------------------------------------------
		//
		// After everything, into the finished image. Preserve, obviously -- the
		// frame is already in there.
		//
		// The pass declares no depth: UI layering is the order quads were
		// submitted, which is the order somebody authored, and a depth buffer
		// would make it depend on numbers nobody set.
		if (desc.DrawUI)
		{
			// The UI pipeline is built against the *output* format, not the
			// scene's HDR one. Renderer::SetTargetFormats speaks for the scene
			// and would give this the wrong answer.
			UIRenderer::SetTargetFormats(desc.OutputFormat, Format::Undefined);

			graph.AddPass("UI",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output, RGLoad::Preserve);
					builder.DisableDepth();
				},
				[draw = desc.DrawUI](RGPassContext& context) { draw(context); });
		}
	}
}
