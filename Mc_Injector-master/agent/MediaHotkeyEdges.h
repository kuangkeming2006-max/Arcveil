#pragma once
#include <array>
#include <cstddef>
namespace mcoverlay {
class MediaHotkeyEdges final {
public:
    bool update(std::size_t index,int key,bool down,bool allowed) noexcept {
        auto& state=m_keys[index];
        const bool changed=!state.primed || state.key!=key;
        const bool edge=!changed && down && !state.down;
        state={key,down,true};
        // Native Windows media keys already control GSMTC; never send a
        // second toggle. Blocked input is still sampled to avoid stale edges.
        const bool native=key==0xB0 || key==0xB1 || key==0xB3;
        return allowed && key>=8 && key<=254 && !native && edge;
    }
private:
    struct Key {int key=0;bool down=false,primed=false;};
    std::array<Key,3> m_keys{};
};
}
