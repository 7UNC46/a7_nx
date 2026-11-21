"""dexflow.py dex 'Class;' [method] : per method, the sequence of const-strings, consts, field accesses and invokes."""
import struct,sys
src=open(__import__('os').path.join(__import__('os').path.dirname(__file__),'dexcalls.py')).read().split("out=[]")[0]
src=src.replace("d=open(sys.argv[1],'rb').read(); pat=re.compile(sys.argv[2])","d=open(sys.argv[1],'rb').read()")
exec(src)
def F(i):
    c,t,n=struct.unpack_from('<HHI',d,fio+8*i);return '%s.%s:%s'%(T(c)[1:-1].split('/')[-1],S(n),T(t))
want=sys.argv[2]; meth=sys.argv[3] if len(sys.argv)>3 else None
for c in range(cds):
    cidx,fl,sup,ifs,srcf,ann,cdata,sv=struct.unpack_from('<8I',d,cdo+32*c)
    if T(cidx)!=want or not cdata:continue
    o=cdata;sf,o=uleb(o);inf,o=uleb(o);dm,o=uleb(o);vm,o=uleb(o)
    for _ in range(sf+inf):_,o=uleb(o);_,o=uleb(o)
    idx=0
    for k in range(dm+vm):
        if k==dm:idx=0
        diff,o=uleb(o);acc,o=uleb(o);co,o=uleb(o);idx+=diff
        name=M(idx)
        if meth and name.split('.')[-1].split('(')[0]!=meth:continue
        if not co:continue
        print('==',name)
        ins=struct.unpack_from('<I',d,co+12)[0];base=co+16;pc=0
        while pc<ins:
            u=struct.unpack_from('<H',d,base+2*pc)[0];op=u&0xff
            if op==0 and u in(0x100,0x200,0x300):
                if u==0x100:n=struct.unpack_from('<H',d,base+2*pc+2)[0];pc+=n*2+4;continue
                if u==0x200:n=struct.unpack_from('<H',d,base+2*pc+2)[0];pc+=n*4+2;continue
                w,n=struct.unpack_from('<HI',d,base+2*pc+2);pc+=(n*w+1)//2+4;continue
            a=struct.unpack_from('<H',d,base+2*pc+2)[0] if W[op]>1 else 0
            if op==0x1a: print('   str ',repr(S(a)))
            elif op==0x1b: print('   str ',repr(S(struct.unpack_from('<I',d,base+2*pc+2)[0])))
            elif op in (0x12,): print('   const/4 v%d,%d'%((u>>8)&0xf, ((u>>12)^8)-8))
            elif op in (0x13,): print('   const/16',struct.unpack_from('<h',d,base+2*pc+2)[0])
            elif op in INV: print('   call',M(a))
            elif 0x60<=op<=0x6d: print('   %s'%('sget' if op<0x67 else 'sput'),F(a))
            elif 0x52<=op<=0x5f: print('   %s'%('iget' if op<0x59 else 'iput'),F(a))
            elif op==0x22: print('   new',T(a))
            elif op in (0x38,0x39,0x32,0x33,0x34,0x35,0x36,0x37,0x3a,0x3b,0x3c,0x3d,0x28,0x29,0x2a): print('   br')
            pc+=W[op]
