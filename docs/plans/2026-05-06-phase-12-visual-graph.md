# Phase 12: Visual Graph (HTML + Cytoscape.js)

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Generate self-contained interactive HTML visualization dari graph data — lebih baik dari Graphify plain Markdown report.
**Architecture:** C++ generate JSON dari SQLite → inject ke HTML template → write file → open browser. Template embed Cytoscape.js inline.
**Tech Stack:** C++17, Cytoscape.js (embedded), HTML/CSS/JS
**Assumptions:** Phase 03 selesai. Browser tersedia di sistem.

---

### Task 1: Graph JSON serializer

**Files:**
- Create: `src/viz/graph_serializer.hpp`
- Create: `src/viz/graph_serializer.cpp`

```cpp
class GraphSerializer {
public:
    explicit GraphSerializer(GraphStore& graph, core::Db& db);

    struct VizNode {
        int64_t id;
        std::string path;
        std::string label;      // filename only
        std::string lang;
        std::string context;
        std::string symbols;
        int64_t size_bytes;
        int degree;             // total connections
        std::string community;  // computed community ID
        std::vector<std::string> active_rules;
    };

    struct VizEdge {
        int64_t src;
        int64_t dst;
        std::string edge_type;
        double weight;
    };

    struct VizData {
        std::vector<VizNode> nodes;
        std::vector<VizEdge> edges;
        std::unordered_map<std::string, std::string> community_colors;
    };

    VizData serialize() const;
    std::string toJson(const VizData& data) const;
};
```

**Community detection (simple):** Group nodes by connected component, assign color per group.

---

### Task 2: HTML template dengan Cytoscape.js

**Files:**
- Create: `src/viz/html_template.hpp`  (template sebagai string literal)

**Visual encoding:**
```
Node color by lang:
  cpp/c/h  → #4fc3f7  (biru muda)
  python   → #ffd54f  (amber)
  js/ts    → #aed581  (hijau muda)
  rust     → #ff8a65  (oranye)
  go       → #80cbc4  (teal)
  java     → #ef9a9a  (merah muda)
  sql/sp   → #ce93d8  (ungu)
  other    → #b0bec5  (abu)

Node size: proportional to degree (min 20px, max 60px)

Edge color by type:
  imports  → #42a5f5  (biru)
  calls    → #66bb6a  (hijau)
  inherits → #ffa726  (oranye)
  includes → #78909c  (abu)
  sp_calls → #ab47bc  (ungu)

Background: #1a1a2e (dark navy)
```

**UI Features:**
- Search bar: highlight matching nodes, dim sisanya
- Filter panel: by lang, edge type, community
- Layout switcher: CoSE (default) | Dagre | Concentric
- Info panel: click node → show context, symbols, rules, deps
- SP nodes: click → show full SQL + params
- Export: PNG / SVG button
- Minimap: corner navigation
- Stats bar: total nodes, edges, communities
- Dark/Light theme toggle
- Community convex hull overlay

---

### Task 3: DOT/Graphviz exporter (static alternative)

**Files:**
- Create: `src/viz/dot_exporter.hpp`
- Create: `src/viz/dot_exporter.cpp`

Generate `.dot` file yang bisa di-render dengan Graphviz:
```dot
digraph icmg {
  rankdir=LR;
  node [style=filled];
  "src/core/db.cpp" [fillcolor="#4fc3f7" label="db.cpp"];
  ...
}
```

---

### Task 4: GEXF + GraphML exporters

**Files:**
- Create: `src/viz/gexf_exporter.cpp`   (Gephi format)
- Create: `src/viz/graphml_exporter.cpp` (yEd format)

---

### Task 5: Browser opener (cross-platform)

**Files:**
- Create: `src/viz/browser_opener.hpp`
- Create: `src/viz/browser_opener.cpp`

```cpp
void openInBrowser(const std::string& file_path);
// Windows: ShellExecuteA(nullptr, "open", path, ...)
// macOS:   system("open " + path)
// Linux:   system("xdg-open " + path)
```

---

### Task 6: CLI commands

**Files:**
- Create: `src/cli/commands/viz_cmd.cpp`

```
icmg graph viz                              # generate + open browser
icmg graph viz --output ./viz/index.html   # custom output
icmg graph viz --no-open                   # generate only, don't open
icmg graph viz --format dot                # Graphviz .dot
icmg graph viz --format gexf               # Gephi
icmg graph viz --format graphml            # yEd
icmg graph viz --format svg                # static SVG (needs graphviz installed)
icmg graph viz --project smart-home        # viz dari project lain
icmg graph viz --filter-lang cpp,go        # only show specific languages
icmg graph viz --community 3               # zoom ke community 3
```

**Output:**
```
Generating visualization...
  Nodes: 249  Edges: 225  Communities: 14
  Output: ./icmg-viz/index.html (2.4 MB)
Opening in browser...
```

---

### Task 7: Final verify

**Step 1:**
```bash
./build/icmg graph scan src/ --depth 5
./build/icmg graph viz --no-open --output /tmp/icmg-viz.html
```
Expected: HTML file dibuat, valid HTML, filesize > 100KB (Cytoscape.js embedded).

**Step 2: Open dan verify visual:**
- Open `/tmp/icmg-viz.html` di browser
- Nodes terlihat dengan warna per language
- Click node → info panel muncul
- Search bar berfungsi
- Layout switch berfungsi

**Step 3: Commit**
```bash
git add src/viz/ src/cli/commands/viz_cmd.cpp
git commit -m "feat: phase-12 visual graph HTML + Cytoscape.js + multi-format export"
```

---

## Amendments from Architecture Review

### MEDIUM Fixes

**A1 — HTML size management**
Cytoscape.js minified = ~600KB. For large graphs:
- < 500 nodes: embed semuanya, static render
- 500-2000 nodes: lazy render (paginate communities), virtual DOM
- > 2000 nodes: warn user, generate DOT/GEXF instead, suggest Gephi

```
icmg graph viz
  Nodes: 2,847 — large graph detected.
  Recommendation: use --format gexf and open with Gephi for better performance.
  Or: --filter-community 3 to visualize one community at a time.
  Continue anyway? [y/N]
```

**A2 — Windowed/virtual rendering**
Untuk graph > 500 nodes, Cytoscape.js layout hanya render nodes di viewport.
Implement dengan Cytoscape.js `pixelRatio` optimization + layout caching.

**A3 — Export size estimate sebelum generate**
```
icmg graph viz --estimate
  Estimated output size: ~4.2 MB (249 nodes, Cytoscape.js embedded)
  [Continue? icmg graph viz]
```
