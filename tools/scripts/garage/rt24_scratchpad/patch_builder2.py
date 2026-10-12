"""Builder: milestone M7 (RT2-44..45), the new totals, and audit tables with the Found column anywhere."""
import io

P = 'C:/Users/ism19/AppData/Local/Temp/claude/C--Users-ism19-Code/69b2a2c1-0da8-459a-8242-c1e5d00ae840/scratchpad/rt2/build_page.py'
t = io.open(P, encoding='utf-8').read()


def replace(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:80])
    t = t.replace(a, b)


replace("    ('M6', 'Scale', 34, 43, '64-81', 72.5),\n]",
        "    ('M6', 'Scale', 34, 43, '64-81', 72.5),\n"
        "    ('M7', 'The fallback and the post chain', 44, 45, '7-10', 8.5),\n]")

# audit tables: the chip goes in whichever column is called Found
replace("""    audit = header == AUDIT_HEAD
    if audit:
        cls += ' audit'""", """    audit = 'Found' in header and 'On the roadmap' in header
    found_col = header.index('Found') if audit else -1
    if audit:
        cls += ' audit' if header == AUDIT_HEAD else ' audit audit2'""")
replace("            elif audit and ci == 3 and AUDIT_CHIP.match(c):",
        "            elif audit and ci == found_col and AUDIT_CHIP.match(c):")

replace("<li><b>44</b><span>items</span></li>", "<li><b>46</b><span>items</span></li>")
replace("<li><b>7</b><span>milestones</span></li>", "<li><b>8</b><span>milestones</span></li>")
replace("<li><b>228&ndash;293</b><span>working sessions</span></li>", "<li><b>235&ndash;303</b><span>working sessions</span></li>")
replace("the work is laid out in seven milestones", "the work is laid out in eight milestones")
replace('aria-label="Seven milestones, M0 to M6, sized', 'aria-label="Eight milestones, M0 to M7, sized')
replace("table.audit tr.group td{", "table.audit2 td:nth-child(3){min-width:190px}\ntable.audit2 td:nth-child(4){min-width:300px}\ntable.audit tr.group td{")

io.open(P, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
