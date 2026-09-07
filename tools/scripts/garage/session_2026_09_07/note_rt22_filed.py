"""RT-2.2 filed (owner, 2026-09-07): the deferred resolve is an item for the
end of the RT series, not a question. The rows, the record, the hand-off's
question, NEXT.md's header, memory."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')


def load(p):
    s = open(p, 'rb').read().decode('utf-8')
    return s, ('\r\n' if s.count('\r\n') > s.count('\n') / 2 else '\n')


def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)


def rep(s, nl, old, new):
    for ending in (nl, '\n' if nl == '\r\n' else '\r\n'):
        o = old.replace('\n', ending)
        if s.count(o) == 1:
            return s.replace(o, new.replace('\n', ending))
    raise AssertionError('anchor: ' + old[:90])


# --- RT-SERIES.md ------------------------------------------------------------
p = 'docs/RT-SERIES.md'; s, nl = load(p)
assert 'RT-2.2' not in s
# The build-order row, after RT-2.1's.
old = "| measure 0.5 d; the resolve medium — **✅ done 2026-09-07, record below: the measurement, and the fix it pointed at (the parallax march at mip 0, not the raster); the resolve proposed with its number, not built** |"
assert s.count(old) == 1
s = s.replace(old, "| measure 0.5 d; the resolve medium — **✅ done 2026-09-07, record below: the measurement, and the fix it pointed at (the parallax march at mip 0, not the raster); the resolve is RT-2.2** |"
              + nl + "| **RT-2.2** | **The deferred resolve** (owner-filed 2026-09-07 from RT-2.1's finding, **for the end of the series** -- after RT-13, not before). The lit pass reads albedo, normal, roughness, metallic, specular and occlusion from the G-buffer for every opaque kind instead of sampling the material again; the raster kept, so the tangent, the emissive map and the coat and sheen uniforms stay where they are and no lane is added (`RV_GBUFFER_FED` on the six lit-kind pipelines, three bindings re-committed in `DrawLit` the way 26-28 are, verified by diff image against the sampled path). **Precondition:** the albedo lane is `R8G8B8A8_UNORM` linear; it goes to sRGB8 or 16F first, or the lit pass bands in the dark tones. | RT-2.1's record | The material evaluated once, in the G-buffer, is the RT-first shape; worth ~0.6 ms at Headland today, more where heavy materials sit near the camera. | small; **at the end** |", 1)
# The complexity row, after RT-2.1's.
old = "| RT-2.1 | ✅ done in a day (2026-09-07): the measurement and the parallax fix; the resolve is proposed at ~0.6 ms on Headland, gated on the albedo lane's storage | low |"
assert s.count(old) == 1
i = s.index(old); j = s.index(nl, i)
s = s[:j] + nl + "| RT-2.2 | 1 d (the lane's storage first, then the variant) | low | The albedo lane's quantisation is the one way it can change the picture; a diff image against the sampled path settles it. **Deferred to the end of the series by the owner (2026-09-07).** |" + s[j:]
# The record's proposal paragraph.
s = rep(s, nl, "**Not built: it has a precondition for the owner.**",
        "**Not built here: filed by the owner as RT-2.2, for the end of the series. Its precondition:**")
s = rep(s, nl, "A small item; the numbers say it is not urgent.",
        "A small item; the numbers say it is not urgent, which is why it waits.")
save(p, s)

# --- HANDOFF.md --------------------------------------------------------------
p = 'docs/HANDOFF.md'; s, nl = load(p)
assert 'RT-2.2' not in s
s = rep(s, nl, "The AO look is accepted; the deferred resolve is proposed (RT-2.1's record) and waits on the owner.",
        "The AO look is accepted; the deferred resolve is **RT-2.2**, owner-filed for the end of the series.")
s = rep(s, nl, "- **The resolve, proposed and not built:** worth ~0.6 ms at Headland now; the raster kept, the material read from the G-buffer; **gated on the albedo lane** (8-bit linear today; sRGB8 or 16F first). The owner decides whether it earns an item.",
        "- **The resolve, not built:** worth ~0.6 ms at Headland now; the raster kept, the material read from the G-buffer; gated on the albedo lane (8-bit linear today; sRGB8 or 16F first). **Owner-filed as RT-2.2 (2026-09-07), for the end of the RT series** -- its row is in RT-SERIES.md.")
s = rep(s, nl, """1. **The deferred resolve.** It is now worth about 0.6 ms at Headland (the material sampled a second time by the lit pass), and it needs the G-buffer's albedo lane in sRGB8 or 16F first, or the lit pass bands in the dark tones. **Do you want it built as a small item, and if so, which storage for the lane?**
2. **RT-3 is next on the list** (GI as a signal this frame). **Green signal?**""",
        """1. **The deferred resolve.** It is now worth about 0.6 ms at Headland (the material sampled a second time by the lit pass), and it needs the G-buffer's albedo lane in sRGB8 or 16F first, or the lit pass bands in the dark tones. **Answered by the owner 2026-09-07: noted as RT-2.2, to be looked into at the end of the series.**
2. **RT-3 is next on the list** (GI as a signal this frame). **Green signal?** -- still open.""")
save(p, s)

# --- NEXT.md -----------------------------------------------------------------
p = 'docs/NEXT.md'; s, nl = load(p)
s = rep(s, nl, "the deferred resolve is proposed at ~0.6 ms and gated on the albedo lane's storage.**",
        "the deferred resolve is RT-2.2, owner-filed for the end of the series.**")
save(p, s)

# --- memory ------------------------------------------------------------------
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\project_ragev_rt_series_state.md'; s, nl = load(p)
s = rep(s, nl, "The deferred resolve is PROPOSED, not built (~0.6 ms\nat Headland; gated on the albedo lane going sRGB8/16F).",
        "The deferred resolve is RT-2.2, owner-filed for the END of\nthe RT series (~0.6 ms at Headland; the albedo lane goes sRGB8/16F first) -- not before.")
s = rep(s, nl, "the AO look accepted; the deferred resolve proposed and waiting on the owner;",
        "the AO look accepted; the deferred resolve filed as RT-2.2 for the end of the series;")
save(p, s)
p = r'C:\Users\ism19\.claude\projects\C--Users-ism19-Code\memory\MEMORY.md'; s, nl = load(p)
s = rep(s, nl, "AO look accepted; the deferred resolve proposed, gated on the albedo lane;",
        "AO look accepted; the deferred resolve is RT-2.2, for the end of the series;")
save(p, s)
print('RT-2.2 filed')
