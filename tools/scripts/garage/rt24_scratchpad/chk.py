import re,difflib
old=open('docs/RT-SERIES-2.md',encoding='utf8').read().split('\n')
new=open('docs/RT-ROADMAP.md',encoding='utf8').read().split('\n')
newtxt='\n'.join(new)
def norm(s): return re.sub(r'\s+',' ',s).strip()
newn=norm(newtxt)
# 1 headings
oh=[l for l in old if l.startswith('### RT2-')]
for h in oh:
    iid=h.split(' ')[1]
    if not any(l.startswith('### '+iid+' ') for l in new): print('MISSING item',iid)
# 2 blockquotes
bq=[i for i,l in enumerate(old) if l.startswith('>')]
print('old blockquote lines',len(bq),'new',sum(1 for l in new if l.startswith('>')))
miss=0
for i in bq:
    t=norm(old[i].lstrip('> '))
    if len(t)<5: continue
    if t not in newn:
        miss+=1
        # fuzzy: find best new line
        best=max(((difflib.SequenceMatcher(None,t[:300],norm(l)[:300]).quick_ratio(),j) for j,l in enumerate(new) if l.startswith('>')),default=(0,0))
        print('BQ not verbatim old',i+1,'best ratio %.2f new line %d'%best, t[:120])
print('bq missing verbatim',miss)
