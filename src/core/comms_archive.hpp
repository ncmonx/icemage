// 2026-06-06: append-only durable comms archive (shared-path JSONL). #comms
// Parallel to the live wire (msg.tsv); this is NEVER truncated, so inter-instance
// dialogue survives. Cross-instance => lives on the shared bridge path, NOT persona DB.
#pragma once
#include <string>
#include <vector>

namespace icmg::core {

struct CommsRow { std::string from, to, body; };

void commsAppend(const std::string& path, const std::string& from,
                 const std::string& to, const std::string& body);
std::vector<CommsRow> commsRead(const std::string& path);

}
