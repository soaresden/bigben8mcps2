# genere l'icone 3D (cube "XMEM2") et l'icon.sys du dossier d'archives
from PIL import Image, ImageDraw, ImageFont
import struct
# --- texture 128x128 : degrade violet + texte
tex=Image.new('RGB',(128,128))
d=ImageDraw.Draw(tex)
for y in range(128):
    c=(int(90+60*y/127),int(30+20*y/127),int(150+80*y/127))
    d.line([(0,y),(127,y)],fill=c)
d.rectangle([3,3,124,124],outline=(255,210,60),width=4)
F=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',44)
F2=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',22)
d.text((64,52),'X',font=F,fill=(255,150,30),anchor='mm')
d.text((64,95),'MEM2',font=F2,fill=(255,230,120),anchor='mm')

px=tex.load()
texd=b''.join(struct.pack('<H',0x8000|((px[x,y][2]>>3)<<10)|((px[x,y][1]>>3)<<5)|(px[x,y][0]>>3)) for y in range(128) for x in range(128))
# --- cube : centre (0,-2.4,0), demi-cote 1.3 ; y negatif = haut
h=1.3; cy=-2.4
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
verts=[]
for n in [(0,0,-1),(0,0,1),(1,0,0),(-1,0,0),(0,-1,0),(0,1,0)]:
    dn=(0,1,0) if n[1]==0 else (0,0,1)
    r=cross(n,dn)
    def P(u,v):
        return tuple(n[i]*h+(u-0.5)*2*h*r[i]+(v-0.5)*2*h*dn[i]+(cy if i==1 else 0) for i in range(3))
    q=[(0,0),(1,0),(1,1),(0,1)]
    for tri in ((0,1,2),(0,2,3)):
        for k in tri:
            u,v=q[k]; verts.append((P(u,v),n,(u,v)))
def fx(v): return max(-32768,min(32767,int(round(v*4096))))
out=struct.pack('<IIIfI',0x010000,1,0x07,1.0,len(verts))
for (x,y,z),(nx,ny,nz),(u,v) in verts:
    out+=struct.pack('<4h',fx(x),fx(y),fx(z),0)
    out+=struct.pack('<4h',fx(nx),fx(ny),fx(nz),0)
    out+=struct.pack('<2h',fx(u),fx(v))
    out+=bytes([0x80,0x80,0x80,0x80])
out+=struct.pack('<IIfII',1,1,1.0,0,1)
out+=struct.pack('<II',0,1)+struct.pack('<ff',1.0,1.0)
out+=texd
ico=out
# --- icon.sys
def sj(s):
    o=b''
    for ch in s:
        if ch==' ': c=0x8140
        elif '0'<=ch<='9': c=0x824F+ord(ch)-48
        elif 'A'<=ch<='Z': c=0x8260+ord(ch)-65
        elif 'a'<=ch<='z': c=0x8281+ord(ch)-97
        elif ch=='-': c=0x817C
        else: c=0x8148
        o+=struct.pack('>H',c)
    return o
l1=sj('Archives XMEM2'); l2=sj('Soaresden')
title=(l1+l2).ljust(68,b'\0')
isys=bytearray(964)
isys[0:4]=b'PS2D'
struct.pack_into('<HHII',isys,4,0,len(l1),0,0x40)
bg=[(0x60,0x20,0x90,0),(0x60,0x20,0x90,0),(0x20,0x10,0x40,0),(0x20,0x10,0x40,0)]
for i,c in enumerate(bg): struct.pack_into('<4i',isys,0x10+i*16,*c)
ld=[(0.5,0.5,0.5,0),(0.0,-0.4,-0.1,0),(-0.5,-0.5,0.5,0)]
for i,v in enumerate(ld): struct.pack_into('<4f',isys,0x50+i*16,*v)
lc=[(0.3,0.3,0.3,0),(0.4,0.4,0.4,0),(0.5,0.5,0.5,0)]
for i,v in enumerate(lc): struct.pack_into('<4f',isys,0x80+i*16,*v)
struct.pack_into('<4f',isys,0xB0,0.5,0.5,0.5,0)
isys[0xC0:0xC0+68]=title
for off in (0x104,0x144,0x184): isys[off:off+9]=b'xmem2.ico'
def carr(name,b):
    return 'const unsigned char %s[%d] __attribute__((aligned(64)))={\n'%(name,len(b))+',\n'.join(','.join('0x%02x'%x for x in b[i:i+24]) for i in range(0,len(b),24))+'};\nconst unsigned int size_%s=%d;\n'%(name,len(b))
open(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','src','folder_icon.c'),'w').write('/* icone du dossier d archives XMEM2 (genere) */\n'+carr('xm_folder_ico',ico)+carr('xm_folder_iconsys',bytes(isys)))

print(len(ico),len(isys))
