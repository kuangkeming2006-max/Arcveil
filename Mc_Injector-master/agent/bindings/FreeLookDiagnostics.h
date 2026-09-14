#pragma once

#include <Windows.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace mcoverlay {

// Always-on, event-driven FreeLook session log. File I/O stays off the render
// and JVM callback threads; verbose bridge sampling is opt-in.
class FreeLookDiagnostics final {
public:
    FreeLookDiagnostics() noexcept { start(); }
    ~FreeLookDiagnostics(){stop("destructor");}
    FreeLookDiagnostics(const FreeLookDiagnostics&)=delete;
    FreeLookDiagnostics& operator=(const FreeLookDiagnostics&)=delete;

    void event(const std::string_view name,const std::string_view detail={}) noexcept {
        if(m_stopping.load(std::memory_order_acquire)) return;
        try {
            char prefix[160]{};
            std::snprintf(prefix,sizeof(prefix),"[%.*s] timestamp=%llu pid=%lu ",
                static_cast<int>(std::min<std::size_t>(name.size(),80U)),name.data(),
                static_cast<unsigned long long>(GetTickCount64()),GetCurrentProcessId());
            std::string line(prefix);
            line.append(detail.data(),detail.size());
            line.push_back('\n');
            std::lock_guard guard(m_mutex);
            if(m_lines.size()<2048U)m_lines.emplace_back(std::move(line));
            else ++m_dropped;
            m_wake.notify_one();
        } catch(...) {}
    }
    void setVerbose(bool value) noexcept{m_verbose.store(value,std::memory_order_release);}
    [[nodiscard]] bool verbose() const noexcept{return m_verbose.load(std::memory_order_acquire);}
    void stop(const char* reason="release") noexcept {
        if(!m_started.load(std::memory_order_acquire))return;
        if(!m_stopping.exchange(true)) {
            try {
                char line[200]{};
                std::snprintf(line,sizeof(line),"[SESSION_END] timestamp=%llu pid=%lu reason=%s\n",
                    static_cast<unsigned long long>(GetTickCount64()),
                    GetCurrentProcessId(),reason?reason:"unknown");
                std::lock_guard guard(m_mutex);m_lines.emplace_back(line);
            } catch(...) {}
        }
        m_wake.notify_all();
        if(m_worker.joinable())m_worker.join();
    }
private:
    void start() noexcept {
        if(m_started.exchange(true))return;
        try{m_worker=std::thread([this]{run();});}
        catch(...){m_started=false;m_fileError=true;}
    }
    void run() noexcept {
        try {
            wchar_t local[32768]{};
            const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
            if(!length||length>=32768){m_fileError=true;return;}
            const auto directory=std::filesystem::path(local)/L"Overlay Studio"/
                L"MinecraftOverlayManager"/L"diagnostics";
            std::filesystem::create_directories(directory);
            SYSTEMTIME time{};GetLocalTime(&time);wchar_t fileName[112]{};
            std::swprintf(fileName,112,L"freelook-%04u%02u%02u-%02u%02u%02u-%lu.log",
                time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,
                time.wSecond,GetCurrentProcessId());
            const auto path=directory/fileName;
            std::ofstream file(path,std::ios::out|std::ios::app);
            if(!file){m_fileError=true;return;}
            file<<"[SESSION_START] build=v40 pid="<<GetCurrentProcessId()
                <<" timestamp="<<GetTickCount64()<<" mapping=unresolved\n";
            file<<"[LOG_PATH] "<<path.string()<<'\n';file.flush();
            const std::wstring debug=L"[McOverlayAgent][INFO] FreeLook diagnostics: "+
                path.wstring()+L"\n";
            OutputDebugStringW(debug.c_str());
            for(;;) {
                std::deque<std::string> batch;std::size_t dropped=0U;
                {
                    std::unique_lock guard(m_mutex);
                    m_wake.wait_for(guard,std::chrono::milliseconds(250),[this]{
                        return m_stopping.load()||!m_lines.empty();});
                    batch.swap(m_lines);dropped=m_dropped;m_dropped=0U;
                }
                if(dropped)file<<"[DROPPED] count="<<dropped<<'\n';
                for(const auto& line:batch)file<<line;
                file.flush();
                if(!file){m_fileError=true;break;}
                if(m_stopping.load()&&batch.empty())break;
            }
        } catch(...){m_fileError=true;}
    }
    std::atomic<bool> m_started{false},m_stopping{false};
    std::atomic<bool> m_verbose{false},m_fileError{false};
    std::mutex m_mutex;std::condition_variable m_wake;
    std::deque<std::string> m_lines;std::size_t m_dropped=0U;
    std::thread m_worker;
};

} // namespace mcoverlay
