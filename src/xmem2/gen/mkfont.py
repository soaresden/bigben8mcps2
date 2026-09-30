from PIL import Image, ImageDraw, ImageFont
import struct
F=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
CW,CH=16,20
COLS=32; AW,AH=512,256
img=Image.new('L',(AW,AH),0); d=ImageDraw.Draw(img)
widths=[]
for i,c in enumerate(range(32,256)):
    ch=chr(c) if c<127 or c>=160 else '?'
    x=(i%COLS)*CW; y=(i//COLS)*CH
    w=int(round(F.getlength(ch)))
    d.text((x,y),ch,font=F,fill=255)
    widths.append(min(max(w,1),CW))
out=['/* atlas police DejaVu Sans 15px, genere */','#include <stdint.h>','const int font_cw=%d, font_ch=%d, font_cols=%d, font_aw=%d, font_ah=%d;'%(CW,CH,COLS,AW,AH)]
out.append('const uint8_t font_w[224]={'+','.join(map(str,widths))+'};')
px=img.load()
vals=[]
for y in range(AH):
    for x in range(AW):
        a=px[x,y]*128//255
        vals.append('0x%08x'%((a<<24)|0xffffff))
out.append('uint32_t font_atlas[%d]'%(AW*AH)+' __attribute__((aligned(128)))={')
for i in range(0,len(vals),16): out.append(','.join(vals[i:i+16])+',')
out.append('};')
open(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','src','font_data.c'),'w').write('\n'.join(out)+'\n')

