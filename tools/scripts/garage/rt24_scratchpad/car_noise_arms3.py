"""Round 3: which part of the picture carries the extra flicker -- reflections, the GI, the moving blur."""
import car_noise as c
em = c.em
for tag, extra in (('cn_norefl', ['--reflection-pass=off']), ('cn_nogi', ['--rtgi=off']),
                   ('cn_nomblur', ['--reflection-moving-blur=0'])):
    em.burst(tag + '_drive', em.STOP, 60, 70, extra)
    em.burst(tag + '_still', 0.0001, 60, 70, extra)
    d, s = c.flicker(tag + '_drive'), c.flicker(tag + '_still')
    print('%-12s car flicker: cube moving %.2f   cube still %.2f   ratio %.2f' % (tag, d, s, d / s), flush=True)
