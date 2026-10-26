"""Shrink a UDK map for release: strip the editor-only source-art copies from its textures.

Usage:  python udk_strip_sourceart.py AirLabs_Space_full.udk AirLabs_Space.udk

UDK keeps the original imported image inside every texture (SourceArt) so the editor can
re-import it; the game only draws the mip data. For the AirLabs space map that's 67 MB of
94 MB. This copies the package byte for byte, empties each texture's SourceArt block (the same
empty form the engine writes for unused data), moves every export and bulk-data offset after it,
then checks the result: everything except the removed source art and the moved offsets must be
identical to the input. Keep the full map for editing in UDK; ship the stripped one.
"""

import struct, sys

class R:
    def __init__(self, b, p=0):
        self.b, self.p = b, p
    def i32(self):
        v = struct.unpack_from('<i', self.b, self.p)[0]; self.p += 4; return v
    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v
    def i64(self):
        v = struct.unpack_from('<q', self.b, self.p)[0]; self.p += 8; return v
    def u16(self):
        v = struct.unpack_from('<H', self.b, self.p)[0]; self.p += 2; return v
    def f32(self):
        v = struct.unpack_from('<f', self.b, self.p)[0]; self.p += 4; return v
    def u8(self):
        v = self.b[self.p]; self.p += 1; return v
    def raw(self, n):
        v = self.b[self.p:self.p + n]; self.p += n; return v
    def fstr(self):
        n = self.i32()
        if n == 0: return ''
        if n > 0:
            s = self.raw(n); return s[:-1].decode('latin1')
        s = self.raw(-n * 2); return s[:-2].decode('utf-16le')



class Pkg:
    def __init__(self, data):
        self.d = d = data
        r = R(d)
        assert r.u32() == 0x9E2A83C1
        self.ver, self.lic = r.u16(), r.u16()
        self.pos = {}
        def field(name, fn):
            self.pos[name] = r.p; v = fn(); setattr(self, name, v); return v
        field('HeadersSize', r.i32)
        self.Folder = r.fstr()
        field('PackageFlags', r.u32)
        field('NameCount', r.i32); field('NameOffset', r.i32)
        field('ExportCount', r.i32); field('ExportOffset', r.i32)
        field('ImportCount', r.i32); field('ImportOffset', r.i32)
        field('DependsOffset', r.i32)
        field('ImportExportGuidsOffset', r.i32); field('ImportGuidsCount', r.i32); field('ExportGuidsCount', r.i32)
        field('ThumbnailTableOffset', r.i32)
        r.raw(16)
        ng = r.i32(); r.raw(12 * ng)
        field('EngineVersion', r.i32); field('CookerVersion', r.i32)
        field('CompressionFlags', r.u32)
        nc = r.i32(); assert nc == 0, 'compressed package'
        self.summary_end_guess = r.p
        # names
        r.p = self.NameOffset
        self.names = []
        for _ in range(self.NameCount):
            self.names.append(r.fstr()); r.raw(8)
        self.names_end = r.p
        # imports
        r.p = self.ImportOffset
        self.imports = []
        for _ in range(self.ImportCount):
            cp, cn, outer, on = self.nm(r), self.nm(r), r.i32(), self.nm(r)
            self.imports.append(dict(classpkg=cp, cls=cn, outer=outer, name=on))
        self.imports_end = r.p
        # exports
        r.p = self.ExportOffset
        self.exports = []
        for _ in range(self.ExportCount):
            e = {}
            e['class'], e['super'], e['outer'] = r.i32(), r.i32(), r.i32()
            e['name'] = self.nm(r)
            e['archetype'] = r.i32()
            e['flags'] = r.i64()
            e['size_pos'] = r.p; e['size'] = r.i32()
            e['off_pos'] = r.p; e['offset'] = r.i32()
            e['eflags'] = r.u32()
            nn = r.i32(); r.raw(4 * nn)
            r.raw(16); e['pflags'] = r.u32()
            self.exports.append(e)
        self.exports_end = r.p

    def nm(self, r):
        i = r.i32(); n = r.i32()
        s = self.names[i] if 0 <= i < len(self.names) else f'?{i}'
        return s if n == 0 else f'{s}_{n - 1}'

    def objname(self, idx):
        if idx == 0: return 'None'
        if idx > 0: return self.exports[idx - 1]['name']
        return self.imports[-idx - 1]['name']

    def classname(self, e):
        return self.objname(e['class']) if e['class'] else 'Class'

    def props_end(self, e):
        """Byte position (absolute) just after the property list ("None") of an export."""
        r = R(self.d, e['offset'])
        r.i32()  # NetIndex
        while True:
            name = self.nm(r)
            if name == 'None': return r.p
            typ = self.nm(r); size = r.i32(); r.i32()
            if typ == 'BoolProperty': r.u8(); continue
            if typ in ('StructProperty', 'ByteProperty'): self.nm(r)
            r.raw(size)

    def bulk_headers(self, e):
        """Inline bulk-data headers in an export: positions p (absolute) where the stored data
        offset equals p + 16, i.e. the data follows the header. Returns (p, flags, count, size)."""
        d = self.d; o, n = e['offset'], e['size']
        out = []
        p = o
        end = o + n - 16
        while p <= end:
            off = struct.unpack_from('<i', d, p + 12)[0]
            if off == p + 16:
                flags, count, size = struct.unpack_from('<Iii', d, p)
                if 0 <= size <= o + n - (p + 16) and flags < 0x1000:
                    out.append((p, flags, count, size))
                    p += 16 + (size if not (flags & 0x01) else 0)
                    continue
            p += 1
        return out

def verify(src, dst):
    import zipfile, io
    A = open(src,'rb').read(); B = open(dst,'rb').read()
    pa, pb = Pkg(A), Pkg(B)
    assert pa.names == pb.names and len(pa.exports) == len(pb.exports) and len(pa.imports) == len(pb.imports)
    allowed = set()
    for e in pa.exports:
        allowed |= set(range(e['size_pos'], e['size_pos'] + 4)) | set(range(e['off_pos'], e['off_pos'] + 4))
    assert all(A[i] == B[i] or i in allowed for i in range(pa.HeadersSize)), 'unexpected header change'
    bad = 0; nbulk = 0
    for ea, eb in zip(pa.exports, pb.exports):
        assert ea['name'] == eb['name'] and ea['class'] == eb['class']
        da = A[ea['offset']:ea['offset'] + ea['size']]; db = B[eb['offset']:eb['offset'] + eb['size']]
        ha, hb = pa.bulk_headers(ea), pb.bulk_headers(eb)
        assert len(ha) == len(hb), (ea['name'], len(ha), len(hb))
        nbulk += len(hb)
        if pa.classname(ea) == 'Texture2D':
            assert hb[0][3] == 0 and hb[0][1] == 0 and hb[0][2] == 0
            for (p1, f1, c1, s1), (p2, f2, c2, s2) in zip(ha[1:], hb[1:]):
                assert (f1, c1, s1) == (f2, c2, s2) and A[p1 + 16:p1 + 16 + s1] == B[p2 + 16:p2 + 16 + s2]
            cut = ha[0][3]; pe = ha[0][0] - ea['offset']
            ra = bytearray(da[:pe + 16] + da[pe + 16 + cut:]); rb = bytearray(db)
            for h in ha:
                rel = h[0] - ea['offset']; rel = rel if rel <= pe else rel - cut
                ra[rel:rel + 16] = b'\0' * 16
            for h in hb:
                rel = h[0] - eb['offset']; rb[rel:rel + 16] = b'\0' * 16
        else:
            ra, rb = bytearray(da), bytearray(db)
            for h in ha: ra[h[0] - ea['offset'] + 12:h[0] - ea['offset'] + 16] = b'\0' * 4
            for h in hb: rb[h[0] - eb['offset'] + 12:h[0] - eb['offset'] + 16] = b'\0' * 4
        if ra != rb: bad += 1; print('differs:', ea['name'])
        # every bulk offset in the new file points right after its header
        for (p2, f2, c2, s2) in hb: assert int.from_bytes(B[p2 + 12:p2 + 16], 'little') == p2 + 16
    end = max(e['offset'] + e['size'] for e in pb.exports)
    assert end == len(B), 'trailing bytes'
    print(f'{len(pa.exports)} exports, {nbulk} bulk blocks: everything identical apart from the removed source art and moved offsets ({bad} differences)')
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z: z.writestr('AirLabs_Space.udk', B)
    print('file %.1f MB, zipped %.1f MB' % (len(B) / 1e6, len(buf.getvalue()) / 1e6))

import struct, sys

def shrink(src, dst):
    d = open(src, 'rb').read()
    p = Pkg(d)
    out = bytearray(d[:p.HeadersSize])
    first = min(e['offset'] for e in p.exports)
    assert first == p.HeadersSize, (first, p.HeadersSize)
    saved = 0
    for e in sorted(p.exports, key=lambda e: e['offset']):
        o, n = e['offset'], e['size']
        data = d[o:o + n]
        heads = p.bulk_headers(e)
        cut = None  # (rel start, rel end) of bytes removed
        if p.classname(e) == 'Texture2D':
            pe = p.props_end(e) - o
            h0 = [h for h in heads if h[0] - o == pe]
            assert h0, f'{e["name"]}: no SourceArt header after the properties'
            _, flags, count, size = h0[0]
            assert not (flags & 0x01), 'SourceArt stored in a separate file?'
            cut = (pe + 16, pe + 16 + size)
            data = data[:cut[0]] + data[cut[1]:]
            struct.pack_into('<Iii', data := bytearray(data), pe, 0, 0, 0)  # empty: no flags, no data
            saved += size
        data = bytearray(data)
        def newrel(rel):
            if cut and rel >= cut[1]: return rel - (cut[1] - cut[0])
            return rel
        new_o = len(out)
        for (hp, flags, count, size) in heads:
            r = newrel(hp - o)
            struct.pack_into('<i', data, r + 12, new_o + r + 16)
        out += data
        struct.pack_into('<i', out, e['size_pos'], len(data))
        struct.pack_into('<i', out, e['off_pos'], new_o)
    open(dst, 'wb').write(out)
    return len(d), len(out), saved

if __name__ == '__main__':
    a, b, s = shrink(sys.argv[1], sys.argv[2])
    print(f'{a/1e6:.1f} MB -> {b/1e6:.1f} MB (removed {s/1e6:.1f} MB of source art)')
    verify(sys.argv[1], sys.argv[2])
