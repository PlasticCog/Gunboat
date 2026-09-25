#include "original_weapons.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main(int argc, char **argv) {
    try {
        if (argc < 2) return 2;
        gb::Archive archive(argv[1]);
        gb::original::Weapons weapons(archive.exe);
        gb::original::WeaponState state;
        auto first = weapons.fire(state,4);
        require(first && first->weapon == 2 && first->sound == 8 && state.reload4 == 7,
                "Station 4 did not start original reload");
        for (int i=0;i<7;++i) {
            require(!weapons.fire(state,4),"Station 4 fired before reloading");
            weapons.reload_tick(state);
        }
        require(bool(weapons.fire(state,4)),"Station 4 failed to reload");
        auto middle=weapons.fire(state,3);
        require(middle && middle->weapon==3 && state.reload3==47,"Station 3 reload mismatch");
        for(int i=0;i<47;++i) {
            require(!weapons.fire(state,3),"Station 3 fired before reloading");
            weapons.reload_tick(state);
        }
        require(bool(weapons.fire(state,3)),"Station 3 failed to reload");
        state={}; state.boatX=1000; state.boatY=2000; state.heading[0]=64;
        for(int i=0;i<40;++i) {
            auto shot=weapons.fire(state,2);
            require(shot && shot->weapon==4 && shot->sound==1,"Original bow gun failed to fire");
            require(shot->projectile.x>1000 && shot->projectile.y==2000,"Integer projectile direction changed");
        }
        require(weapons.fire(state,2)->slot==0,"Full original projectile pool did not reuse slot zero");
        require(state.alternatingBarrel==41,"Twin bow barrels stopped alternating");
        state.condition[0][1]=1;
        require(!weapons.fire(state,2),"Disabled mount fired");
        auto hit=weapons.hit(10,0,0,3);
        require(hit.kind==10 && !hit.destroyed,"Immune object was destroyed");
        hit=weapons.hit(43,0,1,4);
        require(hit.kind==43 && !hit.destroyed,"Machine gun damaged grenade-only target");
        hit=weapons.hit(43,0,1,2);
        require(hit.kind==0x31 && hit.destroyed,"Original destroyed variant not selected");
        hit=weapons.hit(10,0,27,4);
        auto second=weapons.hit(hit.kind,hit.flags,27,4);
        require(hit.flags==8 && second.flags==8 && !second.destroyed,"Original armor OR damage became additive");
        state={}; state.projectiles[0].ticks=1;state.projectiles[31].ticks=1;
        state.projectiles[0].kind=1;state.projectiles[31].kind=2;
        require(weapons.projectile_tick(state,true).empty(),"Frozen projectiles advanced");
        auto impacts=weapons.projectile_tick(state);
        require(impacts.size()==2 && impacts[0].kind==2 && impacts[1].kind==1,"Original impact ordering changed");
        std::cout<<"Original weapon reload, firing, pool, armor, impact ordering and freeze checks passed.\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
