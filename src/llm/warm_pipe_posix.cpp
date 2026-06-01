#ifndef _WIN32
// POSIX warm-pipe IPC via Unix domain sockets — mirror of warm_pipe_win.cpp
// (named pipes on Windows). Newline-delimited JSON, one round-trip per call.
// Socket path: $TMPDIR/<name>.sock (or /tmp/<name>.sock).
#include "warm_pipe.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>

namespace icmg::llm {

std::string fullPipePath(const std::string& name) {
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = (tmp && *tmp) ? tmp : "/tmp";
    if (!dir.empty() && dir.back() == '/') dir.pop_back();
    return dir + "/" + name + ".sock";
}

namespace {

// Fill sockaddr_un from a path; returns false if path too long.
bool fillAddr(sockaddr_un& addr, const std::string& path) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) return false;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return true;
}

// Read until '\n' (exclusive) or timeout/EOF. Returns line without newline.
std::string readLine(int fd, std::chrono::milliseconds timeout) {
    std::string out;
    char buf[4096];
    auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                     deadline - now).count();
        pollfd p{fd, POLLIN, 0};
        int r = ::poll(&p, 1, ms);
        if (r <= 0) break;                       // timeout or error
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;                       // EOF or error
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') return out;
            out.push_back(buf[i]);
        }
    }
    return out;
}

bool writeAll(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR)) continue;
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// PipeServer
// ---------------------------------------------------------------------------

struct PipeServer::Connection {
    int fd = -1;
};

struct PipeServer::Impl {
    PipeConfig cfg;
    std::string path;
    int listen_fd = -1;
    std::atomic<bool> stopping{false};
};

PipeServer::PipeServer(const PipeConfig& cfg) : impl_(std::make_unique<Impl>()) {
    impl_->cfg = cfg;
    impl_->path = fullPipePath(cfg.name);
    ::unlink(impl_->path.c_str());  // stale socket from a previous run

    impl_->listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (impl_->listen_fd < 0) return;

    sockaddr_un addr{};
    if (!fillAddr(addr, impl_->path)) { ::close(impl_->listen_fd); impl_->listen_fd = -1; return; }
    // Security: restrict the socket to the owning user. umask(0077) before bind
    // so the socket is created mode 0600 (no group/other access); chmod after as
    // a belt-and-suspenders guard on platforms that ignore umask for sockets.
    mode_t old_umask = ::umask(0077);
    int bind_rc = ::bind(impl_->listen_fd, (sockaddr*)&addr, sizeof(addr));
    ::umask(old_umask);
    if (bind_rc < 0) {
        ::close(impl_->listen_fd); impl_->listen_fd = -1; return;
    }
    ::chmod(impl_->path.c_str(), 0600);
    if (::listen(impl_->listen_fd, impl_->cfg.max_instances) < 0) {
        ::close(impl_->listen_fd); impl_->listen_fd = -1; return;
    }
}

PipeServer::~PipeServer() {
    if (impl_ && impl_->listen_fd >= 0) {
        ::close(impl_->listen_fd);
        ::unlink(impl_->path.c_str());
    }
}

std::optional<std::shared_ptr<PipeServer::Connection>>
PipeServer::accept(std::atomic<bool>& stop) {
    if (!impl_ || impl_->listen_fd < 0) return std::nullopt;
    for (;;) {
        if (stop.load() || impl_->stopping.load()) return std::nullopt;
        pollfd p{impl_->listen_fd, POLLIN, 0};
        int r = ::poll(&p, 1, 200);  // 200 ms tick to re-check stop
        if (r < 0) { if (errno == EINTR) continue; return std::nullopt; }
        if (r == 0) continue;
        int cfd = ::accept(impl_->listen_fd, nullptr, nullptr);
        if (cfd < 0) { if (errno == EINTR) continue; return std::nullopt; }
        auto c = std::make_shared<Connection>();
        c->fd = cfd;
        return c;
    }
}

std::string PipeServer::readMessage(Connection& c) {
    if (c.fd < 0) return {};
    return readLine(c.fd, std::chrono::milliseconds(60000));
}

bool PipeServer::writeMessage(Connection& c, const std::string& json) {
    if (c.fd < 0) return false;
    std::string framed = json;
    if (framed.empty() || framed.back() != '\n') framed.push_back('\n');
    return writeAll(c.fd, framed);
}

void PipeServer::disconnect(Connection& c) {
    if (c.fd >= 0) { ::close(c.fd); c.fd = -1; }
}

void PipeServer::stop() {
    if (impl_) impl_->stopping.store(true);
}

// ---------------------------------------------------------------------------
// PipeClient
// ---------------------------------------------------------------------------

struct PipeClient::Impl {
    int fd = -1;
    ~Impl() { if (fd >= 0) ::close(fd); }
};

PipeClient::PipeClient() : impl_(std::make_unique<Impl>()) {}
PipeClient::~PipeClient() = default;
PipeClient::PipeClient(PipeClient&&) noexcept = default;
PipeClient& PipeClient::operator=(PipeClient&&) noexcept = default;

std::optional<PipeClient> PipeClient::connect(
    const std::string& name, std::chrono::milliseconds timeout) {
    std::string path = fullPipePath(name);
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return std::nullopt;

    sockaddr_un addr{};
    if (!fillAddr(addr, path)) { ::close(fd); return std::nullopt; }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            PipeClient pc;
            pc.impl_->fd = fd;
            return pc;
        }
        if (std::chrono::steady_clock::now() >= deadline) { ::close(fd); return std::nullopt; }
        // Server may not be listening yet — brief backoff + retry.
        struct timespec ts{0, 20 * 1000 * 1000};  // 20 ms
        nanosleep(&ts, nullptr);
    }
}

std::string PipeClient::sendRequest(const std::string& json_req,
                                    std::chrono::milliseconds read_timeout) {
    if (!impl_ || impl_->fd < 0) return {};
    std::string framed = json_req;
    if (framed.empty() || framed.back() != '\n') framed.push_back('\n');
    if (!writeAll(impl_->fd, framed)) return {};
    return readLine(impl_->fd, read_timeout);
}

}  // namespace icmg::llm
#endif  // !_WIN32
