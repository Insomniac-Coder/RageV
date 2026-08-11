#include <rvpch.h>
#include "FrameGraphBuilder.h"
#include "PostProcess.h"
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
	}

	void BuildFrame(RenderGraph& graph, const FrameDesc& desc)
	{
		if (desc.Output == kRGInvalid || desc.Width == 0 || desc.Height == 0)
			return;

		// --- the scene, in linear HDR -----------------------------------------
		// RGBA16F rather than the 11-11-10 alternative: bloom reads this back
		// and the smallest levels accumulate a lot of energy into few texels,
		// where 10 bits of blue starts to show as a colour cast.
		RGTargetDesc sceneDesc;
		sceneDesc.Name = "SceneHDR";
		sceneDesc.Color = Format::R16G16B16A16_SFLOAT;
		sceneDesc.Depth = Format::D32_SFLOAT;

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

		const RGResource sceneHDR = graph.CreateTarget(sceneDesc);

		graph.AddPass("Scene",
			[&](RGPassBuilder& builder)
			{
				// Attachment 0 only: every pipeline that draws the scene
				// declares one colour format, and a pass binding three would
				// require all of them to declare three.
				builder.WriteAttachments(sceneHDR, { { 0, desc.ClearColor } });
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
					// Preserve: the scene is already in there.
					builder.WriteAttachments(sceneHDR, { { 0, desc.ClearColor } },
											 RGLoad::Preserve);
				},
				[draw = desc.DrawOverlay](RGPassContext& context) { draw(context); });
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
					builder.Sample(sceneHDR);
				},
				[sceneHDR, env](RGPassContext& context)
				{
					PostProcess::Prefilter(context.Cmd, context.Color(sceneHDR),
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
		const bool wantAA = desc.Environment.AA != AntiAliasing::None && PostProcess::IsReady();

		// With anti-aliasing on, tone mapping lands in an intermediate that
		// FXAA then reads. FXAA works on perceived brightness, so it has to run
		// after the transfer function, not before.
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
			const Format format = wantAA ? desc.OutputFormat : desc.OutputFormat;

			graph.AddPass("Tonemap",
				[&](RGPassBuilder& builder)
				{
					builder.Write(tonemapped);
					builder.Sample(sceneHDR);
					if (bloomSource != kRGInvalid)
						builder.Sample(bloomSource);
					builder.DisableDepth();
				},
				[sceneHDR, bloomSource, env, format](RGPassContext& context)
				{
					PostProcess::Tonemap(context.Cmd, context.Color(sceneHDR),
										 bloomSource != kRGInvalid ? context.Color(bloomSource)
																   : nullptr,
										 format, env.Exposure,
										 bloomSource != kRGInvalid ? env.BloomIntensity : 0.0f);
				});
		}

		if (wantAA)
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
