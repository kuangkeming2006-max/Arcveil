#pragma once
#include <cmath>

namespace mcoverlay::aim {
enum class AimTargetFilter { Normal=0, Force=1 };
[[nodiscard]] inline constexpr bool validTargetFilter(int value) noexcept {
    return value==0||value==1;
}
[[nodiscard]] inline constexpr const char* targetFilterName(AimTargetFilter filter) noexcept {
    return filter==AimTargetFilter::Force?"force":"normal";
}
[[nodiscard]] inline constexpr bool confirmedCombatTeammate(
    bool hypixel,bool match,char ownTeam,char entityTeam) noexcept {
    return hypixel&&match&&ownTeam!='u'&&entityTeam==ownTeam;
}
// Classification is authored by the snapshot's actual player class / exact
// world-player object membership, never inferred from names, colours or TAB.
template<class Marker>
[[nodiscard]] inline bool playerEspHitboxEligible(const Marker& marker,int selfId) noexcept {
    const auto& b=marker.bounds;
    return marker.player&&!marker.fireball&&marker.entityId>=0&&marker.entityId!=selfId&&
        std::isfinite(marker.health)&&marker.health>0.F&&
        std::isfinite(marker.currentX)&&std::isfinite(marker.currentY)&&std::isfinite(marker.currentZ)&&
        std::isfinite(marker.previousX)&&std::isfinite(marker.previousY)&&std::isfinite(marker.previousZ)&&
        std::isfinite(b.minX)&&std::isfinite(b.minY)&&std::isfinite(b.minZ)&&
        std::isfinite(b.maxX)&&std::isfinite(b.maxY)&&std::isfinite(b.maxZ)&&
        b.maxX>b.minX&&b.maxY>b.minY&&b.maxZ>b.minZ;
}
template<class Marker>
[[nodiscard]] inline bool targetIdentityEligible(AimTargetFilter filter,const Marker& marker,
    bool hypixel,bool match,char ownTeam) noexcept {
    if(filter==AimTargetFilter::Force) return marker.player&&!marker.fireball;
    const bool coloured=marker.player&&marker.teamColor!='u'&&marker.teamColor!='\0';
    // Keep Normal's exact prior truth table, including its confirmed TAB path.
    const bool validPlayer=marker.confirmedPlayer||(match&&coloured)||(!hypixel&&marker.player);
    return validPlayer&&!confirmedCombatTeammate(hypixel,match,ownTeam,marker.teamColor);
}
template<class Marker>
[[nodiscard]] inline bool targetCandidateEligible(AimTargetFilter filter,const Marker& marker,
    int selfId,bool hypixel,bool match,char ownTeam) noexcept {
    if(filter==AimTargetFilter::Force) return playerEspHitboxEligible(marker,selfId);
    return targetIdentityEligible(filter,marker,hypixel,match,ownTeam)&&marker.entityId!=selfId&&
        std::isfinite(marker.health)&&marker.health>0.F;
}
} // namespace mcoverlay::aim
