#!/usr/bin/env python3
"""Minimal DEX reader: lists methods of classes matching a prefix,
with access flags (native marked) and their full descriptors."""
import struct, sys

def uleb(d, o):
    r = s = 0
    while True:
        b = d[o]; o += 1
        r |= (b & 0x7f) << s; s += 7
        if b < 0x80: return r, o

def main(path, prefix, only_native):
    d = open(path, 'rb').read()
    h = struct.unpack_from('<8s4I20s12I8I', d, 0)
    (string_ids_size, string_ids_off, type_ids_size, type_ids_off,
     proto_ids_size, proto_ids_off, field_ids_size, field_ids_off,
     method_ids_size, method_ids_off, class_defs_size, class_defs_off) = \
        struct.unpack_from('<12I', d, 0x38)
    def string(i):
        off = struct.unpack_from('<I', d, string_ids_off + 4 * i)[0]
        _, o = uleb(d, off)
        e = d.index(b'\0', o)
        return d[o:e].decode('utf-8', 'replace')
    def typ(i):
        return string(struct.unpack_from('<I', d, type_ids_off + 4 * i)[0])
    def proto(i):
        shorty, ret, params = struct.unpack_from('<3I', d, proto_ids_off + 12 * i)
        ps = ''
        if params:
            n = struct.unpack_from('<I', d, params)[0]
            ps = ''.join(typ(struct.unpack_from('<H', d, params + 4 + 2 * k)[0]) for k in range(n))
        return '(%s)%s' % (ps, typ(ret))
    def method(i):
        cls, pr, name = struct.unpack_from('<HHI', d, method_ids_off + 8 * i)
        return typ(cls), string(name), proto(pr)
    for c in range(class_defs_size):
        cidx, flags, sup, ifs, src, ann, cdata, sv = struct.unpack_from('<8I', d, class_defs_off + 32 * c)
        cname = typ(cidx)
        if not cname.startswith(prefix) or not cdata:
            continue
        o = cdata
        sf, o = uleb(d, o); inf, o = uleb(d, o); dm, o = uleb(d, o); vm, o = uleb(d, o)
        for _ in range(sf + inf):
            _, o = uleb(d, o); _, o = uleb(d, o)
        idx = 0
        for k in range(dm + vm):
            if k == dm: idx = 0
            diff, o = uleb(d, o); acc, o = uleb(d, o); _, o = uleb(d, o)
            idx += diff
            native = bool(acc & 0x100)
            if only_native and not native: continue
            cl, nm, sg = method(idx)
            st = 'static ' if acc & 0x8 else ''
            print('%s%s%s %s.%s%s' % ('native ' if native else '', st, '', cl[1:-1], nm, sg))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], len(sys.argv) > 3 and sys.argv[3] == 'native')
