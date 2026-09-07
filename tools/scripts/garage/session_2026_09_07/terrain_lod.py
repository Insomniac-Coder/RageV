"""RT-2.1 (measure): an offline replica of Terrain::SelectLod for the bay
terrain -- the same error metric, distance rule, ground veto, neighbour cap,
skirt rule and frustum test as Terrain.cpp / Frustum.cpp -- so a change to
the level rule can be sized without a rebuild. The runtime's benchmark line
(patch_rt21a.py) is the truth; this says why the numbers are what they are.
Usage: python terrain_lod.py [ratio-multipliers...]"""
import struct, math, sys
import numpy as np

ROOT = r'C:\Users\ism19\Code\RageV'
PATH = ROOT + r'\SampleProject\assets\terrain\bay.rvterrain'
SIZE, HEIGHT = 2800.0, 286.8457
ORIGIN = np.array([0.0, -125.0, 0.0])
CHUNK, LEVELS, RATIO = 64, 4, 0.0003
FOV_DEG, ASPECT, NEAR, FAR = 55.0, 1600.0 / 900.0, 0.05, 8000.0
CAMERAS = {  # x, y, z, distance, yaw, pitch -- bench_night.py's poses
    'headland': (500, 89.47, -1100, 0.01, -157.08, 8.88),
    'deck': (0, 76.4, 950, 0.01, 0, 0),
    'pier': (70, 4.5, 705, 0.01, -46.98, -2.86),
    'glitter': (500, 2.5, 180, 0.01, -90, -1.146),
}

b = open(PATH, 'rb').read()
res = struct.unpack_from('<I', b, 8)[0]
H = np.frombuffer(b, np.uint16, res * res, 16).reshape(res, res).astype(np.float64) / 65535.0 * HEIGHT  # [z][x]
quads = res - 1; cell = SIZE / quads; cps = quads // CHUNK; width = cell * CHUNK; half = SIZE / 2


def level_error(blk, s):
    n = blk.shape[0] - 1
    idx = np.arange(n + 1); lo = np.minimum(idx // s, n // s - 1); t = (idx - lo * s) / s
    c = blk[::s, ::s]
    rows = c[lo, :] * (1 - t)[:, None] + c[lo + 1, :] * t[:, None]
    coarse = rows[:, lo] * (1 - t)[None, :] + rows[:, lo + 1] * t[None, :]
    return float(np.max(np.abs(coarse - blk)))


class Chunk:
    pass


chunks = []
for cz in range(cps):
    for cx in range(cps):
        c = Chunk(); c.cx, c.cz = cx, cz
        blk = H[cz * CHUNK: cz * CHUNK + CHUNK + 1, cx * CHUNK: cx * CHUNK + CHUNK + 1]
        c.err = [0.0] + [level_error(blk, 1 << l) for l in range(1, LEVELS)]
        low, high = float(blk.min()), float(blk.max())
        depth = max(1.25 * (c.err[3] + c.err[3]) + 0.5 * cell, 0.01) if cps > 1 else 0.0
        c.bmin = np.array([-half + cx * CHUNK * cell, low - depth, -half + cz * CHUNK * cell])
        c.bmax = np.array([-half + (cx + 1) * CHUNK * cell, high, -half + (cz + 1) * CHUNK * cell])
        c.centre = (c.bmin + c.bmax) / 2
        c.edges = (cx > 0) + (cx < cps - 1) + (cz > 0) + (cz < cps - 1)
        chunks.append(c)
grid = lambda cx, cz: chunks[cz * cps + cx]


def level_for(d):
    ratio = d / (4.0 * width)
    if ratio < 1.0: return 0
    return min(max(int(math.floor(math.log2(ratio))) + 1, 0), LEVELS - 1)


def height_at(lx, lz):
    fx = min(max((lx + half) / cell, 0.0), quads - 1e-9); fz = min(max((lz + half) / cell, 0.0), quads - 1e-9)
    x0, z0 = int(fx), int(fz); tx, tz = fx - x0, fz - z0
    h00, h10, h01, h11 = H[z0, x0], H[z0, x0 + 1], H[z0 + 1, x0], H[z0 + 1, x0 + 1]
    # the builder's diagonal: i00,i01,i11 / i00,i11,i10 -> split along (0,0)-(1,1)
    if tx > tz: return h00 + (h10 - h00) * tx + (h11 - h10) * tz
    return h00 + (h01 - h00) * tz + (h11 - h01) * tx


def quat_from_euler(r):
    hx, hy, hz = r[0] / 2, r[1] / 2, r[2] / 2
    cx, sx, cy, sy, cz, sz = math.cos(hx), math.sin(hx), math.cos(hy), math.sin(hy), math.cos(hz), math.sin(hz)
    return np.array([cx * cy * cz + sx * sy * sz, sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz])  # w,x,y,z


def rotate(q, v):
    w, x, y, z = q; u = np.array([x, y, z])
    return 2 * np.dot(u, v) * u + (w * w - np.dot(u, u)) * v + 2 * w * np.cross(u, v)


def frustum_planes(pose):
    x, y, z, d, yaw, pitch = pose
    rot = np.array([-math.radians(pitch), -math.radians(yaw), 0.0]); q = quat_from_euler(rot)
    fwd = rotate(q, np.array([0.0, 0.0, -1.0])); right = rotate(q, np.array([1.0, 0.0, 0.0])); up = rotate(q, np.array([0.0, 1.0, 0.0]))
    pos = np.array([x, y, z], dtype=float) - fwd * max(d, 0.01)
    R = np.stack([right, up, fwd * -1.0], axis=1)  # columns: camera axes in world (z = back)
    V = np.eye(4); V[:3, :3] = R.T; V[:3, 3] = -R.T @ pos
    f = 1.0 / math.tan(math.radians(FOV_DEG) / 2)
    P = np.zeros((4, 4)); P[0, 0] = f / ASPECT; P[1, 1] = f; P[2, 2] = FAR / (NEAR - FAR); P[2, 3] = -(FAR * NEAR) / (FAR - NEAR); P[3, 2] = -1.0
    M = P @ V
    r = [M[i, :] for i in range(4)]
    planes = [r[3] + r[0], r[3] - r[0], r[3] + r[1], r[3] - r[1], r[2], r[3] - r[2]]
    return [p / np.linalg.norm(p[:3]) for p in planes], pos, fwd


def intersects(planes, centre, extents):
    for p in planes:
        reach = np.dot(np.abs(p[:3]), extents)
        if np.dot(p[:3], centre) + p[3] + reach < 0: return False
    return True


def select(pose, ratio):
    planes, pos, fwd = frustum_planes(pose)
    vetoed = 0
    for c in chunks:
        dist = np.linalg.norm(pos - (c.centre + ORIGIN))
        w = level_for(dist); lv = w
        while lv > 0 and c.err[lv] > dist * ratio: lv -= 1
        c.level = lv; c.wanted = w; c.dist = dist
        if lv < w: vetoed += 1
    before = [c.level for c in chunks]
    for _ in range(LEVELS):
        changed = False
        for c in chunks:
            cap = LEVELS - 1
            for nx, nz in ((c.cx - 1, c.cz), (c.cx + 1, c.cz), (c.cx, c.cz - 1), (c.cx, c.cz + 1)):
                if 0 <= nx < cps and 0 <= nz < cps: cap = min(cap, grid(nx, nz).level + 1)
            if c.level > cap: c.level = cap; changed = True
        if not changed: break
    capped = sum(1 for c, b0 in zip(chunks, before) if c.level < b0)
    local = pos - ORIGIN
    skirts_drawn = local[1] >= height_at(local[0], local[2])
    for c in chunks:
        c.skirt = any(0 <= nx < cps and 0 <= nz < cps and grid(nx, nz).level != c.level
                      for nx, nz in ((c.cx - 1, c.cz), (c.cx + 1, c.cz), (c.cx, c.cz - 1), (c.cx, c.cz + 1)))
        n = CHUNK >> c.level
        c.tris = 2 * n * n + (c.edges * n * 4 if (skirts_drawn and c.skirt) else 0)
        centre = c.centre + ORIGIN; extents = (c.bmax - c.bmin) / 2
        c.visible = intersects(planes, centre, extents)
    return dict(vetoed=vetoed, capped=capped, skirts=skirts_drawn, pos=pos, fwd=fwd, local=local,
                ground=height_at(local[0], local[2]))


def report(name, pose, ratio):
    r = select(pose, ratio)
    allc = [sum(1 for c in chunks if c.level == l) for l in range(LEVELS)]
    vis = [sum(1 for c in chunks if c.visible and c.level == l) for l in range(LEVELS)]
    tris = [sum(c.tris for c in chunks if c.visible and c.level == l) for l in range(LEVELS)]
    want = [sum(1 for c in chunks if c.wanted == l) for l in range(LEVELS)]
    wantv = [sum(1 for c in chunks if c.visible and c.wanted == l) for l in range(LEVELS)]
    print(f"[{name}] ratio {ratio:g}: camera {np.round(r['pos'],1)} fwd {np.round(r['fwd'],3)}; local y {r['local'][1]:.1f} over ground {r['ground']:.1f} -> skirts {'on' if r['skirts'] else 'OFF'}")
    print(f"   all {len(chunks)} chunks: by distance L0..3 {want}; final {allc}; veto held {r['vetoed']} finer, cap {r['capped']}")
    print(f"   visible {sum(vis)}: by distance {wantv}; final {vis}; triangles per level {tris} = {sum(tris):,}")
    return sum(tris)


def level_map(name, pose, ratio):
    select(pose, ratio)
    print(f"   level map ({name}, ratio {ratio:g}; '.' = culled; x -> right, z -> down):")
    for cz in range(cps):
        print('   ' + ' '.join((str(grid(cx, cz).level) if grid(cx, cz).visible else '.') for cx in range(cps)))


if __name__ == '__main__':
    mults = [float(a) for a in sys.argv[1:]] or [1.0]
    errs = np.array([c.err for c in chunks])
    print(f"grid {res}^2, cell {cell:.3f} m, {cps}x{cps} chunks of {width:.1f} m; level error (m) per chunk, percentiles 50/90/max:")
    for l in range(1, LEVELS):
        print(f"   L{l}: {np.percentile(errs[:,l],50):.2f} / {np.percentile(errs[:,l],90):.2f} / {errs[:,l].max():.2f}")
    for name, pose in CAMERAS.items():
        for m in mults:
            report(name, pose, RATIO * m)
        if name == 'headland':
            for m in mults:
                level_map(name, pose, RATIO * m)
