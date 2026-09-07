"""burst.py gains BURST_SLIDE (WR-16 R5's moving-object test): attach the
engine's Slider to every entity whose tag starts with a prefix, so an
object moves on its own while the camera stands still (--speed=0)."""
p = 'C:/Users/ism19/Code/RageV/tools/scripts/garage/burst.py'
s = open(p, encoding='utf-8').read()
anchor = "    switch = os.environ.get('BURST_SWITCH')"
assert s.count(anchor) == 1, s.count(anchor)
hook = r'''    # BURST_SLIDE="<tag prefix>[,<tag prefix>...]|<speed m/s>|<stop s>" puts
    # the Slider (Source/Slider.cpp) on every entity whose tag starts with
    # one of the prefixes: the object drives along world X while the camera,
    # at --speed=0, stands still -- the moving-object test of WR-16 R5
    # ("porsche_992_gt3_r|1.0|2.0" drives the car for two seconds).
    slide = os.environ.get('BURST_SLIDE')
    if slide:
        prefixes, speed_s, stop_s = slide.rsplit('|', 2)
        prefixes = [x for x in prefixes.split(',') if x]
        block = ('    NativeScriptComponent:\n      Script: Slider\n      Fields:\n'
                 '        Speed: %g\n        StopAfter: %g\n' % (float(speed_s), float(stop_s)))
        out = []
        count = 0
        for part in re.split(r'(?=\n  - EntityID: )', text):
            tag = re.search(r'\n      Tag: (.*)', part)
            if tag and any(tag.group(1).strip().startswith(pfx) for pfx in prefixes):
                assert 'NativeScriptComponent' not in part, tag.group(1)
                body = part.rstrip('\n')
                part = body + '\n' + block.rstrip('\n') + part[len(body):]
                count += 1
            out.append(part)
        text = ''.join(out)
        assert count > 0, 'BURST_SLIDE matched no entity'
        print('slider attached to %d entities: %s m/s for %s s' % (count, speed_s, stop_s))
'''
s = s.replace(anchor, hook + anchor)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('burst.py: BURST_SLIDE added')
