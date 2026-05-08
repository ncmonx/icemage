// Phase 38 T1: `icmg serve` — minimal embedded HTTP server.
//
// No external HTTP lib. Cross-platform via socket(2) + accept loop. Blocking
// single-threaded server (icmg is single-user; no concurrency goal).
//
// Endpoints:
//   GET /                 -> bundled HTML (read-only dashboard)
//   GET /api/audit        -> wiki audit metrics JSON
//   GET /api/memory?n=20  -> last N memory_nodes
//   GET /api/graph?n=50   -> top N graph_nodes (file kind)
//   GET /api/recall?q=X   -> BM25 recall (text-only path; no semantic)
//
// Frontend uses textContent + createElement (no innerHTML / no XSS surface).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  static int closesocket_compat(socket_t s) { return closesocket(s); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using socket_t = int;
  static int closesocket_compat(socket_t s) { return close(s); }
#endif

namespace icmg::cli {

// Inline HTML/JS — frontend uses textContent + createElement (XSS-safe).
static const char* INDEX_HTML = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>icmg dashboard</title><style>
body{font:14px sans-serif;max-width:1100px;margin:1.5em auto;padding:0 1em;color:#222}
h1{border-bottom:2px solid #eee;padding-bottom:.3em}
.card{border:1px solid #ddd;border-radius:6px;padding:1em;margin:1em 0;background:#f9f9f9}
table{border-collapse:collapse;width:100%;margin:.5em 0}
th,td{border:1px solid #ddd;padding:.4em .8em;text-align:left;font-size:13px}
th{background:#f0f0f0}
.metric{display:inline-block;margin-right:1.5em}
.num{font-size:1.8em;font-weight:600;color:#0366d6}
.lbl{color:#666;font-size:.9em;margin-left:.3em}
input{padding:.4em;border:1px solid #ccc;border-radius:4px;width:60%}
button{padding:.4em 1em;background:#0366d6;color:#fff;border:0;border-radius:4px;cursor:pointer}
.muted{color:#666;font-size:.85em}
</style></head><body>
<h1>icmg dashboard</h1>
<p class="muted">Read-only view of memory + graph + audit. Live data from local DB.</p>

<div class="card"><h2>Audit</h2><div id="audit">loading...</div></div>

<div class="card"><h2>Recall</h2>
<input id="q" placeholder="search memory..."><button id="b">Search</button>
<div id="recall-out"></div></div>

<div class="card"><h2>Recent memory (last 20)</h2><div id="memory">loading...</div></div>
<div class="card"><h2>Top graph nodes</h2><div id="graph">loading...</div></div>

<script>
async function fetchJ(url){return (await fetch(url)).json()}

function makeCell(text){
  const td = document.createElement('td');
  td.textContent = text == null ? '' : String(text);
  return td;
}
function makeTable(headers, rows){
  const t = document.createElement('table');
  const hr = document.createElement('tr');
  for (const h of headers) {
    const th = document.createElement('th');
    th.textContent = h;
    hr.appendChild(th);
  }
  t.appendChild(hr);
  for (const r of rows) {
    const tr = document.createElement('tr');
    for (const cell of r) tr.appendChild(makeCell(cell));
    t.appendChild(tr);
  }
  return t;
}
function clearAndAppend(elemId, child){
  const el = document.getElementById(elemId);
  while (el.firstChild) el.removeChild(el.firstChild);
  el.appendChild(child);
}

async function loadAudit(){
  const j = await fetchJ('/api/audit');
  const wrap = document.createElement('div');
  for (const [k, v] of Object.entries(j)) {
    const span = document.createElement('span');
    span.className = 'metric';
    const num = document.createElement('span');
    num.className = 'num';
    num.textContent = v;
    const lbl = document.createElement('span');
    lbl.className = 'lbl';
    lbl.textContent = k;
    span.appendChild(num);
    span.appendChild(lbl);
    wrap.appendChild(span);
  }
  clearAndAppend('audit', wrap);
}

async function loadMemory(){
  const j = await fetchJ('/api/memory?n=20');
  const rows = j.map(n => [n.id, n.topic, n.importance, n.zone]);
  clearAndAppend('memory', makeTable(['id','topic','imp','zone'], rows));
}

async function loadGraph(){
  const j = await fetchJ('/api/graph?n=30');
  const rows = j.map(n => [n.id, n.path, n.kind || '']);
  clearAndAppend('graph', makeTable(['id','path','kind'], rows));
}

async function recall(){
  const q = encodeURIComponent(document.getElementById('q').value);
  const j = await fetchJ('/api/recall?q=' + q);
  const rows = j.map(n => [n.id, n.topic, (n.content || '').substring(0, 140)]);
  clearAndAppend('recall-out', makeTable(['id','topic','content'], rows));
}

document.getElementById('b').addEventListener('click', recall);
loadAudit(); loadMemory(); loadGraph();
</script>
</body></html>
)HTML";

class ServeCommand : public BaseCommand {
public:
    std::string name()        const override { return "serve"; }
    std::string description() const override { return "Embedded HTTP dashboard (read-only) — no JS framework deps"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg serve [--port N] [--host BIND] [--no-open]\n\n"
            "Options:\n"
            "  --port N        Port (default 8080)\n"
            "  --host H        Bind address (default 127.0.0.1)\n"
            "  --no-open       Don't auto-open browser\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        int port = 8080;
        try { port = std::stoi(flagValue(args, "--port", "8080")); } catch (...) {}
        std::string host = flagValue(args, "--host", "127.0.0.1");
        bool no_open = hasFlag(args, "--no-open");

#ifdef _WIN32
        WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) { std::cerr << "serve: socket() failed\n"; return 1; }
        int yes = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "serve: bind " << host << ":" << port << " failed\n";
            closesocket_compat(s); return 1;
        }
        if (listen(s, 8) < 0) { std::cerr << "serve: listen failed\n"; return 1; }
        std::cout << "icmg dashboard: http://" << host << ":" << port << "/\n";
        if (!no_open) tryOpenBrowser("http://" + host + ":" + std::to_string(port) + "/");
        std::cout << "Ctrl-C to stop.\n";

        auto& cfg = core::Config::instance();
        while (true) {
            sockaddr_in cli{}; socklen_t cli_len = sizeof(cli);
            socket_t c = accept(s, (sockaddr*)&cli, &cli_len);
            if (c < 0) continue;
            handleConnection(c, cfg);
            closesocket_compat(c);
        }
    }

private:
    static void tryOpenBrowser(const std::string& url) {
#ifdef _WIN32
        std::string cmd = "start \"\" \"" + url + "\"";
        std::system(cmd.c_str());
#elif defined(__APPLE__)
        std::system(("open " + url).c_str());
#else
        std::system(("xdg-open " + url + " 2>/dev/null &").c_str());
#endif
    }

    void handleConnection(socket_t c, core::Config& cfg) {
        std::string buf; buf.resize(8192);
        int got = recv(c, &buf[0], (int)buf.size() - 1, 0);
        if (got <= 0) return;
        buf.resize(got);
        auto sp1 = buf.find(' ');
        auto sp2 = buf.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) {
            sendResp(c, 400, "text/plain", "bad request"); return;
        }
        std::string path = buf.substr(sp1 + 1, sp2 - sp1 - 1);
        std::string body, ctype = "text/html";

        if (path == "/" || path == "/index.html") {
            body = INDEX_HTML;
        } else if (path.rfind("/api/audit", 0) == 0) {
            body = apiAudit(cfg); ctype = "application/json";
        } else if (path.rfind("/api/memory", 0) == 0) {
            body = apiMemory(cfg, parseN(path)); ctype = "application/json";
        } else if (path.rfind("/api/graph", 0) == 0) {
            body = apiGraph(cfg, parseN(path)); ctype = "application/json";
        } else if (path.rfind("/api/recall", 0) == 0) {
            body = apiRecall(cfg, parseQ(path)); ctype = "application/json";
        } else {
            sendResp(c, 404, "text/plain", "not found"); return;
        }
        sendResp(c, 200, ctype, body);
    }

    static int parseN(const std::string& path) {
        auto q = path.find("n=");
        if (q == std::string::npos) return 20;
        try { return std::stoi(path.substr(q + 2)); } catch (...) { return 20; }
    }
    static std::string parseQ(const std::string& path) {
        auto q = path.find("q=");
        if (q == std::string::npos) return "";
        std::string raw = path.substr(q + 2);
        std::string out;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '+') out.push_back(' ');
            else if (raw[i] == '%' && i + 2 < raw.size()) {
                int v = 0; std::sscanf(raw.c_str() + i + 1, "%2x", &v);
                out.push_back((char)v); i += 2;
            } else out.push_back(raw[i]);
        }
        return out;
    }

    std::string apiAudit(core::Config& cfg) {
        core::Db db(cfg.projectDbPath("."));
        auto get = [&](const std::string& sql){
            int n=0; try { db.query(sql, {}, [&](const core::Row& r){
                if (!r.empty()) n = std::stoi(r[0]);
            }); } catch (...) {}
            return n;
        };
        int files     = get("SELECT COUNT(*) FROM graph_nodes WHERE kind='file' OR kind IS NULL");
        int sps       = get("SELECT COUNT(*) FROM graph_nodes WHERE kind='sp'");
        int tables    = get("SELECT COUNT(*) FROM graph_nodes WHERE kind='table'");
        int mem       = get("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL");
        int embed_mem = get("SELECT COUNT(*) FROM embeddings WHERE kind='memory'");
        std::ostringstream o;
        o << "{\"files\":" << files << ",\"sps\":" << sps << ",\"tables\":" << tables
          << ",\"memory\":" << mem << ",\"embed_memory\":" << embed_mem << "}";
        return o.str();
    }
    std::string apiMemory(core::Config& cfg, int n) {
        core::Db db(cfg.projectDbPath("."));
        std::ostringstream o; o << "[";
        bool first = true;
        db.query("SELECT id,topic,importance,zone FROM memory_nodes "
                 "WHERE deleted_at IS NULL ORDER BY last_used DESC LIMIT ?",
                 {std::to_string(n)},
                 [&](const core::Row& r){
                     if (r.size() < 4) return;
                     if (!first) o << ","; first = false;
                     o << "{\"id\":" << r[0]
                       << ",\"topic\":\"" << esc(r[1])
                       << "\",\"importance\":" << r[2]
                       << ",\"zone\":\"" << esc(r[3]) << "\"}";
                 });
        o << "]"; return o.str();
    }
    std::string apiGraph(core::Config& cfg, int n) {
        core::Db db(cfg.projectDbPath("."));
        std::ostringstream o; o << "[";
        bool first = true;
        db.query("SELECT id,path,COALESCE(kind,'file') FROM graph_nodes "
                 "WHERE parent_id IS NULL ORDER BY id DESC LIMIT ?",
                 {std::to_string(n)},
                 [&](const core::Row& r){
                     if (r.size() < 3) return;
                     if (!first) o << ","; first = false;
                     o << "{\"id\":" << r[0] << ",\"path\":\"" << esc(r[1])
                       << "\",\"kind\":\"" << esc(r[2]) << "\"}";
                 });
        o << "]"; return o.str();
    }
    std::string apiRecall(core::Config& cfg, const std::string& q) {
        if (q.empty()) return "[]";
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto results = store.recall(q, 10, false);
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i) o << ",";
            auto& m = results[i];
            o << "{\"id\":" << m.id << ",\"topic\":\"" << esc(m.topic)
              << "\",\"content\":\"" << esc(m.content) << "\"}";
        }
        o << "]"; return o.str();
    }

    static std::string esc(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c < 0x20) {/* skip ctrl */}
            else out.push_back(c);
        }
        return out;
    }

    void sendResp(socket_t c, int code, const std::string& ctype, const std::string& body) {
        std::ostringstream h;
        h << "HTTP/1.1 " << code << " OK\r\n"
          << "Content-Type: " << ctype << "; charset=utf-8\r\n"
          << "Content-Length: " << body.size() << "\r\n"
          << "Connection: close\r\n\r\n" << body;
        std::string s = h.str();
        send(c, s.data(), (int)s.size(), 0);
    }
};

ICMG_REGISTER_COMMAND("serve", ServeCommand);

} // namespace icmg::cli
