import re
old=open('docs/RT-SERIES-2.md',encoding='utf8').read().split('\n')
new=open('docs/RT-ROADMAP.md',encoding='utf8').read().split('\n')
def norm(s): return re.sub(r'\s+',' ',s).strip()
def sec(lines,start,end):
    s=next(i for i,l in enumerate(lines) if l.startswith(start))
    e=next((i for i,l in enumerate(lines) if i>s and l.startswith(end)),len(lines))
    return lines[s:e]
o10=sec(old,'## 10.','## 11')
n10=sec(new,'## 10.','## 11')
n10n=norm('\n'.join(n10))
rows=[l for l in o10 if l.startswith('|') and not re.match(r'^\|[-| :]+\|$',l)]
print('old s10 rows',len(rows),'new rows',sum(1 for l in n10 if l.startswith('|')))
m=0
for r in rows:
    if norm(r) not in n10n:
        m+=1
        cells=[c.strip() for c in r.strip('|').split('|')]
        # find row in new starting with same first two cells
        key=cells[0]
        cand=[l for l in n10 if l.startswith('|') and key in l]
        print('ROW changed:',r[:150]); 
        for c in cand[:2]: print('   new:',c[:150])
print('rows not verbatim',m)
# all non-table text lines of old s10
for l in o10:
    if l and not l.startswith('|') and norm(l) not in n10n: print('S10 text changed:',l[:200])
# section 8 ids
o8=sec(old,'## 8.','## 9'); n8=sec(new,'## 8.','## 9')
ids=lambda L:set(re.findall(r'\b[A-Z][A-Z0-9]{1,6}-\d{2,3}[a-z]?\b','\n'.join(L)))
a,b=ids(o8),ids(n8)
print('old s8 ids',len(a),'new',len(b),'missing',sorted(a-b))
n8n=norm('\n'.join(n8))
cnt=0
for l in o8:
    if l.strip() and norm(l) not in n8n:
        cnt+=1
        if cnt<40: print('S8 line changed:',l[:220])
print('s8 lines changed',cnt, 'of', sum(1 for l in o8 if l.strip()))
