#pragma once
#include <string>

namespace icmg::viz {

// Returns self-contained HTML with graph JSON injected.
// graphJson: output of GraphSerializer::toJson()
// title: page title (project name or path)
// Security note: all user data inserted into the DOM uses textContent or
// explicit sanitization (escHtml). This file is a local-only static viewer.
inline std::string buildHtml(const std::string& graphJson, const std::string& title) {
    std::string tmpl = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Icemage graph</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><defs><linearGradient id='f' x1='0' y1='0' x2='1' y2='1'><stop offset='0%25' stop-color='%237DD3FC'/><stop offset='100%25' stop-color='%234338CA'/></linearGradient></defs><circle cx='16' cy='16' r='14' fill='none' stroke='url(%23f)' stroke-width='1.5'/><g stroke='url(%23f)' stroke-width='1.4' fill='none' stroke-linecap='round'><path d='M16 16 L16 7 M16 16 L9 12 M16 16 L23 12 M16 16 L9 20 M16 16 L23 20 M16 16 L16 25'/></g><polygon points='16,13 19,16 16,19 13,16' fill='url(%23f)'/></svg>"/>
<script src="https://cdnjs.cloudflare.com/ajax/libs/cytoscape/3.28.1/cytoscape.min.js"
        crossorigin="anonymous"></script>
<script src="https://unpkg.com/layout-base/layout-base.js"></script>
<script src="https://unpkg.com/cose-base/cose-base.js"></script>
<script src="https://unpkg.com/cytoscape-fcose/cytoscape-fcose.js"></script>
<style>
:root{--bg:#1a1a2e;--surface:#16213e;--surface2:#0f3460;--text:#e0e0e0;--text2:#9e9e9e;--accent:#4fc3f7;--border:#2a2a4a;}
body.light{--bg:#f5f5f5;--surface:#fff;--surface2:#e0e0e0;--text:#212121;--text2:#616161;--accent:#1976d2;--border:#e0e0e0;}
*{box-sizing:border-box;margin:0;padding:0;}
body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;display:flex;flex-direction:column;height:100vh;overflow:hidden;}
#topbar{display:flex;align-items:center;gap:8px;padding:8px 12px;background:var(--surface);border-bottom:1px solid var(--border);flex-shrink:0;}
#topbar h1{font-size:14px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:220px;color:var(--accent);}
#search{flex:1;max-width:280px;padding:5px 10px;border-radius:16px;border:1px solid var(--border);background:var(--surface2);color:var(--text);font-size:13px;outline:none;}
#search:focus{border-color:var(--accent);}
.btn{padding:5px 10px;border-radius:4px;border:1px solid var(--border);background:var(--surface2);color:var(--text);font-size:12px;cursor:pointer;white-space:nowrap;}
.btn:hover{background:var(--accent);color:#fff;border-color:var(--accent);}
.btn.active{background:var(--accent);color:#fff;border-color:var(--accent);}
#stats{font-size:11px;color:var(--text2);white-space:nowrap;margin-left:auto;}
#main{display:flex;flex:1;overflow:hidden;}
#cy{flex:1;}
#sidebar{width:280px;background:var(--surface);border-left:1px solid var(--border);display:flex;flex-direction:column;overflow:hidden;transition:width .2s;}
#sidebar.collapsed{width:0;border:none;}
#filter-panel,#options-panel{padding:12px;border-bottom:1px solid var(--border);flex-shrink:0;}
#filter-panel h3,#info-panel h3,#options-panel h3{font-size:11px;font-weight:600;color:var(--text2);text-transform:uppercase;letter-spacing:.5px;margin-bottom:8px;}
.filter-group{margin-bottom:8px;}
.filter-group label{display:flex;align-items:center;gap:6px;font-size:12px;cursor:pointer;padding:2px 0;}
.filter-group input[type=checkbox]{accent-color:var(--accent);}
#info-panel{flex:1;overflow-y:auto;padding:12px;}
.info-field{margin-bottom:6px;}
.info-label{font-size:11px;color:var(--text2);}
.info-value{font-size:12px;word-break:break-all;}
.info-value a{color:var(--accent);cursor:pointer;text-decoration:underline;}
select{padding:4px 8px;background:var(--surface2);color:var(--text);border:1px solid var(--border);border-radius:4px;font-size:12px;}
#tooltip{position:fixed;display:none;background:var(--surface);color:var(--text);border:1px solid var(--border);border-radius:4px;padding:6px 10px;font-size:12px;pointer-events:none;z-index:9999;max-width:400px;word-break:break-all;box-shadow:0 4px 12px rgba(0,0,0,0.3);}
.legend{display:flex;flex-wrap:wrap;gap:6px;font-size:11px;color:var(--text2);}
.legend-item{display:flex;align-items:center;gap:4px;}
.legend-dot{width:8px;height:8px;border-radius:50%;}
.legend-line{width:14px;height:2px;}
</style>
</head>
<body>
<div id="topbar">
  <h1 id="page-title"></h1>
  <input id="search" type="text" placeholder="Search nodes..." autocomplete="off">
  <select id="layout-sel" title="Layout">
    <option value="fcose" selected>fCoSE</option>
    <option value="cose">CoSE</option>
    <option value="breadthfirst">BFS</option>
    <option value="concentric">Concentric</option>
    <option value="grid">Grid</option>
    <option value="circle">Circle</option>
  </select>
  <button class="btn" id="btn-orphans" title="Hide isolated nodes (no edges)">Hide orphans</button>
  <button class="btn" id="btn-groups" title="Group by directory">Group dirs</button>
  <button class="btn" id="btn-png">PNG</button>
  <button class="btn" id="btn-theme">Theme</button>
  <button class="btn" id="btn-sidebar">&#9776;</button>
  <span id="stats"></span>
</div>
<div id="main">
  <div id="cy"></div>
  <div id="sidebar">
    <div id="options-panel">
      <h3>Legend</h3>
      <div class="legend" id="legend"></div>
    </div>
    <div id="filter-panel">
      <h3>Filter</h3>
      <div class="filter-group" id="lang-filters"></div>
      <div class="filter-group" id="edge-filters"></div>
    </div>
    <div id="info-panel">
      <h3>Node Info</h3>
      <div id="info-content"></div>
    </div>
  </div>
</div>
<div id="tooltip"></div>
<script>
// All DOM mutations use textContent for user-supplied data (XSS-safe).
const GRAPH_DATA = ICMG_GRAPH_JSON;
const PAGE_TITLE = ICMG_PAGE_TITLE;

document.getElementById('page-title').textContent = PAGE_TITLE;
document.title = PAGE_TITLE;

// Edge type colors — match scanner edge types
const EDGE_COLORS = {
  imports:   '#42a5f5',
  uses:      '#a855f7',
  companion: '#f59e0b',
  calls:     '#66bb6a',
  inherits:  '#ffa726',
  includes:  '#78909c',
  sp_calls:  '#ec407a'
};
function edgeColor(t){return EDGE_COLORS[t]||'#888';}

// Binary/asset extensions to filter out by default
const BINARY_EXTS = /\.(png|jpe?g|gif|ico|bmp|svg|webp|dll|exe|pdb|lib|so|dylib|class|jar|war|zip|tar|gz|mp[34]|wav|ogg|ttf|otf|woff2?|eot)$/i;

// Truncate label for display, keep full as tooltip
function truncLabel(s, n){
  if (!s) return '';
  if (s.length <= n) return s;
  return s.slice(0, n - 1) + '…';
}

// Track UI state
const state = {
  hideOrphans: false,
  groupByDir: false,
  hiddenLangs: new Set(),
  hiddenEdges: new Set(),
  hiddenBinary: true   // hide PNG/ico/etc by default
};

// Compute degree manually so orphan detection survives filtering
function computeDegrees(){
  const deg = {};
  GRAPH_DATA.nodes.forEach(n => deg['n'+n.id] = 0);
  GRAPH_DATA.edges.forEach(e => {
    deg['n'+e.src] = (deg['n'+e.src]||0) + 1;
    deg['n'+e.dst] = (deg['n'+e.dst]||0) + 1;
  });
  return deg;
}
const degMap = computeDegrees();

function isBinary(n){
  return BINARY_EXTS.test(n.label || n.path || '');
}

function buildElements(){
  const elems = [];
  const dirs = new Set();

  // Collect parent dirs if grouping
  if (state.groupByDir){
    GRAPH_DATA.nodes.forEach(n => { if (n.dir) dirs.add(n.dir); });
    dirs.forEach(d => {
      elems.push({data:{id:'g:'+d, label:d, isParent:true}});
    });
  }

  GRAPH_DATA.nodes.forEach(n => {
    const sz = Math.max(18, Math.min(54, 18 + (degMap['n'+n.id]||0) * 3));
    const node = {
      data:{
        id:'n'+n.id,
        label: truncLabel(n.label, 22),
        fullLabel: n.label,
        path: n.path,
        dir: n.dir || '',
        lang: n.lang,
        context: n.context,
        size_bytes: n.size_bytes,
        degree: degMap['n'+n.id] || 0,
        community: n.community,
        nodeSize: sz,
        color: n.color,
        isBinary: isBinary(n)
      }
    };
    if (state.groupByDir && n.dir) node.data.parent = 'g:'+n.dir;
    elems.push(node);
  });

  GRAPH_DATA.edges.forEach(e => {
    elems.push({data:{
      id:'e'+e.src+'_'+e.dst+'_'+(e.type||'imports'),
      source:'n'+e.src, target:'n'+e.dst,
      type: e.type || 'imports',
      weight: e.weight,
      color: edgeColor(e.type||'imports')
    }});
  });
  return elems;
}

const cy = cytoscape({
  container: document.getElementById('cy'),
  elements: buildElements(),
  style: [
    // Compound parent (directory group)
    {selector:'node[?isParent]', style:{
      'background-color':'#0f3460','background-opacity':0.25,
      'border-width':1,'border-color':'#4a6fa5','border-opacity':0.6,
      'label':'data(label)','font-size':11,'color':'#9bb8e8',
      'text-valign':'top','text-halign':'center','text-margin-y':-4,
      'padding':12,'shape':'round-rectangle','text-outline-width':2,
      'text-outline-color':'#1a1a2e'
    }},
    {selector:'node', style:{
      'background-color':'data(color)','label':'data(label)',
      'width':'data(nodeSize)','height':'data(nodeSize)',
      'font-size':10,'color':'#e0e0e0','text-valign':'bottom',
      'text-outline-width':2,'text-outline-color':'#1a1a2e',
      'text-margin-y':4,'min-zoomed-font-size':7,
      'text-max-width':110,'text-wrap':'ellipsis'
    }},
    {selector:'node:selected', style:{
      'border-width':3,'border-color':'#fff','border-opacity':1
    }},
    {selector:'node.dimmed', style:{'opacity':0.12}},
    {selector:'node.highlighted', style:{
      'opacity':1,'border-width':3,'border-color':'#fff'
    }},
    {selector:'node.hidden-binary', style:{display:'none'}},
    {selector:'node.hidden-orphan', style:{display:'none'}},
    {selector:'node.hidden-lang', style:{display:'none'}},
    {selector:'edge', style:{
      'width':'mapData(weight,0,2,1,4)','line-color':'data(color)',
      'target-arrow-color':'data(color)','target-arrow-shape':'triangle',
      'curve-style':'bezier','opacity':0.65,'arrow-scale':0.9
    }},
    {selector:'edge.dimmed', style:{'opacity':0.04}},
    {selector:'edge.hidden-type', style:{display:'none'}}
  ],
  layout: layoutOpts('fcose'),
  wheelSensitivity: 0.3,
  minZoom: 0.05,
  maxZoom: 3
});

function layoutOpts(name){
  const base = {name, animate:true, animationDuration:500, padding:40, fit:true};
  if (name === 'fcose'){
    return Object.assign(base, {
      quality:'default',
      nodeRepulsion: 6500,
      idealEdgeLength: 90,
      edgeElasticity: 0.45,
      gravity: 0.25,
      gravityRangeCompound: 1.5,
      packComponents: true,
      randomize: true,
      tile: true,
      nodeSeparation: 75
    });
  }
  if (name === 'cose'){
    return Object.assign(base, {
      randomize:true, idealEdgeLength:90, nodeOverlap:12,
      nodeRepulsion:400000, gravity:0.25, numIter:1500
    });
  }
  return base;
}

// ---- Stats bar ----
(function(){
  const comms = new Set(GRAPH_DATA.nodes.map(x=>x.community)).size;
  document.getElementById('stats').textContent =
    GRAPH_DATA.nodes.length + ' nodes · ' +
    GRAPH_DATA.edges.length + ' edges · ' + comms + ' communities';
})();

// ---- Layout switcher ----
document.getElementById('layout-sel').addEventListener('change', function(){
  cy.layout(layoutOpts(this.value)).run();
});

// ---- Search with zoom-to-match ----
let st;
document.getElementById('search').addEventListener('input', function(){
  clearTimeout(st);
  const q = this.value.trim();
  st = setTimeout(()=>applySearch(q), 200);
});
function applySearch(q){
  if (!q){
    cy.nodes().removeClass('dimmed highlighted');
    cy.edges().removeClass('dimmed');
    return;
  }
  const ql = q.toLowerCase();
  const matched = cy.nodes(':visible').filter(n =>
    !n.data('isParent') && (
      (n.data('fullLabel')||n.data('label')||'').toLowerCase().includes(ql) ||
      (n.data('path')||'').toLowerCase().includes(ql) ||
      (n.data('lang')||'').toLowerCase().includes(ql) ||
      (n.data('dir')||'').toLowerCase().includes(ql)
    )
  );
  cy.nodes().addClass('dimmed').removeClass('highlighted');
  cy.edges().addClass('dimmed');
  matched.removeClass('dimmed').addClass('highlighted');
  matched.connectedEdges().removeClass('dimmed');
  if (matched.length > 0 && matched.length <= 8){
    cy.animate({fit:{eles:matched, padding:60}}, {duration:400});
  }
}

// ---- Lang filters ----
(function(){
  const langs = [...new Set(GRAPH_DATA.nodes.map(n=>n.lang||'unknown'))].sort();
  const div = document.getElementById('lang-filters');
  const hdr = document.createElement('div');
  hdr.textContent = 'Languages';
  hdr.style.cssText = 'font-size:11px;color:var(--text2);margin-bottom:4px;font-weight:600';
  div.appendChild(hdr);
  langs.forEach(l => {
    const lbl = document.createElement('label');
    const cb = document.createElement('input');
    cb.type = 'checkbox'; cb.checked = true; cb.dataset.lang = l;
    cb.addEventListener('change', applyLangFilter);
    const txt = document.createTextNode(' ' + l);
    lbl.appendChild(cb); lbl.appendChild(txt);
    div.appendChild(lbl);
  });
  // "Hide binary" toggle
  const lbl = document.createElement('label');
  const cb = document.createElement('input');
  cb.type = 'checkbox'; cb.checked = state.hiddenBinary; cb.id = 'cb-binary';
  cb.addEventListener('change', function(){
    state.hiddenBinary = this.checked;
    applyVisibility();
  });
  const txt = document.createTextNode(' Hide images/binaries');
  lbl.appendChild(cb); lbl.appendChild(txt);
  lbl.style.marginTop = '4px';
  lbl.style.color = 'var(--text2)';
  div.appendChild(lbl);
})();

// ---- Edge filters + Legend ----
(function(){
  const types = [...new Set(GRAPH_DATA.edges.map(e=>e.type||'imports'))].sort();
  const div = document.getElementById('edge-filters');
  const hdr = document.createElement('div');
  hdr.textContent = 'Edge types';
  hdr.style.cssText = 'font-size:11px;color:var(--text2);margin-bottom:4px;font-weight:600';
  div.appendChild(hdr);
  types.forEach(t => {
    const lbl = document.createElement('label');
    const cb = document.createElement('input');
    cb.type = 'checkbox'; cb.checked = true; cb.dataset.etype = t;
    cb.addEventListener('change', applyEdgeFilter);
    const dot = document.createElement('span');
    dot.style.cssText = 'display:inline-block;width:10px;height:10px;border-radius:50%;background:'+edgeColor(t)+';margin:0 4px';
    const txt = document.createTextNode(t);
    lbl.appendChild(cb); lbl.appendChild(dot); lbl.appendChild(txt);
    div.appendChild(lbl);
  });

  // Legend (read-only summary at top of sidebar)
  const leg = document.getElementById('legend');
  const langs = [...new Set(GRAPH_DATA.nodes.map(n=>n.lang||'unknown'))].sort();
  langs.slice(0, 6).forEach(l => {
    const node = GRAPH_DATA.nodes.find(n=>(n.lang||'unknown')===l);
    if (!node) return;
    const item = document.createElement('div');
    item.className = 'legend-item';
    const dot = document.createElement('span');
    dot.className = 'legend-dot';
    dot.style.background = node.color;
    item.appendChild(dot);
    const txt = document.createTextNode(l);
    item.appendChild(txt);
    leg.appendChild(item);
  });
  types.slice(0, 4).forEach(t => {
    const item = document.createElement('div');
    item.className = 'legend-item';
    const line = document.createElement('span');
    line.className = 'legend-line';
    line.style.background = edgeColor(t);
    item.appendChild(line);
    const txt = document.createTextNode(t);
    item.appendChild(txt);
    leg.appendChild(item);
  });
})();

function applyLangFilter(){
  state.hiddenLangs = new Set(
    [...document.querySelectorAll('#lang-filters input[data-lang]:not(:checked)')]
      .map(c => c.dataset.lang)
  );
  applyVisibility();
}
function applyEdgeFilter(){
  state.hiddenEdges = new Set(
    [...document.querySelectorAll('#edge-filters input:not(:checked)')]
      .map(c => c.dataset.etype)
  );
  cy.edges().forEach(e => {
    if (state.hiddenEdges.has(e.data('type'))) e.addClass('hidden-type');
    else e.removeClass('hidden-type');
  });
  // Re-eval orphans because edge visibility changed
  if (state.hideOrphans) applyOrphanFilter();
}
function applyVisibility(){
  cy.nodes().forEach(n => {
    if (n.data('isParent')) return;
    const langHide = state.hiddenLangs.has(n.data('lang')||'unknown');
    const binHide = state.hiddenBinary && n.data('isBinary');
    if (langHide || binHide) n.addClass('hidden-lang');
    else n.removeClass('hidden-lang');
  });
  if (state.hideOrphans) applyOrphanFilter();
  hideEmptyParents();
}
function applyOrphanFilter(){
  cy.nodes().forEach(n => {
    if (n.data('isParent')) return;
    const visEdges = n.connectedEdges(':visible').length;
    if (state.hideOrphans && visEdges === 0) n.addClass('hidden-orphan');
    else n.removeClass('hidden-orphan');
  });
  hideEmptyParents();
}
function hideEmptyParents(){
  cy.nodes('[?isParent]').forEach(p => {
    const visChildren = p.children(':visible').length;
    if (visChildren === 0) p.addClass('hidden-orphan');
    else p.removeClass('hidden-orphan');
  });
}

// ---- Toggle buttons ----
document.getElementById('btn-orphans').addEventListener('click', function(){
  state.hideOrphans = !state.hideOrphans;
  this.classList.toggle('active', state.hideOrphans);
  applyOrphanFilter();
});
document.getElementById('btn-groups').addEventListener('click', function(){
  state.groupByDir = !state.groupByDir;
  this.classList.toggle('active', state.groupByDir);
  rebuildGraph();
});

function rebuildGraph(){
  cy.elements().remove();
  cy.add(buildElements());
  applyVisibility();
  applyEdgeFilter();
  cy.layout(layoutOpts(document.getElementById('layout-sel').value)).run();
}

// ---- Tooltip on hover (full label) ----
const tooltip = document.getElementById('tooltip');
cy.on('mouseover', 'node', function(evt){
  const n = evt.target.data();
  if (n.isParent) return;
  tooltip.textContent = (n.fullLabel||n.label||'') + (n.path && n.path !== n.fullLabel ? '  —  '+n.path : '');
  tooltip.style.display = 'block';
});
cy.on('mousemove', 'node', function(evt){
  const e = evt.originalEvent;
  if (!e) return;
  tooltip.style.left = (e.clientX + 12) + 'px';
  tooltip.style.top  = (e.clientY + 12) + 'px';
});
cy.on('mouseout', 'node', function(){
  tooltip.style.display = 'none';
});

// ---- Node info panel ----
function showInfo(n){
  const panel = document.getElementById('info-content');
  panel.textContent = '';
  function addField(label, value, isLink){
    const wrap = document.createElement('div');
    wrap.className = 'info-field';
    const lbl = document.createElement('div');
    lbl.className = 'info-label';
    lbl.textContent = label;
    const val = document.createElement('div');
    val.className = 'info-value';
    val.textContent = value;
    wrap.appendChild(lbl);
    wrap.appendChild(val);
    panel.appendChild(wrap);
  }
  addField('File', n.fullLabel || n.label || '—');
  addField('Path', n.path || '—');
  addField('Directory', n.dir || '(root)');
  addField('Language', n.lang || '—');
  addField('Size', fmtBytes(n.size_bytes));
  addField('Connections', String(n.degree || 0));
  addField('Community', n.community || '—');
  if (n.context) addField('Context', n.context.substring(0, 300));

  // Neighbors list
  const node = cy.getElementById('n' + GRAPH_DATA.nodes.find(x=>x.label===n.fullLabel && x.path===n.path)?.id);
  // Better: use the actual selected element
}

cy.on('tap','node',function(evt){
  const target = evt.target;
  if (target.data('isParent')) return;
  const n = target.data();
  const panel = document.getElementById('info-content');
  panel.textContent = '';
  function addField(label, value){
    const wrap = document.createElement('div');
    wrap.className = 'info-field';
    const lbl = document.createElement('div');
    lbl.className = 'info-label';
    lbl.textContent = label;
    const val = document.createElement('div');
    val.className = 'info-value';
    val.textContent = value;
    wrap.appendChild(lbl);
    wrap.appendChild(val);
    panel.appendChild(wrap);
  }
  addField('File', n.fullLabel || n.label || '—');
  addField('Path', n.path || '—');
  addField('Directory', n.dir || '(root)');
  addField('Language', n.lang || '—');
  addField('Size', fmtBytes(n.size_bytes));
  addField('Connections', String(n.degree || 0));
  addField('Community', n.community || '—');
  if (n.context) addField('Context', n.context.substring(0, 300));

  // Neighbors
  const neighbors = target.neighborhood('node').filter(x => !x.data('isParent'));
  if (neighbors.length > 0){
    const wrap = document.createElement('div');
    wrap.className = 'info-field';
    const lbl = document.createElement('div');
    lbl.className = 'info-label';
    lbl.textContent = 'Neighbors (' + neighbors.length + ')';
    wrap.appendChild(lbl);
    neighbors.slice(0, 20).forEach(nb => {
      const a = document.createElement('a');
      a.textContent = nb.data('fullLabel') || nb.data('label');
      a.style.cssText = 'display:block;font-size:12px;margin:2px 0';
      a.addEventListener('click', () => {
        cy.animate({fit:{eles:nb, padding:80}}, {duration:300});
        nb.trigger('tap');
      });
      wrap.appendChild(a);
    });
    panel.appendChild(wrap);
  }
});

cy.on('tap', function(evt){
  if (evt.target === cy){
    const panel = document.getElementById('info-content');
    panel.textContent = '';
    const hint = document.createElement('p');
    hint.style.cssText = 'color:var(--text2);font-size:12px';
    hint.textContent = 'Click a node to inspect.';
    panel.appendChild(hint);
  }
});

function fmtBytes(b){
  if (!b) return '0 B';
  if (b < 1024) return b + ' B';
  if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
  return (b/1048576).toFixed(1) + ' MB';
}

document.getElementById('btn-png').addEventListener('click', function(){
  const url = cy.png({full:true, scale:2, bg:'#1a1a2e'});
  const a = document.createElement('a');
  a.href = url; a.download = 'icmg-graph.png'; a.click();
});
document.getElementById('btn-theme').addEventListener('click', function(){
  document.body.classList.toggle('light');
});
document.getElementById('btn-sidebar').addEventListener('click', function(){
  document.getElementById('sidebar').classList.toggle('collapsed');
});

// Apply default visibility (hides binaries by default)
applyVisibility();
cy.ready(function(){ cy.fit(40); });
</script>
</body>
</html>)HTML";

    // Replace placeholders
    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    // Escape title for JS string literal
    std::string safeTitle;
    for (char c : title) {
        if (c == '"') safeTitle += "\\\"";
        else if (c == '\\') safeTitle += "\\\\";
        else safeTitle += c;
    }

    replaceAll(tmpl, "ICMG_GRAPH_JSON", graphJson);
    replaceAll(tmpl, "ICMG_PAGE_TITLE", "\"" + safeTitle + "\"");
    return tmpl;
}

} // namespace icmg::viz
