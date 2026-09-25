#pragma once
#include "assets.hpp"

namespace gb::original {
// Coordinates and intermediate values are the original 16-bit quantities.
// Screen angles wrap at 65536; 128 angle units are one VGA viewport column.
struct RenderVertex { uint16_t x, height, y; };
struct ProjectedVertex { uint16_t angle, row, inverseDistance; };
struct Camera { uint16_t x=0,y=0,angle=0;uint8_t horizon=17; };
class Renderer {
    std::array<uint8_t,256> arctangent;
    std::array<uint16_t,128> cosine;
    std::array<uint8_t,8> negateAngle,quadrant;
    std::array<uint8_t,30> visibility;
    std::array<uint8_t,36> order;
    std::array<uint8_t,45> neighbors;
    void trapezoid(ProjectedVertex a,ProjectedVertex b,uint16_t apex,uint8_t first,
                   uint8_t last,bool startsAtApex,uint8_t color);
public:
    // Original scratch frame: drawing rectangle X=40..295, Y=64..127.
    std::array<uint8_t,320*200> pixels{};
    explicit Renderer(const Bytes& executable);
    ProjectedVertex project(RenderVertex,const Camera&)const;
    void triangle(std::array<ProjectedVertex,3>,uint8_t color);
    void background(uint8_t horizon,uint8_t sky=11,uint8_t water=9);
    void terrain(const World&,uint16_t playerX,uint16_t playerY,const Camera&);
};
void renderer_probe(const Bytes& executable,const std::filesystem::path& input,
                    const std::filesystem::path& output);
}
