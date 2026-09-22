#pragma once
#include <array>
#include <string_view>
#include <cmath>
#include <algorithm>

namespace mcoverlay::navigation {
struct Row { const char* label; int page; const char* keywords; };
inline constexpr auto rows=std::to_array<Row>({
    {"COMBAT",-1,"战斗"},{"Aim Assist",8,"smooth lock silent 瞄准 辅助 锁定"},{"Smart Hotbar",23,"inventory sword blocks number keys item slot 智能 物品栏 剑 方块 数字键"},{"Bed Breaker",20,"bed tool path local 床 挖掘 工具 路径"},{"Velocity",21,"knockback horizontal vertical probability 击退 水平 垂直 概率"},
    {"MOVEMENT",-1,"移动"},{"SafeWalk",4,"蹲 安全 边缘"},{"Scaffold",5,"搭桥 方块"},{"Flight",6,"fly 飞行"},
    {"Bunny Hop",7,"bhop 跳跃"},{"Sprint",24,"force sprint 疾跑 自动 强制"},
    {"VISUALS",-1,"视觉 透视"},{"Player ESP",0,"玩家 框 队友"},{"Bed ESP",1,"床 防御 材质"},
    {"Nametags",2,"名字 血量 头像"},{"Fireball ESP",14,"火球"},{"Trajectories",16,"bow knockback 弓箭 击退 预测 轨迹"},
    {"FreeLook",22,"camera perspective third person freelook 自由视角 相机 第三人称"},
    {"HUD & ALERTS",-1,"面板 提醒"},{"Bed Alerts",3,"床 警告"},{"Player Stats",9,"hypixel 战绩 查询"},{"Now Playing",19,"media music spotify 音乐 播放器 媒体"},{"Module List",12,"textgui 文本 功能"},
    {"PLAYERS",-1,"玩家"},{"Blacklist",11,"黑名单"},{"Attack Shield",25,"player impulse knockback 屏蔽 攻击 冲量"},
    {"SETTINGS",-1,"设置"},{"Interface",13,"theme 主题 界面"},{"Input Method",18,"ime 输入法"},{"Diagnostics",10,"debug 调试"}
});
inline bool contains(std::string_view text,std::string_view query) noexcept {
    const auto lower=[](unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; };
    return std::search(text.begin(),text.end(),query.begin(),query.end(),
        [&](char a,char b){return lower(static_cast<unsigned char>(a))==lower(static_cast<unsigned char>(b));})!=text.end();
}
struct Result { std::array<Row,rows.size()> rows{}; std::size_t count=0; };
inline Result filter(std::string_view query) noexcept {
    while(!query.empty() && query.front()==' ') query.remove_prefix(1);
    while(!query.empty() && query.back()==' ') query.remove_suffix(1);
    Result result; const Row* category=nullptr; bool added=false,categoryMatches=false;
    for(const auto& row:rows) {
        if(row.page<0) { category=&row; added=false; categoryMatches=contains(row.label,query)||contains(row.keywords,query); continue; }
        if(!query.empty() && !categoryMatches && !contains(row.label,query) && !contains(row.keywords,query)) continue;
        if(category && !added) { result.rows[result.count++]=*category; added=true; }
        result.rows[result.count++]=row;
    }
    return result;
}
// A moving centre lights entire glyphs, never a gradient clipped across a glyph.
inline float glyphGlow(std::size_t index,std::size_t count,double time) noexcept {
    if(count<2) return 1;
    const double span=double(count-1), phase=std::fmod(std::max(0.0,time)*5.2,2*span);
    const double centre=phase<=span ? phase : 2*span-phase;
    // Keep a broad, flat centre so the highlight never dims while crossing
    // between two glyphs.  The falloff still lights neighbouring complete
    // glyphs; no gradient is ever sliced through the middle of a character.
    const double distance=std::abs(double(index)-centre);
    const float x=std::clamp(1.0F-static_cast<float>(
        std::max(0.0,distance-0.55))/1.75F,0.0F,1.0F);
    return x*x*(3-2*x);
}
}
