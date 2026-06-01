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
#include "../../core/json_safe.hpp"   // v1.68.1 safeDump
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include "../../imem/memory_store.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <shellapi.h>
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

// v0.42.0: Knowledge browser — context_nodes CRUD dashboard.
static const char* KNOWLEDGE_HTML = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>icmg dashboard</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh}
.hdr{background:#161b22;border-bottom:1px solid #30363d;padding:.75em 1.5em;display:flex;align-items:center;gap:1em}
.hdr h1{font-size:1.1em;font-weight:700;color:#f0f6fc;letter-spacing:-.01em}
.hdr .ver{font-size:.8em;color:#8b949e;margin-left:.3em}
.kpi-bar{display:flex;gap:1em;padding:.75em 1.5em;background:#161b22;border-bottom:1px solid #21262d}
.kpi{background:#0d1117;border:1px solid #21262d;border-radius:6px;padding:.5em .9em;display:flex;flex-direction:column;min-width:110px}
.kpi .n{font-size:1.6em;font-weight:700;color:#58a6ff;line-height:1.1}
.kpi .l{font-size:.75em;color:#8b949e;margin-top:.1em}
.tabs{display:flex;gap:0;padding:0 1.5em;background:#161b22;border-bottom:1px solid #30363d}
.tab{padding:.6em 1.2em;cursor:pointer;border-bottom:2px solid transparent;color:#8b949e;font-size:.9em;transition:color .15s}
.tab:hover{color:#c9d1d9}
.tab.active{color:#58a6ff;border-color:#58a6ff}
.panel{display:none;padding:1.2em 1.5em}.panel.active{display:block}
.toolbar{display:flex;gap:.6em;align-items:center;margin-bottom:.8em;flex-wrap:wrap}
.toolbar input{background:#0d1117;border:1px solid #30363d;border-radius:6px;padding:.4em .7em;color:#c9d1d9;font-size:.85em;flex:1;min-width:200px}
.toolbar input:focus{outline:none;border-color:#58a6ff}
.toolbar label{color:#8b949e;font-size:.85em;white-space:nowrap;display:flex;align-items:center;gap:.3em}
.btn{padding:.35em .8em;border:1px solid;border-radius:6px;cursor:pointer;font-size:.82em;font-weight:500;white-space:nowrap;transition:background .15s}
.btn-primary{background:#238636;border-color:#2ea043;color:#fff}.btn-primary:hover{background:#2ea043}
.btn-info{background:#1f6feb;border-color:#388bfd;color:#fff}.btn-info:hover{background:#388bfd}
.btn-warn{background:#b08800;border-color:#d29922;color:#fff}.btn-warn:hover{background:#d29922}
.btn-danger{background:#b91c1c;border-color:#f85149;color:#fff}.btn-danger:hover{background:#f85149}
.btn-muted{background:#21262d;border-color:#30363d;color:#8b949e}.btn-muted:hover{background:#30363d}
table{border-collapse:collapse;width:100%;font-size:.84em}
th{background:#161b22;color:#8b949e;font-weight:600;text-transform:uppercase;font-size:.75em;letter-spacing:.04em;padding:.5em .8em;text-align:left;border-bottom:1px solid #21262d;white-space:nowrap}
td{padding:.45em .8em;border-bottom:1px solid #161b22;vertical-align:top}
tr:hover td{background:#161b22}
.tier-hot td:first-child{border-left:2px solid #f78166}
.tier-cold td:first-child{border-left:2px solid #58a6ff}
.tier-skill td:first-child{border-left:2px solid #3fb950}
.inactive{opacity:.45}
.badge{display:inline-block;padding:.15em .5em;border-radius:3px;font-size:.75em;font-weight:600}
.badge-hot{background:#3d1a1a;color:#f78166}.badge-cold{background:#1c3252;color:#58a6ff}
.badge-skill{background:#1a3d27;color:#3fb950}.badge-rule{background:#2d2a1a;color:#e3b341}
.badge-on{background:#1a3d27;color:#3fb950}.badge-off{background:#2d1a1a;color:#8b949e}
.acts{display:flex;gap:.3em;flex-wrap:nowrap}
/* Modal */
#modal{display:none;position:fixed;inset:0;background:rgba(1,4,9,.8);z-index:100;align-items:flex-start;justify-content:center;padding-top:5%}
#modal.open{display:flex}
.mbox{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:1.5em;width:min(580px,92vw);max-height:80vh;overflow-y:auto}
.mbox h2{font-size:1em;font-weight:700;color:#f0f6fc;margin-bottom:1em}
.frow{margin-bottom:.9em}
.frow label{display:block;font-size:.8em;font-weight:600;color:#8b949e;margin-bottom:.3em}
.frow input,.frow select,.frow textarea{width:100%;background:#0d1117;border:1px solid #30363d;border-radius:6px;padding:.4em .7em;color:#c9d1d9;font-size:.85em}
.frow input:focus,.frow select,.frow textarea:focus{outline:none;border-color:#58a6ff}
.frow textarea{height:110px;font-family:monospace;resize:vertical}
.fbtns{display:flex;gap:.5em;margin-top:1.2em}
.ferr{color:#f85149;font-size:.82em;margin-top:.4em;min-height:1.2em}
.empty{text-align:center;color:#8b949e;padding:2em;font-size:.9em}
</style></head><body>
<div class="hdr"><h1>icmg<span class="ver">dashboard</span></h1></div>
<div class="kpi-bar" id="kpis"></div>
<div class="tabs">
  <div class="tab active" data-panel="p-knowledge">Knowledge</div>
  <div class="tab" data-panel="p-skills">Skills</div>
  <div class="tab" data-panel="p-rules">Rules</div>
</div>
<div class="panel active" id="p-knowledge">
  <div class="toolbar">
    <input id="k-search" placeholder="Filter knowledge nodes...">
    <label><input type="checkbox" id="k-inactive" onchange="loadKnowledge()"> Show inactive</label>
    <div class="btn btn-primary" onclick="openAdd('knowledge')">+ Add</div>
  </div>
  <table><thead><tr><th>Key</th><th>Tier</th><th>Title</th><th style="width:28%">Content</th><th>Active</th><th></th></tr></thead>
  <tbody id="k-tbody"></tbody></table>
</div>
<div class="panel" id="p-skills">
  <div class="toolbar">
    <input id="s-search" placeholder="Filter skills...">
    <label><input type="checkbox" id="s-inactive" onchange="loadSkills()"> Show inactive</label>
    <div class="btn btn-primary" onclick="openAdd('skill')">+ Add skill</div>
  </div>
  <table><thead><tr><th>Key</th><th>Title</th><th style="width:35%">Content</th><th>Active</th><th></th></tr></thead>
  <tbody id="s-tbody"></tbody></table>
</div>
<div class="panel" id="p-rules">
  <div class="toolbar">
    <input id="r-search" placeholder="Filter rules...">
    <label><input type="checkbox" id="r-inactive" onchange="loadRules()"> Show inactive</label>
  </div>
  <table><thead><tr><th>Name</th><th>Type</th><th>Priority</th><th style="width:35%">Content</th><th>Active</th><th></th></tr></thead>
  <tbody id="r-tbody"></tbody></table>
</div>
<div id="modal" onclick="if(event.target===this)closeModal()">
  <div class="mbox" id="mbox"></div>
</div>
<script>
// ── Tabs ─────────────────────────────────────────────────────────────────────
document.querySelectorAll('.tab').forEach(function(t){
  t.onclick=function(){
    document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('active')});
    document.querySelectorAll('.panel').forEach(function(x){x.classList.remove('active')});
    t.classList.add('active');
    document.getElementById(t.dataset.panel).classList.add('active');
  };
});
// ── Helpers ──────────────────────────────────────────────────────────────────
function fetchJ(u,opts){return fetch(u,opts).then(function(r){return r.json();});}
function ce(tag,cls){var e=document.createElement(tag);if(cls)e.className=cls;return e;}
function tx(e,t){e.textContent=t;return e;}
function badge(text,cls){return tx(ce('span','badge badge-'+cls),text);}
function mkBtn(cls,label,fn){var b=ce('button','btn '+cls);b.textContent=label;b.onclick=fn;return b;}
function clear(id){var e=document.getElementById(id);while(e.firstChild)e.removeChild(e.firstChild);return e;}
// ── KPIs ─────────────────────────────────────────────────────────────────────
function loadKpis(){
  var kpiEl=document.getElementById('kpis');
  function kpi(n,l,cl){
    var d=ce('div','kpi');var num=ce('div','n');num.id='kpi-'+cl;num.textContent=n;
    var lbl=ce('div','l');lbl.textContent=l;d.appendChild(num);d.appendChild(lbl);kpiEl.appendChild(d);
  }
  kpi('…','Hot nodes','hot');kpi('…','Cold nodes','cold');kpi('…','Skills','skills');kpi('…','Rules','rules');
  // hot
  fetchJ('/api/knowledge/list?tier=hot').then(function(d){document.getElementById('kpi-hot').textContent=d.length;});
  fetchJ('/api/knowledge/list?tier=cold').then(function(d){document.getElementById('kpi-cold').textContent=d.length;});
  fetchJ('/api/knowledge/list?tier=skill').then(function(d){document.getElementById('kpi-skills').textContent=d.length;});
  fetchJ('/api/rules').then(function(d){document.getElementById('kpi-rules').textContent=d.length;});
}
// ── Knowledge tab ─────────────────────────────────────────────────────────────
var kNodes=[];
function loadKnowledge(){
  var inactive=document.getElementById('k-inactive').checked;
  fetchJ('/api/knowledge/list?tier='+(inactive?'&inactive=1':'')).then(function(d){
    kNodes=d;renderKnowledge(d);
  });
}
function renderKnowledge(data){
  var q=document.getElementById('k-search').value.toLowerCase();
  var tb=clear('k-tbody');
  var filtered=data.filter(function(n){
    return !q||n.key.includes(q)||n.title.toLowerCase().includes(q)||(n.content||'').toLowerCase().includes(q);
  });
  if(!filtered.length){var tr=ce('tr');var td=ce('td');td.colSpan=6;td.className='empty';td.textContent='No nodes found.';tr.appendChild(td);tb.appendChild(tr);return;}
  filtered.forEach(function(n){
    var tr=ce('tr','tier-'+n.tier+(n.active?'':' inactive'));
    function td(txt,max){var c=ce('td');c.textContent=max&&txt&&txt.length>max?txt.substr(0,max)+'…':txt||'';return c;}
    tr.appendChild(td(n.key));
    var tierTd=ce('td');tierTd.appendChild(badge(n.tier,n.tier));tr.appendChild(tierTd);
    tr.appendChild(td(n.title));tr.appendChild(td(n.content||'',90));
    var activeTd=ce('td');activeTd.appendChild(badge(n.active?'on':'off',n.active?'on':'off'));tr.appendChild(activeTd);
    var acts=ce('td');var div=ce('div','acts');
    div.appendChild(mkBtn('btn-info','Edit',function(){openEdit('knowledge',n);}));
    div.appendChild(mkBtn('btn-warn',n.active?'Disable':'Enable',function(){
      fetchJ('/api/knowledge/toggle?key='+encodeURIComponent(n.key)+'&active='+(n.active?'0':'1')).then(function(){loadKnowledge();loadKpis();});
    }));
    div.appendChild(mkBtn('btn-danger','Del',function(){
      if(confirm('Delete "'+n.key+'"?'))fetchJ('/api/knowledge/delete?key='+encodeURIComponent(n.key),{method:'DELETE'}).then(function(){loadKnowledge();loadKpis();});
    }));
    acts.appendChild(div);tr.appendChild(acts);tb.appendChild(tr);
  });
}
document.getElementById('k-search').oninput=function(){renderKnowledge(kNodes);};
// ── Skills tab ────────────────────────────────────────────────────────────────
var sNodes=[];
function loadSkills(){
  var inactive=document.getElementById('s-inactive').checked;
  fetchJ('/api/knowledge/list?tier=skill'+(inactive?'&inactive=1':'')).then(function(d){
    sNodes=d;renderSkills(d);
  });
}
function renderSkills(data){
  var q=document.getElementById('s-search').value.toLowerCase();
  var tb=clear('s-tbody');
  var filtered=data.filter(function(n){
    return !q||n.key.includes(q)||n.title.toLowerCase().includes(q)||(n.content||'').toLowerCase().includes(q);
  });
  if(!filtered.length){var tr=ce('tr');var td=ce('td');td.colSpan=5;td.className='empty';td.textContent='No skills indexed. Run: icmg skill index';tr.appendChild(td);tb.appendChild(tr);return;}
  filtered.forEach(function(n){
    var tr=ce('tr','tier-skill'+(n.active?'':' inactive'));
    function td(txt,max){var c=ce('td');c.textContent=max&&txt&&txt.length>max?txt.substr(0,max)+'…':txt||'';return c;}
    tr.appendChild(td(n.key));tr.appendChild(td(n.title));tr.appendChild(td(n.content||'',100));
    var activeTd=ce('td');activeTd.appendChild(badge(n.active?'on':'off',n.active?'on':'off'));tr.appendChild(activeTd);
    var acts=ce('td');var div=ce('div','acts');
    div.appendChild(mkBtn('btn-warn',n.active?'Disable':'Enable',function(){
      fetchJ('/api/knowledge/toggle?key='+encodeURIComponent(n.key)+'&active='+(n.active?'0':'1')).then(function(){loadSkills();loadKpis();});
    }));
    acts.appendChild(div);tr.appendChild(acts);tb.appendChild(tr);
  });
}
document.getElementById('s-search').oninput=function(){renderSkills(sNodes);};
// ── Rules tab ─────────────────────────────────────────────────────────────────
var rRules=[];
function loadRules(){
  fetchJ('/api/rules').then(function(d){rRules=d;renderRules(d);});
}
function renderRules(data){
  var q=document.getElementById('r-search').value.toLowerCase();
  var tb=clear('r-tbody');
  var inactive=document.getElementById('r-inactive').checked;
  var filtered=data.filter(function(n){
    if(!inactive&&!n.active)return false;
    return !q||n.name.toLowerCase().includes(q)||(n.content||'').toLowerCase().includes(q);
  });
  if(!filtered.length){var tr=ce('tr');var td=ce('td');td.colSpan=6;td.className='empty';td.textContent='No rules found. Add with: icmg rule add';tr.appendChild(td);tb.appendChild(tr);return;}
  filtered.forEach(function(n){
    var tr=ce('tr'+(n.active?'':' inactive'));
    function td(txt,max){var c=ce('td');c.textContent=max&&txt&&txt.length>max?txt.substr(0,max)+'…':txt||'';return c;}
    tr.appendChild(td(n.name));
    var typeTd=ce('td');typeTd.appendChild(badge(n.rule_type||'rule','rule'));tr.appendChild(typeTd);
    tr.appendChild(td(String(n.priority||0)));tr.appendChild(td(n.content||'',90));
    var activeTd=ce('td');activeTd.appendChild(badge(n.active?'on':'off',n.active?'on':'off'));tr.appendChild(activeTd);
    var acts=ce('td');var div=ce('div','acts');
    div.appendChild(mkBtn('btn-warn',n.active?'Disable':'Enable',function(){
      fetchJ('/api/rules/toggle?id='+n.id+'&active='+(n.active?'0':'1'),{method:'POST'}).then(function(){loadRules();loadKpis();});
    }));
    acts.appendChild(div);tr.appendChild(acts);tb.appendChild(tr);
  });
}
document.getElementById('r-search').oninput=function(){renderRules(rRules);};
document.getElementById('r-inactive').onchange=function(){renderRules(rRules);};
// ── Modal add/edit ────────────────────────────────────────────────────────────
function openAdd(mode){openEdit(mode,{tier:mode==='skill'?'skill':'cold'});}
function openEdit(mode,n){
  var box=clear('mbox');
  var h=ce('h2');h.textContent=(n.key?'Edit: '+n.key:'Add '+(mode==='skill'?'skill':'node'));box.appendChild(h);
  function row(lbl,el){var d=ce('div','frow');var l=ce('label');l.textContent=lbl;d.appendChild(l);d.appendChild(el);box.appendChild(d);return el;}
  var ft=ce('input');ft.value=n.title||'';row('Title',ft);
  var fs=ce('select');
  (mode==='skill'?['skill']:['hot','cold','skill']).forEach(function(t){
    var o=document.createElement('option');o.value=o.textContent=t;if(t===(n.tier||'cold'))o.selected=true;fs.appendChild(o);
  });
  row('Tier',fs);
  var fc=ce('textarea');fc.textContent=n.content||'';row('Content',fc);
  var ferr=ce('div','ferr');box.appendChild(ferr);
  var fbtns=ce('div','fbtns');
  fbtns.appendChild(mkBtn('btn-primary','Save',function(){
    var body={title:ft.value,content:fc.value,tier:fs.value};
    if(n.key)body.key=n.key;
    var ep=n.key?'/api/knowledge/update':'/api/knowledge/add';
    fetchJ(ep,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
      .then(function(r){
        if(r.error){ferr.textContent=r.error;return;}
        closeModal();if(mode==='skill')loadSkills();else loadKnowledge();loadKpis();
      }).catch(function(e){ferr.textContent=String(e);});
  }));
  fbtns.appendChild(mkBtn('btn-muted','Cancel',closeModal));
  box.appendChild(fbtns);
  document.getElementById('modal').classList.add('open');
}
function closeModal(){document.getElementById('modal').classList.remove('open');}
// ── Init ──────────────────────────────────────────────────────────────────────
loadKpis();loadKnowledge();loadSkills();loadRules();
</script></body></html>
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
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
        } else if (path == "/knowledge") {
            body = KNOWLEDGE_HTML; ctype = "text/html";
        } else if (path.rfind("/api/knowledge", 0) == 0) {
            body = apiKnowledge(cfg, buf.substr(0, sp1), path, buf); ctype = "application/json";
        } else if (path.rfind("/api/rules", 0) == 0) {
            body = apiRules(cfg, buf.substr(0, sp1), path); ctype = "application/json";
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

    // v0.42.0: knowledge REST API + HTML dashboard.
    // GET  /api/knowledge/list?tier=&inactive=1
    // GET  /api/knowledge/get?key=X
    // POST /api/knowledge/add     body: JSON node
    // PUT  /api/knowledge/update?key=X  body: JSON fields
    // DELETE /api/knowledge/delete?key=X
    std::string apiKnowledge(core::Config& cfg, const std::string& method,
                              const std::string& path, const std::string& raw_req) {
        using nlohmann::json;
        core::Db db(cfg.projectDbPath("."));
        core::ContextNodeStore store(db);

        // Extract subpath: /api/knowledge/list -> "list"
        std::string sub;
        {
            auto p = path.find("/api/knowledge/");
            if (p != std::string::npos) sub = path.substr(p + 15);
            auto q = sub.find('?'); if (q != std::string::npos) sub = sub.substr(0, q);
        }

        auto getParam = [&](const std::string& key) {
            auto q = path.find(key + "=");
            if (q == std::string::npos) return std::string("");
            std::string val;
            size_t i = q + key.size() + 1;
            while (i < path.size() && path[i] != '&' && path[i] != ' ') val += path[i++];
            return val;
        };

        // Extract request body (after double CRLF)
        std::string body_json;
        {
            auto pos = raw_req.find("\r\n\r\n");
            if (pos != std::string::npos) body_json = raw_req.substr(pos + 4);
        }

        if (sub == "list" || path.rfind("/api/knowledge/list", 0) == 0 ||
            path == "/api/knowledge") {
            std::string tier    = getParam("tier");
            bool inactive       = getParam("inactive") == "1";
            auto nodes = store.list(tier, !inactive);
            json arr = json::array();
            for (auto& n : nodes) {
                arr.push_back({{"key", n.node_key}, {"title", n.title},
                               {"tier", n.tier}, {"active", n.active},
                               {"source", n.source_file}, {"tags", n.tags},
                               {"content", n.content}});
            }
            return icmg::core::safeDump(arr);
        }

        if (sub == "get") {
            auto key = getParam("key");
            auto node = store.get(key);
            if (!node) return "{\"error\":\"not found\"}";
            json j = {{"key", node->node_key}, {"title", node->title},
                      {"tier", node->tier}, {"active", node->active},
                      {"source", node->source_file}, {"tags", node->tags},
                      {"content", node->content}};
            return icmg::core::safeDump(j);
        }

        if ((sub == "add" || sub == "update") && !body_json.empty()) {
            try {
                auto j = json::parse(body_json);
                core::ContextNode node;
                if (sub == "update") {
                    auto key = j.value("key", getParam("key"));
                    auto existing = store.get(key);
                    if (!existing) return "{\"error\":\"not found\"}";
                    node = *existing;
                }
                if (j.contains("title"))   node.title       = j["title"];
                if (j.contains("content")) node.content     = j["content"];
                if (j.contains("tier"))    node.tier        = j["tier"];
                if (j.contains("tags"))    node.tags        = j["tags"].is_string()
                    ? j["tags"].get<std::string>() : j["tags"].dump();
                if (j.contains("source"))  node.source_file = j["source"];
                if (j.contains("active"))  node.active      = j["active"].get<bool>();
                if (node.node_key.empty()) {
                    // Slugify title for new nodes
                    for (unsigned char c : node.title) {
                        if (std::isalnum(c)) node.node_key += static_cast<char>(std::tolower(c));
                        else if (!node.node_key.empty() && node.node_key.back() != '-')
                            node.node_key += '-';
                    }
                    while (!node.node_key.empty() && node.node_key.back() == '-')
                        node.node_key.pop_back();
                }
                store.upsert(node);
                return "{\"ok\":true,\"key\":\"" + esc(node.node_key) + "\"}";
            } catch (...) {
                return "{\"error\":\"invalid JSON body\"}";
            }
        }

        if (sub == "delete") {
            auto key = getParam("key");
            if (!store.get(key)) return "{\"error\":\"not found\"}";
            store.remove(key);
            return "{\"ok\":true}";
        }

        if (sub == "toggle") {
            auto key    = getParam("key");
            auto active = getParam("active");
            auto node   = store.get(key);
            if (!node) return "{\"error\":\"not found\"}";
            store.setActive(key, active != "0" && active != "false");
            return "{\"ok\":true}";
        }

        return "{\"error\":\"unknown endpoint\"}";
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

    // v0.42.0: rules REST API.
    // GET  /api/rules              → list all rules
    // POST /api/rules/toggle?id=N&active=0|1
    std::string apiRules(core::Config& cfg, const std::string& method,
                         const std::string& path) {
        core::Db db(cfg.projectDbPath("."));

        bool is_toggle = (path.find("/api/rules/toggle") == 0);

        if (is_toggle) {
            auto getId = [&]() -> int {
                auto q = path.find("id=");
                if (q == std::string::npos) return 0;
                try { return std::stoi(path.substr(q + 3)); } catch (...) { return 0; }
            };
            auto getActive = [&]() -> int {
                auto q = path.find("active=");
                if (q == std::string::npos) return 1;
                std::string v = path.substr(q + 7, 1);
                return (v == "1" || v == "t") ? 1 : 0;
            };
            int id = getId();
            if (id <= 0) return "{\"error\":\"missing id\"}";
            try {
                db.run("UPDATE rules SET active=? WHERE id=?",
                       {std::to_string(getActive()), std::to_string(id)});
            } catch (...) {
                return "{\"error\":\"db error\"}";
            }
            return "{\"ok\":true}";
        }

        // LIST
        std::ostringstream o; o << "[";
        bool first = true;
        try {
            db.query("SELECT id,rule_type,name,content,priority,active FROM rules ORDER BY priority DESC,id",
                     {}, [&](const core::Row& r) {
                if (r.size() < 6) return;
                if (!first) o << ","; first = false;
                int active = 0;
                try { active = std::stoi(r[5]); } catch (...) {}
                o << "{\"id\":" << r[0]
                  << ",\"rule_type\":\"" << esc(r[1])
                  << "\",\"name\":\"" << esc(r[2])
                  << "\",\"content\":\"" << esc(r[3])
                  << "\",\"priority\":" << r[4]
                  << ",\"active\":" << (active ? "true" : "false") << "}";
            });
        } catch (...) {}
        o << "]";
        return o.str();
    }

};

ICMG_REGISTER_COMMAND("serve", ServeCommand);

} // namespace icmg::cli
