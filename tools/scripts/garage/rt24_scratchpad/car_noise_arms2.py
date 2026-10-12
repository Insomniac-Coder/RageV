"""Round 2: is it any moving object, and what changes across the picture when one moves."""
import car_noise as c
em = c.em
SHIP = em.ENTITIES
PLAIN = SHIP[:SHIP.index('  - EntityID: 7311000000000000202')].replace('Emissive: [8, 5, 2.5, 1]', 'Emissive: [0, 0, 0, 1]')
assert PLAIN != SHIP and 'Emissive: [0, 0, 0, 1]' in PLAIN


def arm(tag, entities, extra):
    em.ENTITIES = entities
    em.burst(tag + '_drive', em.STOP, 60, 70, extra)
    em.burst(tag + '_still', 0.0001, 60, 70, extra)
    d, s = c.flicker(tag + '_drive'), c.flicker(tag + '_still')
    print('%-14s car flicker: cube moving %.2f   cube still %.2f   ratio %.2f' % (tag, d, s, d / s), flush=True)


arm('cn_plain', PLAIN, [])
for view in ('rays', 'reflection', 'confidence'):
    em.ENTITIES = SHIP
    extra = ['--debug-view=' + view, '--debug-view-mix=0']
    em.burst('dv_%s_drive' % view, em.STOP, 95, 1, extra)
    em.burst('dv_%s_still' % view, 0.0001, 95, 1, extra)
    print('view', view, 'captured', flush=True)
arm('cn_noconf', SHIP, ['--reflection-confidence-rays=off'])
arm('cn_nodirconf', SHIP, ['--direct-confidence-rays=off'])
em.ENTITIES = SHIP
