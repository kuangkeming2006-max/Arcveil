#include "../agent/FeatureNavigation.h"
#include "../agent/bindings/TrajectoryMath.h"
#include <array>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
struct Point { double x,y,z; };
struct Box { double minX,minY,minZ,maxX,maxY,maxZ; };
}
int main() {
    using namespace mcoverlay;
    int checks=0, failures=0;
    const auto check=[&](bool ok,const char* name) {
        ++checks; if(!ok) { ++failures; std::printf("FAIL %s\n",name); }
    };
    const auto has=[](const navigation::Result& result,int page) {
        for(std::size_t i=0;i<result.count;++i) if(result.rows[i].page==page) return true;
        return false;
    };
    auto result=navigation::filter("");
    check(result.count==navigation::rows.size(),"empty search restores all pages");
    result=navigation::filter("  bEd  ");
    check(has(result,1) && has(result,3) && !has(result,0),"case-insensitive bed search across categories");
    result=navigation::filter("MOVEMENT");
    check(result.count==6 && has(result,4) && has(result,24) && !has(result,15),
          "category search includes visible movement children only");
    result=navigation::filter("");
    check(!has(result,15)&&!has(result,17),
          "retired Long Jump and Local Combat pages stay hidden globally");
    result=navigation::filter("弓箭");
    check(result.count==2 && has(result,16),"UTF8 keyword search");
    result=navigation::filter("silent");
    check(result.count==2 && has(result,8),"setting alias finds its feature page");
    check(navigation::filter("not a real feature").count==0,"empty result has no orphan headings");
    for(const auto& row:navigation::rows) {
        if(row.page<0) continue;
        result=navigation::filter(row.label);
        check(has(result,row.page),"all real feature labels are searchable");
        for(std::size_t i=0;i<result.count;++i)
            if(result.rows[i].page<0)
                check(i+1<result.count && result.rows[i+1].page>=0,"headings require matching children");
    }
    check(navigation::glyphGlow(0,5,0)==1,"wave starts on first whole glyph");
    check(navigation::glyphGlow(4,5,4.0/5.2)==1,"faster wave reaches last glyph");
    check(navigation::glyphGlow(3,5,5.0/5.2)==1,"faster wave reverses direction");
    check(navigation::glyphGlow(0,5,8.0/5.2)==1,"faster wave loops to first glyph");
    for(int frame=0;frame<2400;++frame) {
        float total=0,brightest=0;
        for(std::size_t glyph=0;glyph<16;++glyph) {
            const float value=navigation::glyphGlow(glyph,16,frame/240.0);
            check(value>=0 && value<=1,"whole-glyph brightness remains bounded");
            total+=value; brightest=std::max(brightest,value);
        }
        check(brightest>0.95F,"continuous wave never disappears between letters");
        check(total>1.0F,"several adjacent whole glyphs can illuminate together");
    }
    const Point from{0,1,0},to{10,1,0};
    const Box mob{2,0,-.3,2.6,2,.3},player{4,0,-.3,4.6,2,.3};
    const auto mobHit=trajectory::segmentBox(from,to,mob,.3);
    const auto playerHit=trajectory::segmentBox(from,to,player,.3);
    check(std::abs(mobHit-.17)<1e-10 && mobHit<playerHit,"earlier living mob wins over later player");
    check(.1<mobHit && mobHit<.8,"block before entity wins; ground after entity loses");
    auto point=trajectory::interpolate(from,to,mobHit);
    check(std::abs(point.x-1.7)<1e-10,"impact lies on swept expanded hitbox");
    check(std::isinf(trajectory::segmentBox(Point{0,4,0},Point{10,4,0},mob)),"above mob misses");
    check(trajectory::segmentBox(Point{2.3,1,0},Point{2.3,1,0},mob)==0,"inside box collides immediately");
    check(std::isinf(trajectory::segmentBox(from,from,mob)),"stationary point outside misses");
    check(std::abs(trajectory::segmentBox(to,from,mob)-.74)<1e-10,"negative direction sweep");
    check(std::isinf(trajectory::segmentBox(Point{NAN,1,0},to,mob)),"invalid coordinates fail closed");
    check(std::isinf(trajectory::segmentBox(from,to,Box{3,0,0,1,2,1})),"inverted bounds fail closed");
    check(trajectory::bowStrength(-50)==0 && trajectory::bowStrength(NAN)==0,"invalid charge ignored");
    check(trajectory::bowStrength(0)==0 && trajectory::bowStrength(20)==1 &&
          trajectory::bowStrength(40)==1,"charge saturates at vanilla full draw");
    check(std::abs(trajectory::bowStrength(10)-5.0/12)<1e-10,"partial draw uses vanilla nonlinear speed");
    std::printf("Navigation/trajectory: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
