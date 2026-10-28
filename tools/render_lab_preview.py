# Rasterize the mesh/font atlas from tools/lab_preview_ui.cpp (requires NumPy and Pillow).
import json, sys
from pathlib import Path
import numpy as np
from PIL import Image

for name in sys.argv[1:]:
    prefix = Path(name)
    d = json.loads(prefix.with_suffix('.json').read_text())
    w, h = d['width'], d['height']
    atlas = np.frombuffer(prefix.with_suffix('.atlas').read_bytes(), dtype=np.uint8).reshape(d['th'], d['tw'])
    out = np.zeros((h, w, 3), dtype=np.float32)
    out[:] = [18, 22, 30]
    for dl in d['lists']:
        vertices = np.asarray(dl['v'], dtype=np.float64)
        indices = dl['i']
        for start, count, offset, cx0, cy0, cx1, cy1 in dl['commands']:
            for k in range(start, start + count, 3):
                tri = vertices[[indices[k+i] + offset for i in range(3)]]
                x0 = max(0, int(np.floor(tri[:,0].min())), int(cx0))
                x1 = min(w, int(np.ceil(tri[:,0].max())) + 1, int(cx1))
                y0 = max(0, int(np.floor(tri[:,1].min())), int(cy0))
                y1 = min(h, int(np.ceil(tri[:,1].max())) + 1, int(cy1))
                if x1 <= x0 or y1 <= y0: continue
                a,b,c = tri[:,:2]
                den = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
                if abs(den) < 1e-8: continue
                yy, xx = np.mgrid[y0:y1,x0:x1].astype(float); xx += .5; yy += .5
                wa = ((b[1]-c[1])*(xx-c[0]) + (c[0]-b[0])*(yy-c[1])) / den
                wb = ((c[1]-a[1])*(xx-c[0]) + (a[0]-c[0])*(yy-c[1])) / den
                wc = 1-wa-wb
                inside = (wa >= -1e-5) & (wb >= -1e-5) & (wc >= -1e-5)
                if not inside.any(): continue
                weight = np.stack([wa,wb,wc], axis=-1)
                uv = weight @ tri[:,2:4]
                tx = np.clip((uv[:,:,0]*d['tw']).astype(int), 0, d['tw']-1)
                ty = np.clip((uv[:,:,1]*d['th']).astype(int), 0, d['th']-1)
                colors = np.asarray([[int(v[4]) >> shift & 255 for shift in (0,8,16,24)] for v in tri], dtype=float)
                color = weight @ colors
                alpha = color[:,:,3] / 255 * atlas[ty,tx] / 255
                alpha *= inside
                dst = out[y0:y1,x0:x1]
                dst[:] = dst*(1-alpha[:,:,None]) + color[:,:,:3]*alpha[:,:,None]
    Image.fromarray(np.clip(out,0,255).astype(np.uint8)).save(prefix.with_suffix('.png'))
    print(prefix.with_suffix('.png'))


