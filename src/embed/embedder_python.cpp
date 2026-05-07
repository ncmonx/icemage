// Phase 23 Task 1: Python sidecar embedder.
// Spawns `python3 <embed_script>` once per CLI run; communicates JSON-per-line
// over stdin/stdout. Lazy-init: spawned only on first embed() call.
//
// Graceful fallback: if Python missing OR sentence-transformers not installed
// OR sidecar handshake fails -> available()=false; callers MUST check.
//
// Cross-platform: Windows uses CreatePipe + CreateProcess; POSIX uses pipe + fork.

#include "embedder.hpp"
#include "../core/logger.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <filesystem>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #include <sys/types.h>
  #include <signal.h>
  #include <fcntl.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::embed {

namespace {

// Locate sidecar script: env override, then alongside binary, then ~/.icmg/.
std::string findScript() {
    if (const char* env = std::getenv("ICMG_EMBED_SCRIPT")) {
        if (env[0] && fs::exists(env)) return env;
    }
#ifdef _WIN32
    char buf[1024]; DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    fs::path bin(std::string(buf, n));
#else
    fs::path bin = fs::canonical("/proc/self/exe");
#endif
    fs::path candidate = bin.parent_path() / "icmg_embedder.py";
    if (fs::exists(candidate)) return candidate.string();
    candidate = bin.parent_path() / "embed" / "icmg_embedder.py";
    if (fs::exists(candidate)) return candidate.string();
    candidate = fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE")
                       : std::getenv("HOME") ? std::getenv("HOME") : ".")
              / ".icmg" / "icmg_embedder.py";
    if (fs::exists(candidate)) return candidate.string();
    return "";
}

// Locate `python` / `python3` interpreter.
std::string findPython() {
    if (const char* env = std::getenv("ICMG_PYTHON")) {
        if (env[0]) return env;
    }
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}

class PythonSidecar : public Embedder {
public:
    PythonSidecar() { spawn(); }
    ~PythonSidecar() override { shutdown(); }

    bool        available() const override { return alive_; }
    int         dim() const override       { return dim_; }
    std::string model() const override     { return model_; }

    std::vector<float> embed(const std::string& text) override {
        if (!alive_) return {};
        json req = {{"op", "embed"}, {"id", ++req_id_}, {"text", text}};
        if (!sendLine(req.dump())) { shutdown(); return {}; }
        std::string line;
        if (!readLine(line)) { shutdown(); return {}; }
        try {
            json resp = json::parse(line);
            if (resp.contains("error")) { shutdown(); return {}; }
            auto vec = resp["vec"].get<std::vector<float>>();
            return vec;
        } catch (...) { shutdown(); return {}; }
    }

private:
    bool alive_ = false;
    int  dim_   = 384;
    std::string model_ = "all-MiniLM-L6-v2";
    std::atomic<int64_t> req_id_{0};

#ifdef _WIN32
    HANDLE child_in_w_ = nullptr;
    HANDLE child_out_r_ = nullptr;
    PROCESS_INFORMATION pi_{};
#else
    int  in_fd_ = -1;
    int  out_fd_ = -1;
    pid_t pid_  = -1;
#endif

    void spawn() {
        std::string script = findScript();
        std::string py     = findPython();
        if (script.empty()) {
            core::Logger::instance().warn("embed: sidecar script not found; semantic recall disabled");
            return;
        }

#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
        HANDLE in_r=nullptr, in_w=nullptr, out_r=nullptr, out_w=nullptr;
        if (!CreatePipe(&in_r, &in_w, &sa, 0))   return;
        if (!CreatePipe(&out_r, &out_w, &sa, 0)) { CloseHandle(in_r); CloseHandle(in_w); return; }
        SetHandleInformation(in_w,  HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{}; si.cb = sizeof(si);
        si.dwFlags     = STARTF_USESTDHANDLES;
        si.hStdInput   = in_r;
        si.hStdOutput  = out_w;
        si.hStdError   = GetStdHandle(STD_ERROR_HANDLE);

        std::string cmd = "\"" + py + "\" -u \"" + script + "\"";
        std::vector<char> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
        BOOL ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi_);
        CloseHandle(in_r); CloseHandle(out_w);
        if (!ok) {
            CloseHandle(in_w); CloseHandle(out_r);
            core::Logger::instance().warn("embed: failed to spawn python sidecar");
            return;
        }
        child_in_w_  = in_w;
        child_out_r_ = out_r;
#else
        int in_pipe[2], out_pipe[2];
        if (pipe(in_pipe)  < 0) return;
        if (pipe(out_pipe) < 0) { close(in_pipe[0]); close(in_pipe[1]); return; }
        pid_t pid = fork();
        if (pid < 0) {
            close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]);
            return;
        }
        if (pid == 0) {
            dup2(in_pipe[0],  0);
            dup2(out_pipe[1], 1);
            close(in_pipe[0]);  close(in_pipe[1]);
            close(out_pipe[0]); close(out_pipe[1]);
            execlp(py.c_str(), py.c_str(), "-u", script.c_str(), (char*)nullptr);
            _exit(127);
        }
        close(in_pipe[0]); close(out_pipe[1]);
        in_fd_  = in_pipe[1];
        out_fd_ = out_pipe[0];
        pid_    = pid;
#endif

        // Handshake: read first line — should be {"op":"ready","dim":384,"model":"..."}
        std::string line;
        if (!readLine(line)) { shutdown(); return; }
        try {
            json j = json::parse(line);
            if (j.value("op", "") != "ready") { shutdown(); return; }
            dim_   = j.value("dim", 384);
            model_ = j.value("model", "all-MiniLM-L6-v2");
            alive_ = true;
        } catch (...) { shutdown(); return; }
    }

    bool sendLine(const std::string& s) {
        std::string out = s + "\n";
#ifdef _WIN32
        DWORD written = 0;
        return WriteFile(child_in_w_, out.data(), (DWORD)out.size(), &written, nullptr) && written == out.size();
#else
        ssize_t n = write(in_fd_, out.data(), out.size());
        return n == (ssize_t)out.size();
#endif
    }

    bool readLine(std::string& line) {
        line.clear();
        char c;
        for (int i = 0; i < 1 << 20; ++i) {
#ifdef _WIN32
            DWORD got = 0;
            if (!ReadFile(child_out_r_, &c, 1, &got, nullptr) || got == 0) return !line.empty();
#else
            ssize_t r = read(out_fd_, &c, 1);
            if (r <= 0) return !line.empty();
#endif
            if (c == '\n') return true;
            if (c != '\r') line.push_back(c);
        }
        return false;
    }

    void shutdown() {
        alive_ = false;
#ifdef _WIN32
        if (child_in_w_)  { CloseHandle(child_in_w_);  child_in_w_  = nullptr; }
        if (child_out_r_) { CloseHandle(child_out_r_); child_out_r_ = nullptr; }
        if (pi_.hProcess) {
            TerminateProcess(pi_.hProcess, 0);
            CloseHandle(pi_.hProcess); CloseHandle(pi_.hThread);
            pi_.hProcess = nullptr;
        }
#else
        if (in_fd_  >= 0) { close(in_fd_);  in_fd_  = -1; }
        if (out_fd_ >= 0) { close(out_fd_); out_fd_ = -1; }
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            int st; waitpid(pid_, &st, 0);
            pid_ = -1;
        }
#endif
    }
};

} // anon

std::unique_ptr<Embedder> makeEmbedder() {
    auto e = std::make_unique<PythonSidecar>();
    if (!e->available()) return nullptr;
    return e;
}

} // namespace icmg::embed
