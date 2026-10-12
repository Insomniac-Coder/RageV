"""Three suspects for the car's noise while the cube drives, one switched off at a time (headless)."""
import car_noise as c
em = c.em
SHIP = em.ENTITIES
ARMS = [
    ('cn_noshadow', SHIP.replace('CastShadows: true', 'CastShadows: false'), []),
    ('cn_nolight', SHIP[:SHIP.index('  - EntityID: 7311000000000000202')], []),
    ('cn_nopool', SHIP, ['--reflection-lamp-pool=0']),
]
for tag, entities, extra in ARMS:
    em.ENTITIES = entities
    em.burst(tag + '_drive', em.STOP, 60, 70, extra)
    em.burst(tag + '_still', 0.0001, 60, 70, extra)
    d, s = c.flicker(tag + '_drive'), c.flicker(tag + '_still')
    print('%-12s car flicker: cube moving %.2f   cube still %.2f   ratio %.2f' % (tag, d, s, d / s), flush=True)
em.ENTITIES = SHIP
