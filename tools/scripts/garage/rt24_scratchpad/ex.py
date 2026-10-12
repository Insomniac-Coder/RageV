import pymupdf,sys
d=pymupdf.open(sys.argv[1])
open(sys.argv[2],'w',encoding='utf-8').write('\n'.join(f'--- p{i+1}\n'+p.get_text().strip() for i,p in enumerate(d)))
print(len(d))
