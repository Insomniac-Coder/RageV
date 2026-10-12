import pymupdf,sys
d=pymupdf.open(sys.argv[1]); t=''.join(p.get_text() for p in d)
open(sys.argv[2],'w',encoding='utf-8').write(t); print(len(d),'pages',len(t),'chars')
