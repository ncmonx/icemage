#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace icmg::llm {

struct PipeConfig {
    std::string name = "icmg-llm-warm";
    int max_instances = 4;
    int buffer_size = 1024 * 1024;  // 1 MB
};

class PipeServer {
public:
    struct Connection;
    explicit PipeServer(const PipeConfig& cfg);
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    // Block until client connects or stop_token requested. nullopt on stop.
    std::optional<std::shared_ptr<Connection>> accept(std::stop_token tok);

    // Read newline-delimited JSON from conn. Returns empty on EOF/error.
    std::string readMessage(Connection& c);

    // Write newline-delimited JSON response.
    bool writeMessage(Connection& c, const std::string& json);

    // Disconnect + recycle instance for next accept.
    void disconnect(Connection& c);

    // Signal stop; outstanding accept() returns nullopt.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PipeClient {
public:
    static std::optional<PipeClient> connect(
        const std::string& name,
        std::chrono::milliseconds timeout);
    ~PipeClient();
    PipeClient(PipeClient&&) noexcept;
    PipeClient& operator=(PipeClient&&) noexcept;

    // Single round-trip. Returns response JSON or empty on failure.
    std::string sendRequest(const std::string& json_req,
                            std::chrono::milliseconds read_timeout
                                = std::chrono::milliseconds(60000));

private:
    PipeClient();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Format platform-specific full pipe path from name.
std::string fullPipePath(const std::string& name);

}  // namespace icmg::llm
