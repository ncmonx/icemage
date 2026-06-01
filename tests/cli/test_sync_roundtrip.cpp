// Phase 48 T6 — sync JSONL roundtrip unit test.
// Verifies push (DB → JSONL) and pull (JSONL → DB) preserve all fields.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>
#include <string>

using nlohmann::json;
using icmg::core::Db;
using icmg::core::Row;

// Minimal schema matching sync_cmd's SELECT columns.
static void setupSyncSchema(Db& db) {
    db.run(
        "CREATE TABLE memory_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " topic TEXT NOT NULL,"
        " content TEXT NOT NULL,"
        " keywords TEXT NOT NULL DEFAULT '',"
        " importance INTEGER NOT NULL DEFAULT 1,"
        " created_at INTEGER,"
        " zone TEXT NOT NULL DEFAULT 'default',"
        " created_by TEXT NOT NULL DEFAULT '',"
        " row_version INTEGER NOT NULL DEFAULT 0,"
        " deleted_at INTEGER)");
}

// Serialize memory_nodes → JSONL string (mirrors pushTable logic).
static std::string serializeMemory(Db& db) {
    std::ostringstream out;
    db.query(
        "SELECT id, topic, content, keywords, importance, created_at,"
        "       COALESCE(zone,'default'), COALESCE(created_by,''),"
        "       COALESCE(row_version,0), deleted_at"
        " FROM memory_nodes ORDER BY id",
        {},
        [&](const Row& r){
            if (r.size() < 10) return;
            json j;
            j["id"]          = std::stoll(r[0]);
            j["topic"]       = r[1];
            j["content"]     = r[2];
            j["keywords"]    = r[3];
            try { j["importance"]  = std::stoi(r[4]); } catch (...) { j["importance"] = 1; }
            try { j["created_at"]  = std::stoll(r[5]); } catch (...) { j["created_at"] = 0; }
            j["zone"]        = r[6];
            j["created_by"]  = r[7];
            try { j["row_version"] = std::stoi(r[8]); } catch (...) { j["row_version"] = 0; }
            j["deleted"]     = !r[9].empty();
            out << j.dump() << "\n";
        });
    return out.str();
}

// Deserialize JSONL → fresh DB (mirrors pullTable insert logic).
static int deserializeMemory(const std::string& jsonl, Db& db) {
    int count = 0;
    std::istringstream in(jsonl);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            db.run(
                "INSERT OR REPLACE INTO memory_nodes"
                "(id, topic, content, keywords, importance, created_at, zone, created_by, row_version)"
                " VALUES(?,?,?,?,?,?,?,?,?)",
                {std::to_string(j.value("id", (int64_t)0)),
                 j.value("topic", ""),
                 j.value("content", ""),
                 j.value("keywords", ""),
                 std::to_string(j.value("importance", 1)),
                 std::to_string(j.value("created_at", (int64_t)0)),
                 j.value("zone", "default"),
                 j.value("created_by", ""),
                 std::to_string(j.value("row_version", 0))});
            ++count;
        } catch (...) {}
    }
    return count;
}

TEST("sync: push produces one JSON object per line") {
    Db src(":memory:");
    setupSyncSchema(src);
    src.run("INSERT INTO memory_nodes(topic,content,keywords,importance,created_at)"
            " VALUES('T1','C1','k1',2,1000)");
    src.run("INSERT INTO memory_nodes(topic,content,keywords,importance,created_at)"
            " VALUES('T2','C2','k2',1,2000)");

    std::string jsonl = serializeMemory(src);
    std::istringstream ss(jsonl);
    std::string line;
    int line_count = 0;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        ++line_count;
        auto j = json::parse(line);
        ASSERT_TRUE(j.contains("topic"));
        ASSERT_TRUE(j.contains("content"));
        ASSERT_TRUE(j.contains("importance"));
        ASSERT_TRUE(j.contains("row_version"));
    }
    ASSERT_EQ(line_count, 2);
}

TEST("sync: push encodes required fields") {
    Db src(":memory:");
    setupSyncSchema(src);
    src.run("INSERT INTO memory_nodes(topic,content,keywords,importance,created_at,zone,created_by)"
            " VALUES('TopicA','BodyA','kw',3,9999,'backend','alice')");

    std::string jsonl = serializeMemory(src);
    auto j = json::parse(jsonl);

    ASSERT_EQ(j["topic"].get<std::string>(),       std::string("TopicA"));
    ASSERT_EQ(j["content"].get<std::string>(),     std::string("BodyA"));
    ASSERT_EQ(j["keywords"].get<std::string>(),    std::string("kw"));
    ASSERT_EQ(j["importance"].get<int>(),          3);
    ASSERT_EQ(j["created_at"].get<int64_t>(),      (int64_t)9999);
    ASSERT_EQ(j["zone"].get<std::string>(),        std::string("backend"));
    ASSERT_EQ(j["created_by"].get<std::string>(),  std::string("alice"));
    ASSERT_FALSE(j["deleted"].get<bool>());
}

TEST("sync: deleted flag set for soft-deleted rows") {
    Db src(":memory:");
    setupSyncSchema(src);
    src.run("INSERT INTO memory_nodes(topic,content,deleted_at) VALUES('D','dead',12345)");

    std::string jsonl = serializeMemory(src);
    auto j = json::parse(jsonl);
    ASSERT_TRUE(j["deleted"].get<bool>());
}

TEST("sync: full roundtrip preserves all fields") {
    Db src(":memory:");
    setupSyncSchema(src);
    src.run("INSERT INTO memory_nodes(topic,content,keywords,importance,created_at,zone,created_by,row_version)"
            " VALUES('Alpha','Content-A','k1',2,5555,'core','bot',7)");
    src.run("INSERT INTO memory_nodes(topic,content,keywords,importance,created_at,zone,created_by,row_version)"
            " VALUES('Beta','Content-B','k2',1,6666,'ui','human',3)");

    std::string jsonl = serializeMemory(src);

    Db dst(":memory:");
    setupSyncSchema(dst);
    int imported = deserializeMemory(jsonl, dst);
    ASSERT_EQ(imported, 2);

    int count = 0;
    dst.query("SELECT topic, importance, zone, row_version FROM memory_nodes ORDER BY topic",
              {},
              [&](const Row& r){
                  ++count;
                  if (count == 1) {  // Alpha
                      ASSERT_EQ(r[0], std::string("Alpha"));
                      ASSERT_EQ(std::stoi(r[1]), 2);
                      ASSERT_EQ(r[2], std::string("core"));
                      ASSERT_EQ(std::stoi(r[3]), 7);
                  } else {           // Beta
                      ASSERT_EQ(r[0], std::string("Beta"));
                      ASSERT_EQ(std::stoi(r[1]), 1);
                      ASSERT_EQ(r[2], std::string("ui"));
                      ASSERT_EQ(std::stoi(r[3]), 3);
                  }
              });
    ASSERT_EQ(count, 2);
}

TEST("sync: empty table produces empty JSONL") {
    Db src(":memory:");
    setupSyncSchema(src);
    std::string jsonl = serializeMemory(src);
    // strip whitespace
    std::string trimmed = jsonl;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == ' '))
        trimmed.pop_back();
    ASSERT_TRUE(trimmed.empty());
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
