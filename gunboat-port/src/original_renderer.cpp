#include "original_renderer.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace gb::original {
namespace {
uint16_t neg(uint16_t v){return uint16_t(0-v);}
int16_t signed16(uint16_t v){return int16_t(v);}
uint16_t difference(uint16_t a,uint16_t b){return uint16_t(a-b);}
uint16_t slope(uint16_t edge,uint16_t apex,unsigned rows){
    auto d=difference(edge,apex);bool negative=signed16(d)<0;
    unsigned magnitude=negative?neg(d):d;
    if(rows)magnitude/=rows;
    return negative?neg(uint16_t(magnitude)):uint16_t(magnitude);
}
}
Renderer::Renderer(const Bytes& b){
    if(b.size()<0x28e79)throw std::runtime_error("Executable lacks original renderer tables");
    std::copy_n(b.begin()+0xc796,256,arctangent.begin());
    for(size_t i=0;i<cosine.size();i++)cosine[i]=word(b,0xc692+i*2);
    std::copy_n(b.begin()+0x1b730+0xd731,8,negateAngle.begin());
    std::copy_n(b.begin()+0x1b730+0xd741,8,quadrant.begin());
    std::copy_n(b.begin()+0x11556,30,visibility.begin());
    std::copy_n(b.begin()+0x11574,36,order.begin());
    std::copy_n(b.begin()+0x11529,45,neighbors.begin());
}
// GB image 106B3..107CE, including byte-level wraps at 1071F and 107C0.
ProjectedVertex Renderer::project(RenderVertex v,const Camera& c)const{
    auto dx=difference(v.x,c.x),dy=difference(v.y,c.y);unsigned octant=0;
    if(signed16(dx)<0){octant=3;dx=neg(dx);}
    if(signed16(dy)<0){dy=neg(dy);octant^=1;}
    if(dx>=dy){std::swap(dx,dy);octant^=4;}
    unsigned ratio=255;if(dx!=dy)ratio=dy?((uint32_t(dx)<<16)/dy)>>8:0;
    uint8_t angle=arctangent[ratio];uint8_t rotated=uint8_t((angle>>3)|(angle<<5));
    uint16_t a=uint16_t(uint16_t(rotated)*257)&0x1fe0;
    if(negateAngle[octant])a=neg(a);
    a=uint16_t((uint16_t(uint8_t((a>>8)+quadrant[octant]))<<8)|(a&255));
    a=difference(a,c.angle);
    unsigned reciprocal=cosine[(uint8_t(~(angle>>1))&254)/2]>>1;
    uint16_t scale=255;if((dy>>8)||(dy&255)>(reciprocal>>8))scale=uint16_t(reciprocal/dy);
    scale&=255;
    uint16_t row=uint16_t((0x200|scale)-((scale*(v.height&255))>>5));
    if(signed16(row)<0)row=0;
    row>>=3;row=uint16_t((row&0xff00)|uint8_t(row+c.horizon));++row;
    return {a,row,scale};
}
// Original VGA fill 10992..10A98. This retains signed angular wrap and the
// original inclusive-row/exclusive-column rounding, rather than floating edges.
void Renderer::trapezoid(ProjectedVertex a,ProjectedVertex b,uint16_t apex,
                         uint8_t first,uint8_t last,bool startsAtApex,uint8_t color){
    uint16_t cx=a.angle,dx=b.angle;
    if(signed16(difference(dx,cx))>=0)std::swap(dx,cx);
    unsigned count=uint8_t(last-first)+1;
    uint16_t left=dx,right=cx,dl=slope(dx,apex,count-1),dr=slope(cx,apex,count-1);
    if(startsAtApex){left=right=apex;dl=neg(dl);dr=neg(dr);}
    left=uint16_t(left+0x40);right=uint16_t(right+0xbf);
    for(unsigned row=0;row<count;row++){
        // SI is a wrapping 16-bit address in the original 320-byte scanline.
        uint16_t base=uint16_t(40+uint16_t(first*320)+row*320);
        if(base>=0xa000)break;
        if(base>=0x5000){
            int x=(left>>7)&511;int width=uint8_t(((right>>7)&511)-x);
            bool visible=true;
            if(x>=256){if(x<384)visible=false;else{x-=512;width+=x;if(width<0)visible=false;x=0;}}
            if(visible){
                if(x+width>=256)width=256-x;
                width=std::max(1,width);
                for(int k=0;k<width;k++){size_t p=size_t(base)+x+k;if(p<pixels.size())pixels[p]=color;}
            }
        }
        left=difference(left,dl);right=difference(right,dr);
    }
}
// Original primitive dispatcher 1080A..10991 (triangle commands 00h/80h).
void Renderer::triangle(std::array<ProjectedVertex,3> v,uint8_t color){
    if((v[0].angle&v[1].angle&v[2].angle)&0x8000)return;
    if(v[0].row>=v[1].row)std::swap(v[0],v[1]);
    if(v[1].row>=v[2].row)std::swap(v[1],v[2]);
    if(v[0].row>=v[1].row)std::swap(v[0],v[1]);
    auto [a,b,c]=v;
    if(c.row==a.row){
        uint16_t left=a.angle,right=b.angle,angle=c.angle;
        if(signed16(difference(angle,right))>=0){
            if(signed16(difference(angle,left))>=0){
                if(signed16(difference(left,right))<0)right=angle;else left=angle;
            }
        }else if(signed16(difference(angle,left))<0){
            if(signed16(difference(left,right))<0)left=angle;else right=angle;
        }
        trapezoid({left,0,0},{right,0,0},right,uint8_t(a.row),uint8_t(a.row),false,color);
    }else if(c.row==b.row){
        trapezoid(b,c,a.angle,uint8_t(a.row),uint8_t(c.row),true,color);
    }else if(b.row==a.row){
        trapezoid(a,b,c.angle,uint8_t(a.row),uint8_t(c.row),false,color);
    }else{
        uint16_t delta=difference(c.angle,a.angle);bool negative=signed16(delta)<0;
        uint32_t magnitude=negative?neg(delta):delta;
        magnitude=magnitude*(b.row-a.row)/(c.row-a.row);
        uint16_t split=uint16_t(a.angle+(negative?neg(uint16_t(magnitude)):uint16_t(magnitude)));
        ProjectedVertex middle{split,b.row,0};
        trapezoid(b,middle,a.angle,uint8_t(a.row),uint8_t(b.row),true,color);
        trapezoid(b,middle,c.angle,uint8_t(b.row),uint8_t(c.row),false,color);
    }
}
void Renderer::background(uint8_t horizon,uint8_t sky,uint8_t water){
    pixels.fill(0);for(int y=0;y<64;y++)for(int x=40;x<296;x++)pixels[(y+64)*320+x]=y<=horizon?sky:y<=horizon+2?8:water;
}
void Renderer::terrain(const World& world,uint16_t px,uint16_t py,const Camera& camera){
    // The original loads up to 512 vertices per group from a visibility-masked
    // 3x3 neighborhood, in a different order for each half-cell quadrant.
    int row=9-(uint8_t((py>>8)-4)>>2),col=(uint8_t((px>>8)-4)>>2)+1;
    int center=row*17+col;if(center<0||center>=187)return;
    uint8_t cell=world.cells[center],mask=(cell&63)<visibility.size()?visibility[cell&63]:255;
    unsigned turn=cell>>6;if(turn)mask=uint8_t((mask<<(turn*2))|(mask>>(8-turn*2)));mask^=255;
    int q=((py>>9)&1)*2+((px>>9)&1);
    struct Primitive{std::array<RenderVertex,3> vertices;uint8_t color;uint16_t key;};
    std::vector<Primitive> groups[2];unsigned counts[2]={0,0};
    for(int n=0;n<9;n++){
        size_t at=order[q*9+n]*5;if(neighbors[at]&mask)continue;
        int offset=int16_t(uint16_t(neighbors[at+3]|neighbors[at+4]<<8));int ci=center+offset;if(ci<0||ci>=187)continue;
        cell=world.cells[ci];const auto& tile=world.tiles[cell&63];
        uint16_t ox=uint16_t(uint8_t((px>>8&252)+neighbors[at+1]))<<8;
        uint16_t oy=uint16_t(uint8_t((py>>8&252)+neighbors[at+2]))<<8;
        unsigned start=0;
        for(int group=0;group<2;group++){
            unsigned total=tile.groups[group],available=std::min(total,512-counts[group]);
            for(const auto& f:tile.faces){
                if(unsigned(f.indices[0])<start||unsigned(f.indices[0])>=start+available)continue;
                bool fits=true;for(int i:f.indices)if(unsigned(i)<start||unsigned(i)>=start+available)fits=false;if(!fits)continue;
                Primitive primitive{};primitive.color=f.color;
                uint8_t nearest=0,farthest=255;
                for(size_t j=0;j<3;j++){
                    auto v=tile.vertices[f.indices[j]];auto [x,y]=rotate(v.x,v.y,cell>>6);
                    RenderVertex transformed{uint16_t((ox+(x&255)*8)*4),v.h,uint16_t((oy+(y&255)*8)*4)};
                    primitive.vertices[j]=transformed;auto projected=project(transformed,camera);
                    nearest=std::max(nearest,uint8_t(projected.inverseDistance));farthest=std::min(farthest,uint8_t(projected.inverseDistance));
                }
                primitive.key=uint16_t(nearest)|(uint16_t(farthest)<<8);groups[group].push_back(primitive);
            }
            counts[group]+=available;start+=total;
        }
    }
    // DOS merges group A with object sprites. Terrain-only ordering here is
    // the same ascending reciprocal-distance key. Group B is reverse stream.
    std::stable_sort(groups[0].begin(),groups[0].end(),[](const auto& a,const auto& b){return a.key<b.key;});
    for(const auto& p:groups[0])triangle({project(p.vertices[0],camera),project(p.vertices[1],camera),project(p.vertices[2],camera)},p.color);
    for(auto i=groups[1].rbegin();i!=groups[1].rend();++i)triangle({project(i->vertices[0],camera),project(i->vertices[1],camera),project(i->vertices[2],camera)},i->color);
}
void renderer_probe(const Bytes& executable,const std::filesystem::path& input,const std::filesystem::path& output){
    auto bytes=read_file(input);Renderer r(executable);Bytes result;size_t p=0;
    auto get=[&](){auto n=word(bytes,p);p+=2;return n;};
    auto put=[&](uint16_t n){result.push_back(uint8_t(n));result.push_back(uint8_t(n>>8));};
    unsigned projections=get();for(unsigned i=0;i<projections;i++){
        RenderVertex v{get(),get(),get()};Camera c{get(),get(),get(),uint8_t(get())};auto q=r.project(v,c);put(q.angle);put(q.row);put(q.inverseDistance);
    }
    unsigned triangles=get();for(unsigned i=0;i<triangles;i++){
        std::array<ProjectedVertex,3> v;for(auto& point:v)point={get(),get(),0};uint8_t color=uint8_t(get());r.pixels.fill(0);r.triangle(v,color);
        for(int y=64;y<128;y++)result.insert(result.end(),r.pixels.begin()+y*320+40,r.pixels.begin()+y*320+296);
    }
    if(p!=bytes.size())throw std::runtime_error("Unexpected renderer probe input bytes");write_file(output,result);
}
}
