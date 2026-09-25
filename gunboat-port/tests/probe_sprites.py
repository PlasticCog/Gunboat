"""Direct decoder prototype, compared with the original x86 execution oracle."""
import sys, struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'reverse_engineering/tools'))
from gunboat_formats import read_datac,asset_path,UNPACKED_IMAGE
from probe_original_sprites import render

exe=UNPACKED_IMAGE.read_bytes()
def native(kind,bank,angle):
    records=read_datac();a=asset_path(records[bank]).read_bytes();b=asset_path(records[bank+1]).read_bytes();b=b.ljust(0x1aac,b'\0')+a
    scale=46 if kind in (57,58,59) else 47
    masks=exe[0xeb3f+(47-scale)*10:0xeb3f+(47-scale)*10+10]
    facing=exe[0x28fd7:0x28fd7+16]
    view=((angle+2)&255)>>2
    def hbit(i):return (masks[(i//8)%7]>>(7-i%8))&1
    def maskbit(m,i):return (m>>(7-i%8))&1
    def part(offset,depth):
        w,h,ptr,lens,extra=(*a[offset:offset+2],*struct.unpack_from('<HHH',a,offset+2))
        rows=[]
        for row in range(h):
            src=struct.unpack_from('<H',a,ptr+row*2)[0];cl,dl=a[lens+row*2:lens+row*2+2]
            pixels=[]
            def emit(c,i):pixels.extend([c]*(1+hbit(i)))
            if cl&0xc0:
                n=cl&(127 if cl&128 else 63);pad=(w-n)//2
                phase=w*view//32 if cl&128 else 0
                src+=n*view//32 if cl&128 else 0
                for i in range(pad):emit(0,phase+i)
                for i in range(n):emit(b[src+i],phase+pad+i)
                for i in range(pad):emit(0,phase+pad+n+i)
            else:
                turn=(view+1)//2;width=w;dep=depth
                if turn&16:src+=cl+dl
                if turn&8:src+=cl;cl,dl=dl,cl;width,dep=dep,width
                m0,m1=facing[(turn&7)*2:(turn&7)*2+2]
                missing=sum((1+hbit(i))*maskbit(m0,i) for i in range(width-cl))
                missing+=sum((1+hbit(width+i))*maskbit(m1,i) for i in range(dl,dep))
                pixels.extend([0]*((missing+1)//2))
                for i in range(cl):
                    if maskbit(m0,width-cl+i):emit(b[src+i],width-cl+i)
                for i in range(dl):
                    if maskbit(m1,i):emit(b[src+cl+i],width+i)
                pixels.extend([0]*(missing//2))
            repeat=1+((masks[9-row//8]>>(7-row%8))&1)
            rows.extend([pixels]*repeat)
        return rows
    desc=kind*8;main=part(desc,a[desc+6]);sub=struct.unpack_from('<H',a,6)[0]+a[desc+7]*8
    upper=part(sub,a[desc+6]) if a[sub] else []
    # Main bottom baseline is y=120 in the original VGA fixture.
    output=bytearray(320*200)
    def draw(rows,bottom,shift=0):
        if not rows:return
        width=len(rows[-1]);left=2*((72-((width//2+shift)//2)+8)&255)
        for y,row in enumerate(rows):
            for x,c in enumerate(row):
                yy=bottom-len(rows)+y
                if 40<=left+x<296 and 0<=yy<200 and c:output[yy*320+left+x]=c
    draw(main,120)
    if upper:
        ratio=(a[sub+6]*256//a[sub])&255
        shift=(len(upper[-1])*ratio//2)>>8
        draw(upper,120-len(main),shift)
    return bytes(output)

if __name__=='__main__':
    errors=[];cases=0
    for bank in (77,79,81,75):
        d=__import__('json').load(open(Path(__file__).resolve().parents[2]/f'reverse_engineering/out/world_export/original-map-{[77,79,81,75].index(bank)+1}.json'))
        for kind in map(int,d['objectAtlas']['kinds']):
            for angle in range(0,256,32):
                got=native(kind,bank,angle);want=render(kind,bank=bank,scale=47,angle=angle,screen=True)
                n=sum(x!=y for x,y in zip(got,want))
                cases+=1
                if n:errors.append((bank,kind,angle,n))
    print('Cases',cases,'Mismatches',len(errors),'First',errors[:30])
