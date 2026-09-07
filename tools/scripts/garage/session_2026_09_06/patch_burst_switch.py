"""burst.py gains BURST_SWITCH (WR-16 R4's change test): attach the Switcher
native script to every entity whose tag starts with a given prefix."""
p = 'C:/Users/ism19/Code/RageV/tools/scripts/garage/burst.py'
s = open(p, encoding='utf-8').read()
anchor = "    old = re.search(r'    ManagedScriptComponent:"
assert s.count(anchor) == 1, s.count(anchor)
hook = r'''    # BURST_SWITCH="<tag prefix>[,<tag prefix>...]|<seconds>" attaches the
    # engine's Switcher (Source/Switcher.cpp) to every entity whose tag
    # starts with one of the prefixes: after <seconds> its light goes to
    # Intensity 0 and its mesh to a flat emissive of 0 -- the change test of
    # WR-16 R4 ("Tube ,Bottom light bars|1.328" switches the garage's tubes
    # and their lenses off at frame 80 of a --frame-time=0.0166 run).
    switch = os.environ.get('BURST_SWITCH')
    if switch:
        prefixes, seconds = switch.rsplit('|', 1)
        prefixes = [x for x in prefixes.split(',') if x]
        block = ('    NativeScriptComponent:\n      Script: Switcher\n      Fields:\n'
                 '        AtSeconds: %g\n        Intensity: 0\n        Emissive: 0\n' % float(seconds))
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
        assert count > 0, 'BURST_SWITCH matched no entity'
        print('switcher attached to %d entities at %s s' % (count, seconds))
'''
s = s.replace(anchor, hook + anchor)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('burst.py: BURST_SWITCH added')
