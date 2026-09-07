"""RT-first T5, the frame graph and the debug views (the tail of 5f, which
stopped at the hoisted block's anchors: edit the block first, then dedent).
Do not re-run 5f."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
assert 'directLit' not in s, 'already patched'
start = "\t\t\tstruct SignalPassNames { const char* Accumulate; const char* Blur[3]; };"
endmark = "\t\t\t\treturn blurred;" + nl + "\t\t\t};" + nl
a = s.find(start); assert a >= 0
b = s.find(endmark, a); assert b >= 0
b += len(endmark)
block = s[a:b]
s = s[:a] + s[b:]
# the pair, on the block at its original indentation
block = rep(block, nl, """CameraMotion* motion, RGTargetDesc blurDesc) -> RGResource""",
            """CameraMotion* motion, RGTargetDesc blurDesc, bool pair) -> RGResource""")
import re as _re
def line(block, old, new):
    """Replace one whole line's text (comments between lines cannot break it)."""
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert block.count(o) == 1, (block.count(o), old[:60]); return block.replace(o, n)
block = line(block, "[params, fresh, sceneHDR, normalIndex, velocityIndex, current, previous, hasHistory, motion]",
             "[params, fresh, sceneHDR, normalIndex, velocityIndex, current, previous, hasHistory, motion, pair]")
block = line(block, "\t\t\t\t\t\t\t*motion, hasHistory);",
             "\t\t\t\t\t\t\t*motion, hasHistory,\n\t\t\t\t\t\t\tpair ? context.Color(fresh, 1) : nullptr,\n\t\t\t\t\t\t\tpair && hasHistory ? context.Color(previous, 3) : nullptr);")
block = line(block, "\t\t\t\tblurDesc.ExtraColors.clear();",
             "\t\t\t\tblurDesc.ExtraColors.clear();\n\t\t\t\tif (pair)\n\t\t\t\t\tblurDesc.ExtraColors.push_back(blurDesc.Color);")
block = line(block, "[params, input, current, sceneHDR, normalIndex, stride](RGPassContext& context)",
             "[params, input, current, sceneHDR, normalIndex, stride, pair](RGPassContext& context)")
m = _re.search(r"([ \t]+)stride\);", block); assert m and block.count("stride);") == 1
indent = m.group(1)
block = block.replace(indent + "stride);", indent + "stride," + nl + indent
                      + "// the twin: attachment 3 of the accumulated target, 1 of a blurred one" + nl + indent
                      + "pair ? context.Color(input, input == current ? 3 : 1) : nullptr);")
dedented = block.replace(nl + '\t\t\t', nl + '\t\t')
assert dedented.startswith('\t')
dedented = dedented[1:]
anchor = "\t\tconst bool gbufferPass = Renderer3D::GBufferPassAvailable();"
assert s.count(anchor) == 1
s = s.replace(anchor, "\t\t// The reconstruction contract as passes (RT-first T4), for any signal:" + nl
              + "\t\t// the accumulate and the three a-trous blurs. Above the G-buffer pass" + nl
              + "\t\t// because the direct light (T5) runs between it and the lit pass." + nl
              + dedented + anchor)
s = rep(s, nl, """															reflectionHistory, &desc.Reflections->Motion(),
															reflectionBlurDesc);""",
       """															reflectionHistory, &desc.Reflections->Motion(),
															reflectionBlurDesc, false);""")
# the direct light on the contract
s = rep(s, nl, """		RGResource directTraced = kRGInvalid;
		if (directSignal)
		{""", """		RGResource directTraced = kRGInvalid;
		RGResource directLit = kRGInvalid;      // what the lit pass adds: the blurred pair
		RGResource currentDirect = kRGInvalid;  // the accumulated pair, for the debug views
		if (directSignal)
		{""")
s = rep(s, nl, """												 Format::R16G16B16A16_SFLOAT,
												 directView, directRays);
				});
		}
""", """												 Format::R16G16B16A16_SFLOAT,
												 directView, directRays);
				});
			// The contract: the pair accumulated over the frames behind it
			// (surface reprojection, the tests, the bound, the motion-capped
			// memory) and blurred while young. Four attachments: the diffuse,
			// the surface, the extra, the specular twin.
			directLit = directTraced;
			TemporalHistory& direct = *desc.DirectLight;
			direct.Prepare(Renderer::GetDevice(),
						   desc.Width * (uint32_t)supersample, desc.Height * (uint32_t)supersample,
						   Format::R16G16B16A16_SFLOAT, "DirectLight",
						   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT,
						   Format::R16G16B16A16_SFLOAT);
			if (direct.Current() && direct.Previous())
			{
				const RGResource previousDirect = graph.Import(direct.Previous(), "DirectPrevious");
				currentDirect = graph.Import(direct.Current(), "DirectCurrent");
				const bool directHistory = direct.HasHistory();
				RGTargetDesc directBlurDesc = directDesc;
				directBlurDesc.Name = "DirectBlurred";
				static const SignalPassNames kDirectPasses =
					{ "DirectAccumulate", { "DirectBlur", "DirectBlur2", "DirectBlur4" } };
				directLit = addSignal(kDirectPasses, Renderer3D::DirectSignal(),
									  directTraced, currentDirect, previousDirect, directHistory,
									  &direct.Motion(), directBlurDesc, true);
				direct.Advance();
			}
		}
		else if (desc.DirectLight)
		{
			desc.DirectLight->Invalidate();
		}
""")
s = rep(s, nl, """				if (directTraced != kRGInvalid)
					builder.Sample(directTraced);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter, directTraced,""",
       """				if (directLit != kRGInvalid)
					builder.Sample(directLit);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter, directLit,""")
s = rep(s, nl, """								  Renderer3D::SetDirectLight(
									  directTraced != kRGInvalid ? context.Color(directTraced, 0) : nullptr,
									  directTraced != kRGInvalid ? context.Color(directTraced, 1) : nullptr);""",
       """								  Renderer3D::SetDirectLight(
									  directLit != kRGInvalid ? context.Color(directLit, 0) : nullptr,
									  directLit != kRGInvalid ? context.Color(directLit, 1) : nullptr);""")
# the debug views
s = rep(s, nl, """							  : view == EngineConfig::DebugViewMode::ReflectionPicture ? 4.0f
									: 1.0f;""", """							  : view == EngineConfig::DebugViewMode::ReflectionPicture ? 4.0f
							  // The direct light's accumulated diffuse over four; its
							  // refusal reason on the same ramp as the reflections'.
							  : view == EngineConfig::DebugViewMode::DirectLight ? 4.0f
							  : view == EngineConfig::DebugViewMode::DirectRefusal ? 6.0f
									: 1.0f;""")
s = rep(s, nl, """										 : reflectionView
											   ? (tracedReflections ? currentReflections : kRGInvalid)
											   : kRGInvalid;""", """										 : reflectionView
											   ? (tracedReflections ? currentReflections : kRGInvalid)
										 : (view == EngineConfig::DebugViewMode::DirectLight
											|| view == EngineConfig::DebugViewMode::DirectRefusal)
											   ? currentDirect
											   : kRGInvalid;""")
s = rep(s, nl, """										 : view == EngineConfig::DebugViewMode::ReflectionChoice ? 2u
										 : 0u;""", """										 : view == EngineConfig::DebugViewMode::ReflectionChoice ? 2u
										 : view == EngineConfig::DebugViewMode::DirectRefusal ? 2u
										 : 0u;""")
save(p, s)

p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
s = rep(s, nl, """								   ReflectionChoice, ReflectionPicture };""",
       """								   ReflectionChoice, ReflectionPicture,
								   DirectLight, DirectRefusal };"""); save(p, s)
p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
s = rep(s, nl, """			else if (lowered == "reflection-picture" || lowered == "reflectionpicture")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;
""", """			else if (lowered == "reflection-picture" || lowered == "reflectionpicture")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;
			// RT-first T5: the direct light's accumulated diffuse (before the
			// albedo, over four), and the reason its history was refused.
			else if (lowered == "direct-light" || lowered == "directlight")
				config.DebugView = EngineConfig::DebugViewMode::DirectLight;
			else if (lowered == "direct-refusal" || lowered == "directrefusal")
				config.DebugView = EngineConfig::DebugViewMode::DirectRefusal;
"""); save(p, s)
p = 'RageVEditor/assets/shaders/debug_view.rvshader'; s, nl = load(p)
s = rep(s, nl, """	else if (mode == 8)""", """	else if (mode == 8 || mode == 10)   // reflection-picture, direct-light: the picture over the scale""")
save(p, s)
print('T5g patched')
