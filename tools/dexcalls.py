# dexcalls.py <classes.dex> <regex>: who calls the methods matching regex (a Dalvik bytecode walk). MIT.
"""dexcalls.py <classes.dex> <calleeregex> : list callers of matching methods (by decoding Dalvik bytecode)."""
import struct,sys,re
d=open(sys.argv[1],'rb').read(); pat=re.compile(sys.argv[2])
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
def M(i):
    c,p,n=struct.unpack_from('<HHI',d,mio+8*i);return '%s.%s%s'%(T(c)[1:-1],S(n),P(p))
# instruction width in code units per opcode
W=[1]*256
def setw(r,w):
    for x in r:W[x]=w
setw([0x00,0x01,0x04,0x07,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x1d,0x1e,0x21,0x27,0x28],1)
setw(range(0xb0,0xd0),1); setw(range(0x7b,0x90),1)
setw([0x02,0x05,0x08,0x13,0x15,0x16,0x19,0x1a,0x1c,0x1f,0x20,0x22,0x23,0x29],2)
setw(range(0x2d,0x32),2); setw(range(0x32,0x3e),2); setw(range(0x44,0x6e),2); setw(range(0x90,0xb0),2); setw(range(0xd0,0xe3),2)
setw([0x03,0x06,0x09,0x14,0x17,0x1b,0x24,0x25,0x26,0x2a,0x2b,0x2c],3); setw(range(0x6e,0x73),3); setw(range(0x74,0x79),3)
W[0x18]=5; W[0xfa]=4;W[0xfb]=4;W[0xfc]=3;W[0xfd]=3;W[0xfe]=2;W[0xff]=2
INV=set(range(0x6e,0x73))|set(range(0x74,0x79))|{0xfa,0xfb}
out=[]
for c in range(cds):
    cidx,fl,sup,ifs,src,ann,cdata,sv=struct.unpack_from('<8I',d,cdo+32*c)
    if not cdata:continue
    o=cdata;sf,o=uleb(o);inf,o=uleb(o);dm,o=uleb(o);vm,o=uleb(o)
    for _ in range(sf+inf):_,o=uleb(o);_,o=uleb(o)
    idx=0
    for k in range(dm+vm):
        if k==dm:idx=0
        diff,o=uleb(o);acc,o=uleb(o);co,o=uleb(o);idx+=diff
        if not co:continue
        ins=struct.unpack_from('<I',d,co+12)[0];base=co+16;pc=0
        caller=None
        while pc<ins:
            u=struct.unpack_from('<H',d,base+2*pc)[0];op=u&0xff
            if op==0 and u!=0:  # payloads
                if u==0x100: n=struct.unpack_from('<H',d,base+2*pc+2)[0];pc+=n*2+4;continue
                if u==0x200: n=struct.unpack_from('<H',d,base+2*pc+2)[0];pc+=n*4+2;continue
                if u==0x300: w,n=struct.unpack_from('<HI',d,base+2*pc+2);pc+=(n*w+1)//2+4;continue
            if op in INV:
                mi=struct.unpack_from('<H',d,base+2*pc+2)[0]
                callee=M(mi)
                if pat.search(callee):
                    if caller is None:caller=M(idx)
                    out.append((caller,callee))
            pc+=W[op]
for a,b in out:print(a,' -> ',b)
