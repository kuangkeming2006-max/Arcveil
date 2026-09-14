#pragma once
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace mcoverlay::silent {
// No JNI, rendering or networking on this worker. Producers enqueue bounded
// text records; file creation and buffered writes never run on a game thread.
class SilentDebugRecorder final {
public:
    ~SilentDebugRecorder() { stop(); }
    void configure(bool file,bool chat) noexcept {
        m_file.store(file); m_chat.store(chat);
        if(file && !m_started.exchange(true)) {
            try { m_worker=std::thread([this]{ run(); }); }
            catch(...) { m_started=false; m_file=false; }
        }
    }
    bool enabled() const noexcept { return m_file.load() || m_chat.load(); }
    bool chatEnabled() const noexcept { return m_chat.load(); }
    void stop() noexcept {
        m_file=false; m_chat=false; m_stopping=true; m_wake.notify_all();
        if(m_worker.joinable()) m_worker.join();
    }
    static const char* owner(unsigned value) noexcept {
        constexpr const char* names[]{"None","SilentCombat","ManualBlock","BedBreaker","Scaffold"};
        return value<5 ? names[value] : "Unknown";
    }
    template<class S> void event(const char* name,const S& s,
        const char* detail="",bool important=false) noexcept {
        if(!enabled()) return;
        char line[2400]{};
        std::snprintf(line,sizeof(line),
            "[%s] tick=%llu timestamp=%llu frame=%llu physicsTick=%llu snapshot=%llu camera=(%.6f,%.6f) logical=(%.6f,%.6f) "
            "previousLogical=(%.6f,%.6f) lastReported=(%.6f,%.6f) rotationOwner=%u interactionOwner=%s "
            "restorePending=%d physical=(%.5f,%.5f) resolved=(%.5f,%.5f) physicalSprint=%d logicalSprint=%d sprintSuppressed=%d sneaking=%d onGround=%d "
            "motion=(%.7f,%.7f,%.7f) candidate=%d cameraEntity=%d rayFirst=%d attackTarget=%d committedAttack=%d inputEvent=%llu attackIntent=%llu dispatches=%llu "
            "cameraBlock=(%d,%d,%d,%d) logicalBlock=(%d,%d,%d,%d) interactionMode=%u leftDown=%d %s\n",
            name,static_cast<unsigned long long>(s.interactionTick),
            static_cast<unsigned long long>(GetTickCount64()),static_cast<unsigned long long>(s.tick),
            static_cast<unsigned long long>(s.movement.physicsTick),
            static_cast<unsigned long long>(s.movement.snapshotVersion),
            s.camera.yaw,s.camera.pitch,s.logical.yaw,s.logical.pitch,s.previousLogical.yaw,s.previousLogical.pitch,
            s.lastReported.yaw,s.lastReported.pitch,static_cast<unsigned>(s.rotationOwner),
            owner(static_cast<unsigned>(s.interactionOwner)),s.restorePending,
            s.movement.physicalForward,s.movement.physicalStrafe,s.movement.forward,s.movement.strafe,
            s.movement.physicalSprinting,s.movement.sprinting,
            s.movement.sprintSuppressed,s.sneaking,
            s.movement.onGround,
            s.movement.currentVelocity.x,s.movement.currentVelocity.y,s.movement.currentVelocity.z,
            s.candidateTargetId,s.cameraMouseOverEntityId,s.rayFirstHitEntityId,s.attackTargetId,
            s.committedAttackTargetId,
            static_cast<unsigned long long>(s.inputEventId),
            static_cast<unsigned long long>(s.attackIntentId),
            static_cast<unsigned long long>(s.attackDispatchCount),
            s.cameraBlock.x,s.cameraBlock.y,s.cameraBlock.z,s.cameraBlock.valid,
            s.rayBlock.x,s.rayBlock.y,s.rayBlock.z,s.rayBlock.valid,
            static_cast<unsigned>(s.interaction),s.leftMouseDown,detail);
        try {
            std::lock_guard guard(m_mutex);
            if(m_file) {
                if(m_lines.size()<4096) m_lines.emplace_back(line);
                else ++m_dropped;
            }
            if(m_chat && important) {
                char brief[420]{};
                std::snprintf(brief,sizeof(brief),"[SL] %s owner=%s lock=%d ray=%d attack=%d %s",
                    name,owner(static_cast<unsigned>(s.interactionOwner)),s.candidateTargetId,
                    s.rayFirstHitEntityId,s.attackTargetId,detail);
                if(m_chatLines.size()<128) m_chatLines.emplace_back(brief);
            }
        } catch(...) {}
    }
    bool popChat(std::string& text) noexcept {
        std::lock_guard guard(m_mutex);
        if(!m_chat) { m_chatLines.clear(); return false; }
        if(m_chatLines.empty()) return false;
        text=std::move(m_chatLines.front()); m_chatLines.pop_front(); return true;
    }
    bool hasChat() noexcept {
        std::lock_guard guard(m_mutex); return m_chat && !m_chatLines.empty();
    }
    const std::atomic<bool>& fileError() const noexcept { return m_fileError; }
private:
    void run() noexcept {
        try {
            wchar_t local[32768]{};
            const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
            if(!length || length>=32768) { m_fileError=true; return; }
            const auto directory=std::filesystem::path(local)/L"Overlay Studio"/
                L"MinecraftOverlayManager"/L"diagnostics";
            std::filesystem::create_directories(directory);
            SYSTEMTIME time{}; GetLocalTime(&time);
            wchar_t name[100]{};
            std::swprintf(name,100,L"silent-%04u%02u%02u-%02u%02u%02u-%lu.log",
                time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId());
            std::ofstream file(directory/name,std::ios::out|std::ios::app);
            if(!file) { m_fileError=true; return; }
            file<<"Silent v33 diagnostic: monotonic timestamp in ms; owner 0=Camera 1=AimAssist 2=Restore; no credentials.\n";
            for(;;) {
                std::deque<std::string> batch;
                std::size_t dropped=0;
                { std::unique_lock guard(m_mutex);
                  m_wake.wait_for(guard,std::chrono::milliseconds(250),[this]{return m_stopping.load();});
                  batch.swap(m_lines); dropped=std::exchange(m_dropped,0); }
                if(dropped) file<<"[DROPPED] count="<<dropped<<'\n';
                for(const auto& row:batch) file<<row;
                file.flush();
                if(!file) { m_fileError=true; break; }
                if(m_stopping) break;
            }
        } catch(...) { m_fileError=true; }
    }
    std::atomic<bool> m_file{false},m_chat{false},m_started{false},m_stopping{false},m_fileError{false};
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::string> m_lines,m_chatLines;
    std::size_t m_dropped=0;
    std::thread m_worker;
};
}
