#!/usr/bin/env python3
"""Generate original Muse shell artwork with vector-like primitives."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import math
root=Path(__file__).resolve().parents[1];out=root/'build/native/muse/sce_sys';out.mkdir(parents=True,exist_ok=True)
for name,w,h in [('icon0.png',512,512),('pic1.png',1920,1080)]:
    image=Image.new('RGB',(w,h));pixels=image.load()
    for y in range(h):
        for x in range(w):
            light=math.exp(-(((x/w)-.5)**2+((y/h)-.4)**2)*4)
            pixels[x,y]=(int(4+8*light),int(12+28*light),int(23+30*light))
    d=ImageDraw.Draw(image);size=280 if w==512 else 300;cx=w/2;cy=h/2-10;r=size/5
    d.rounded_rectangle((cx-size/2,cy-size/2,cx+size/2,cy+size/2),radius=r,fill='#68f2cd')
    pts=[(cx-size*.25,cy+size*.23),(cx-size*.25,cy-size*.23),(cx,cy+size*.03),(cx+size*.25,cy-size*.23),(cx+size*.25,cy+size*.23)]
    d.line(pts,fill='#12363c',width=int(size*.055),joint='curve')
    image.save(out/name)
print(out)
