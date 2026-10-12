"""A staged shader arm for the cube drive: the source file's text edited only in the staged copy.
usage: car_noise_stage.py <tag> <shader> <anchor-file> <replacement-file>"""
import io, os, sys
import car_noise as c
em, sr = c.em, c.em.stage_run
tag, shader, was, now = sys.argv[1], sys.argv[2], io.open(sys.argv[3]).read(), io.open(sys.argv[4]).read()
src = io.open(os.path.join(sr.SRC_DIR, shader), encoding='utf-8', newline='').read()
eol = '\r\n' if src.count('\r\n') > src.count('\n') // 2 else '\n'
was, now = was.replace('\n', eol), now.replace('\n', eol)
assert src.count(was) == 1, 'anchor matched %d times' % src.count(was)
real_restore = sr.restore
def staged_restore():
    ok = real_restore()
    io.open(os.path.join(sr.STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(src.replace(was, now))
    return ok
sr.restore = staged_restore
try:
    em.burst(tag + '_drive', em.STOP, 60, 70)
    em.burst(tag + '_still', 0.0001, 60, 70)
finally:
    sr.restore = real_restore
    real_restore()
d, s = c.flicker(tag + '_drive'), c.flicker(tag + '_still')
print('%-12s car flicker: cube moving %.2f   cube still %.2f   ratio %.2f' % (tag, d, s, d / s))
