// v1.52.0: icmg llm warm --start/stop/status + hidden warm-loop daemon entry.
#include "../../core/path_utils.hpp"
#include "../../llm/warm_client.hpp"
#include "../../llm/warm_loop.hpp"
#include "../../llm/warm_pipe.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string pidFilePath() {
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) home = ".";
    return (fs::path(home) / ".icmg" / "llm" / "warm.pid").string();
}

int readPid(const std::string& path) {
    std::ifstream f(path);
    if (!f) return -1;
    int pid = -1; f >> pid;
    return pid;
}

bool processAlive(int pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD r = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return r == WAIT_TIMEOUT;
#else
    return false;  // Linux stub
#endif
}

int doStart() {
    auto pf = pidFilePath();
    int existing = readPid(pf);
    if (existing > 0 && processAlive(existing)) {
        if (icmg::llm::warmAvailable()) {
            std::cout << "warm-loop already running (pid=" << existing << ")\n";
            return 0;
        }
#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)existing);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
#endif
    }
    std::error_code ec;
    fs::create_directories(fs::path(pf).parent_path(), ec);
    fs::remove(pf, ec);

#ifdef _WIN32
    std::string self = icmg::core::selfExePath();
    std::string cmdline = "\"" + self + "\" llm warm-loop";
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdbuf(cmdline.begin(), cmdline.end());
    cmdbuf.push_back(0);
    BOOL ok = CreateProcessA(
        nullptr, cmdbuf.data(), nullptr, nullptr, FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr, &si, &pi);
    if (!ok) {
        std::cerr << "warm-loop: CreateProcess failed err=" << GetLastError() << "\n";
        return 1;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    std::cout << "warm-loop started (pid=" << pi.dwProcessId << ")\n";
    for (int i = 0; i < 50; ++i) {
        if (icmg::llm::warmAvailable()) {
            std::cout << "  pipe ready\n";
            return 0;
        }
        Sleep(100);
    }
    std::cout << "  warning: pipe not yet ready after 5s — model may still be loading\n";
    return 0;
#else
    std::cerr << "warm-loop: Linux start not implemented in v1.52.0\n";
    return 2;
#endif
}

int doStop() {
    auto c = icmg::llm::PipeClient::connect(icmg::llm::warmPipeName(),
                                              std::chrono::milliseconds(200));
    if (!c) { std::cout << "warm-loop: not running\n"; return 0; }
    auto resp = c->sendRequest(R"({"id":"stop","cmd":"shutdown"})",
                                std::chrono::milliseconds(2000));
    if (resp.find("shutting_down") != std::string::npos) {
        std::cout << "warm-loop: stopped\n";
        return 0;
    }
    std::cout << "warm-loop: stop request sent (response unclear)\n";
    return 0;
}

int doStatus() {
    auto c = icmg::llm::PipeClient::connect(icmg::llm::warmPipeName(),
                                              std::chrono::milliseconds(200));
    if (!c) { std::cout << "warm-loop: not running\n"; return 1; }
    auto resp = c->sendRequest(R"({"id":"status","cmd":"status"})",
                                std::chrono::milliseconds(2000));
    if (resp.empty()) { std::cout << "warm-loop: timeout\n"; return 1; }
    std::cout << resp << "\n";
    return 0;
}

int doWarmLoop() {
    icmg::llm::WarmLoopConfig cfg;
    cfg.pipe.name = icmg::llm::warmPipeName();
    cfg.pid_file_path = pidFilePath();
    return icmg::llm::runWarmLoop(cfg);
}

}  // anon

namespace icmg::cli {

int runLlmWarm(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: icmg llm warm --start | --stop | --status\n";
        return 2;
    }
    const auto& a = args[0];
    if (a == "--start")  return doStart();
    if (a == "--stop")   return doStop();
    if (a == "--status") return doStatus();
    std::cerr << "warm: unknown action: " << a << "\n";
    return 2;
}

int runLlmWarmLoop(const std::vector<std::string>&) {
    return doWarmLoop();
}

}  // namespace icmg::cli
