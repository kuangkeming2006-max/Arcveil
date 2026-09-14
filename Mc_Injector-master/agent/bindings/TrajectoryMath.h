#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace mcoverlay::trajectory {
// Returns the first fraction along a swept segment. Shared by arrows and
// silent targeting; testing endpoints alone tunnels through narrow hitboxes.
template<class Point, class Box>
double segmentBox(Point from, Point to, const Box& box, double padding=0) noexcept {
    double enter=0, leave=1;
    const auto axis=[&](double origin,double direction,double low,double high) {
        if (!std::isfinite(origin) || !std::isfinite(direction) ||
            !std::isfinite(low) || !std::isfinite(high) || low > high ||
            !std::isfinite(padding) || padding < 0) return false;
        low-=padding; high+=padding;
        if(std::abs(direction)<1e-10) return origin>=low && origin<=high;
        double a=(low-origin)/direction,b=(high-origin)/direction;
        if(a>b) std::swap(a,b);
        enter=std::max(enter,a); leave=std::min(leave,b);
        return enter<=leave;
    };
    if(axis(from.x,to.x-from.x,box.minX,box.maxX) &&
       axis(from.y,to.y-from.y,box.minY,box.maxY) &&
       axis(from.z,to.z-from.z,box.minZ,box.maxZ)) return enter;
    return std::numeric_limits<double>::infinity();
}
template<class Point> double distanceSquared(Point a,Point b) noexcept {
    return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z);
}
template<class Point> Point interpolate(Point a,Point b,double t) noexcept {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
inline double bowStrength(double ticks) noexcept {
    if (!std::isfinite(ticks) || ticks <= 0) return 0;
    const double t=ticks/20.0;
    return std::clamp((t*t+2*t)/3,0.0,1.0);
}
}
