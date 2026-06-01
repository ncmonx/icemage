#ifdef _WIN32
#include "warm_pipe.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace icmg::llm {

std::string fullPipePath(const std::string& name) {
    return "\\\\.\\pipe\\" + name;
}

struct PipeServer::Connection {
    HANDLE handle = INVALID_HANDLE_VALUE;
    int instance_idx = -1;
};

struct PipeServer::Impl {
    PipeConfig cfg;
    std::vector<HANDLE> instances;
    std::vector<std::atomic<bool>> in_use;
    std::atomic<bool> stop_flag{false};
    std::mutex mtx;

    explicit Impl(const PipeConfig& c) : cfg(c), in_use(c.max_instances) {
        for (auto& b : in_use) b = false;
        instances.assign(c.max_instances, INVALID_HANDLE_VALUE);
        std::string path = fullPipePath(cfg.name);
        std::wstring wpath(path.begin(), path.end());
        for (int i = 0; i < c.max_instances; ++i) {
            HANDLE h = CreateNamedPipeW(
                wpath.c_str(),
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                c.max_instances,
                c.buffer_size,
                c.buffer_size,
                0,
                nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                for (auto& ih : instances) if (ih != INVALID_HANDLE_VALUE) CloseHandle(ih);
                throw std::runtime_error("CreateNamedPipeW failed");
            }
            instances[i] = h;
        }
    }
    ~Impl() {
        for (auto h : instances) if (h != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
        }
    }
};

PipeServer::PipeServer(const PipeConfig& cfg) : impl_(std::make_unique<Impl>(cfg)) {}
PipeServer::~PipeServer() = default;

std::optional<std::shared_ptr<PipeServer::Connection>>
PipeServer::accept(std::atomic<bool>& stop) {
    while (!stop.load() && !impl_->stop_flag) {
        int idx = -1;
        {
            std::lock_guard lk(impl_->mtx);
            for (size_t i = 0; i < impl_->in_use.size(); ++i) {
                bool expected = false;
                if (impl_->in_use[i].compare_exchange_strong(expected, true)) {
                    idx = (int)i; break;
                }
            }
        }
        if (idx < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        HANDLE h = impl_->instances[idx];
        BOOL ok = ConnectNamedPipe(h, nullptr);
        DWORD err = GetLastError();
        if (!ok && err != ERROR_PIPE_CONNECTED) {
            impl_->in_use[idx] = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        auto conn = std::make_shared<Connection>();
        conn->handle = h;
        conn->instance_idx = idx;
        return conn;
    }
    return std::nullopt;
}

std::string PipeServer::readMessage(Connection& c) {
    std::string buf;
    buf.reserve(4096);
    char tmp[4096];
    while (true) {
        DWORD n = 0;
        BOOL ok = ReadFile(c.handle, tmp, sizeof(tmp), &n, nullptr);
        if (!ok || n == 0) return {};
        buf.append(tmp, n);
        if (!buf.empty() && buf.back() == '\n') break;
        if (buf.size() > (size_t)impl_->cfg.buffer_size) return {};
    }
    if (!buf.empty() && buf.back() == '\n') buf.pop_back();
    return buf;
}

bool PipeServer::writeMessage(Connection& c, const std::string& json) {
    std::string out = json;
    out.push_back('\n');
    DWORD n = 0;
    return WriteFile(c.handle, out.data(), (DWORD)out.size(), &n, nullptr) &&
           n == out.size();
}

void PipeServer::disconnect(Connection& c) {
    if (c.handle == INVALID_HANDLE_VALUE) return;
    FlushFileBuffers(c.handle);
    DisconnectNamedPipe(c.handle);
    if (c.instance_idx >= 0)
        impl_->in_use[c.instance_idx] = false;
    c.handle = INVALID_HANDLE_VALUE;
    c.instance_idx = -1;
}

void PipeServer::stop() {
    impl_->stop_flag = true;
    std::string path = fullPipePath(impl_->cfg.name);
    std::wstring wp(path.begin(), path.end());
    HANDLE dummy = CreateFileW(wp.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE) CloseHandle(dummy);
}

}  // namespace icmg::llm

// ---- PipeClient implementation ----

namespace icmg::llm {

struct PipeClient::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Impl() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

PipeClient::PipeClient() : impl_(std::make_unique<Impl>()) {}
PipeClient::~PipeClient() = default;
PipeClient::PipeClient(PipeClient&&) noexcept = default;
PipeClient& PipeClient::operator=(PipeClient&&) noexcept = default;

std::optional<PipeClient> PipeClient::connect(
    const std::string& name, std::chrono::milliseconds timeout)
{
    std::string path = fullPipePath(name);
    std::wstring wp(path.begin(), path.end());
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        HANDLE h = CreateFileW(wp.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            PipeClient pc;
            pc.impl_->handle = h;
            return pc;
        }
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        WaitNamedPipeW(wp.c_str(), 50);
    }
    return std::nullopt;
}

std::string PipeClient::sendRequest(const std::string& json_req,
                                     std::chrono::milliseconds read_timeout)
{
    if (impl_->handle == INVALID_HANDLE_VALUE) return {};
    std::string out = json_req;
    if (out.empty() || out.back() != '\n') out.push_back('\n');
    DWORD n = 0;
    if (!WriteFile(impl_->handle, out.data(), (DWORD)out.size(), &n, nullptr) ||
        n != out.size()) return {};

    auto deadline = std::chrono::steady_clock::now() + read_timeout;
    std::string buf;
    char tmp[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD avail = 0;
        BOOL pok = PeekNamedPipe(impl_->handle, nullptr, 0, nullptr, &avail, nullptr);
        if (!pok) return {};
        if (avail == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        DWORD got = 0;
        if (!ReadFile(impl_->handle, tmp, sizeof(tmp), &got, nullptr)) return {};
        buf.append(tmp, got);
        if (!buf.empty() && buf.back() == '\n') {
            buf.pop_back();
            return buf;
        }
    }
    return {};
}

}  // namespace icmg::llm
#endif
