# dexmethods.py <classes.dex>: every method, tab-separated: class, name, signature, static/native. MIT.
import struct,sys
d=open(sys.argv[1],'rb').read()
def uleb(o):
    r=s=0
    while True:
        b=d[o];o+=1;r|=(b&0x7f)<<s;s+=7
        if b<0x80:return r,o
(sis,sio,tis,tio,pis,pio,fis,fio,mis,mio,cds,cdo)=struct.unpack_from('<12I',d,0x38)
def S(i):
    off=struct.unpack_from('<I',d,sio+4*i)[0];_,o=uleb(off);e=d.index(b'\0',o);return d[o:e].decode('utf8','replace')
def T(i):return S(struct.unpack_from('<I',d,tio+4*i)[0])
def P(i):
    sh,ret,par=struct.unpack_from('<3I',d,pio+12*i);ps=''
    if par:
        n=struct.unpack_from('<I',d,par)[0];ps=''.join(T(struct.unpack_from('<H',d,par+4+2*k)[0]) for k in range(n))
    return '(%s)%s'%(ps,T(ret))
for c in range(cds):
    cidx,fl,sup,ifs,src,ann,cdata,sv=struct.unpack_from('<8I',d,cdo+32*c)
    if not cdata:continue
    cn=T(cidx)[1:-1]
    o=cdata;sf,o=uleb(o);inf,o=uleb(o);dm,o=uleb(o);vm,o=uleb(o)
    for _ in range(sf+inf):_,o=uleb(o);_,o=uleb(o)
    idx=0
    for k in range(dm+vm):
        if k==dm:idx=0
        diff,o=uleb(o);acc,o=uleb(o);co,o=uleb(o);idx+=diff
        cl,pr,nm=struct.unpack_from('<HHI',d,mio+8*idx)
        print('%s\t%s\t%s\t%s'%(cn,S(nm),P(pr),('static' if acc&8 else 'inst')+(' native' if acc&0x100 else '')))
