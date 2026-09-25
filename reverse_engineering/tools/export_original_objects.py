"""Export initial map objects and tile scenery using original DOS sprite code."""
import json
import struct
from PIL import Image
from gunboat_formats import ROOT, read_datac, asset_path
from export_original_world import decode_tiles
from probe_original_sprites import render

OUT=ROOT/'reverse_engineering/out/world_export'


def placements(raw, tiles, tile_bytes):
    base=struct.unpack_from('<H',raw,0x10d)[0]
    assert 0 < base < 1000
    objects=[]
    for i in range(base):
        word=struct.unpack_from('<H',raw,273+2*i)[0]
        kind=word&255
        if kind==0:
            continue
        assert kind<64
        x=struct.unpack_from('<H',raw,2273+2*i)[0]
        y=struct.unpack_from('<H',raw,4273+2*i)[0]
        objects.append(dict(kind=kind,x=x,y=y,heading=(word>>8)&7,
                            source='map',index=i,offset=273+2*i,flags=word>>8))
    for cell_index,cell in enumerate(raw[2:189]):
        tile=tiles[cell&63]
        start=tile['offset']+4+len(tile['vertices'])*4
        row,col=divmod(cell_index,17)
        rotation=cell>>6
        for i in range(tile['objectCount']):
            kind,x,y,unused=tile_bytes[start+i*4:start+i*4+4]
            rx,ry=[(x,y),(y,128-x),(128-x,128-y),(128-y,x)][rotation]
            objects.append(dict(kind=kind,x=col*1024+(rx&255)*8,
                y=(10-row)*1024+(ry&255)*8,heading=(start+i*4)&7,source='tile',
                index=i,cell=cell_index,offset=start+i*4,extra=unused))
    return objects,base


def main():
    records=read_datac()
    tile_bytes=asset_path(records[83]).read_bytes()
    tiles=decode_tiles(tile_bytes)
    summaries=[]
    for world,bank in enumerate([77,79,81,75],1):
        path=OUT/f'original-map-{world}.json'
        data=json.loads(path.read_text())
        objects,base=placements(asset_path(records[69+world]).read_bytes(),tiles,tile_bytes)
        # 0xEE01 skips kind 57 explicitly. Preserve those records for inspection.
        kinds=sorted({o['kind'] for o in objects if o['kind']!=57})
        atlas=Image.new('RGBA',(256*8,64*len(kinds)))
        palette=data['dosPalette']
        visible={}
        for row,kind in enumerate(kinds):
            count=0
            for view in range(8):
                pixels=render(kind,bank=bank,angle=view*32,screen=True)
                assert max(pixels)<len(palette)
                indexed=Image.frombytes('P',(320,200),pixels).crop((40,64,296,128))
                vals=list(indexed.getdata())
                count+=sum(v!=0 for v in vals)
                rgba=Image.new('RGBA',(256,64))
                rgba.putdata([tuple(palette[v])+(255 if v else 0,) for v in vals])
                atlas.paste(rgba,(view*256,row*64))
            visible[kind]=dict(row=row,opaquePixels=count)
        atlas_name=f'objects-{world}.png'
        atlas.save(OUT/atlas_name)
        for obj in objects:
            frame=visible.get(obj['kind'])
            obj['atlasRow']=frame['row'] if frame and frame['opaquePixels'] else -1
        data['objects']=objects
        data['objectAtlas']=dict(texture=atlas_name,rows=len(kinds),views=8,
            cellWidth=256,cellHeight=64,anchorX=120,anchorY=8,
            sourceRecords=[bank,bank+1],kinds=visible)
        data['objectNotes']='Initial map placements and tile scenery. Runtime spawning/damage are not simulated. Billboard size and eight-direction view selection are provisional.'
        data['notes']='Original terrain, object coordinates and daytime VGA colors. Height and sprite display scale remain provisional.'
        path.write_text(json.dumps(data,separators=(',',':')))
        summary=dict(world=world,mapObjectLimit=base,placements=len(objects),
            rendered=sum(o['atlasRow']>=0 for o in objects),sourceRecords=[bank,bank+1],
            blankKinds=[k for k,v in visible.items() if not v['opaquePixels']],
            explicitlyHiddenKind=57)
        summaries.append(summary)
        print(summary,flush=True)
    (ROOT/'reverse_engineering/out/object-export-report.json').write_text(json.dumps(summaries,indent=2))


if __name__=='__main__':
    main()
