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
<title>icmg graph</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/cytoscape/3.28.1/cytoscape.min.js"
        crossorigin="anonymous"></script>
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
#stats{font-size:11px;color:var(--text2);white-space:nowrap;margin-left:auto;}
#main{display:flex;flex:1;overflow:hidden;}
#cy{flex:1;}
#sidebar{width:280px;background:var(--surface);border-left:1px solid var(--border);display:flex;flex-direction:column;overflow:hidden;transition:width .2s;}
#sidebar.collapsed{width:0;border:none;}
#filter-panel{padding:12px;border-bottom:1px solid var(--border);flex-shrink:0;}
#filter-panel h3,#info-panel h3{font-size:11px;font-weight:600;color:var(--text2);text-transform:uppercase;letter-spacing:.5px;margin-bottom:8px;}
.filter-group{margin-bottom:8px;}
.filter-group label{display:flex;align-items:center;gap:6px;font-size:12px;cursor:pointer;padding:2px 0;}
.filter-group input[type=checkbox]{accent-color:var(--accent);}
#info-panel{flex:1;overflow-y:auto;padding:12px;}
.info-field{margin-bottom:6px;}
.info-label{font-size:11px;color:var(--text2);}
.info-value{font-size:12px;word-break:break-all;}
select{padding:4px 8px;background:var(--surface2);color:var(--text);border:1px solid var(--border);border-radius:4px;font-size:12px;}
</style>
</head>
<body>
<div id="topbar">
  <h1 id="page-title"></h1>
  <input id="search" type="text" placeholder="Search nodes..." autocomplete="off">
  <select id="layout-sel" title="Layout">
    <option value="cose">CoSE</option>
    <option value="breadthfirst">BFS</option>
    <option value="concentric">Concentric</option>
    <option value="grid">Grid</option>
    <option value="circle">Circle</option>
  </select>
  <button class="btn" id="btn-png">PNG</button>
  <button class="btn" id="btn-theme">Theme</button>
  <button class="btn" id="btn-sidebar">&#9776;</button>
  <span id="stats"></span>
</div>
<div id="main">
  <div id="cy"></div>
  <div id="sidebar">
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
<script>
// All DOM mutations use textContent for user-supplied data (XSS-safe).
const GRAPH_DATA = ICMG_GRAPH_JSON;
const PAGE_TITLE = ICMG_PAGE_TITLE;

document.getElementById('page-title').textContent = PAGE_TITLE;
document.title = PAGE_TITLE;

const EDGE_COLORS={imports:'#42a5f5',calls:'#66bb6a',inherits:'#ffa726',includes:'#78909c',sp_calls:'#ab47bc'};
function edgeColor(t){return EDGE_COLORS[t]||'#888';}

function buildElements(){
  const elems=[];
  GRAPH_DATA.nodes.forEach(n=>{
    const sz=Math.max(20,Math.min(60,20+n.degree*4));
    elems.push({data:{id:'n'+n.id,label:n.label,path:n.path,lang:n.lang,
      context:n.context,size_bytes:n.size_bytes,degree:n.degree,
      community:n.community,nodeSize:sz,color:n.color}});
  });
  GRAPH_DATA.edges.forEach(e=>{
    elems.push({data:{source:'n'+e.src,target:'n'+e.dst,
      type:e.type,weight:e.weight,color:edgeColor(e.type)}});
  });
  return elems;
}

const cy=cytoscape({
  container:document.getElementById('cy'),
  elements:buildElements(),
  style:[
    {selector:'node',style:{
      'background-color':'data(color)','label':'data(label)',
      'width':'data(nodeSize)','height':'data(nodeSize)',
      'font-size':10,'color':'#e0e0e0','text-valign':'bottom',
      'text-outline-width':1,'text-outline-color':'#1a1a2e',
      'text-margin-y':4,'min-zoomed-font-size':8
    }},
    {selector:'node:selected',style:{'border-width':3,'border-color':'#fff','border-opacity':1}},
    {selector:'node.dimmed',style:{'opacity':0.12}},
    {selector:'node.highlighted',style:{'opacity':1,'border-width':2,'border-color':'#fff'}},
    {selector:'edge',style:{
      'width':'mapData(weight,0,2,1,4)','line-color':'data(color)',
      'target-arrow-color':'data(color)','target-arrow-shape':'triangle',
      'curve-style':'bezier','opacity':0.7
    }},
    {selector:'edge.dimmed',style:{'opacity':0.05}},
  ],
  layout:{name:'cose',animate:false,randomize:true,idealEdgeLength:80,nodeOverlap:10,padding:30},
  wheelSensitivity:0.3
});

// Stats bar
(function(){
  const comms=new Set(GRAPH_DATA.nodes.map(x=>x.community)).size;
  document.getElementById('stats').textContent=
    GRAPH_DATA.nodes.length+' nodes · '+GRAPH_DATA.edges.length+' edges · '+comms+' communities';
})();

// Layout switcher
document.getElementById('layout-sel').addEventListener('change',function(){
  cy.layout({name:this.value,animate:true,animationDuration:400,padding:30}).run();
});

// Search
let st;
document.getElementById('search').addEventListener('input',function(){
  clearTimeout(st);
  const q=this.value.trim();
  st=setTimeout(()=>applySearch(q),200);
});

function applySearch(q){
  if(!q){cy.nodes().removeClass('dimmed highlighted');cy.edges().removeClass('dimmed');return;}
  const ql=q.toLowerCase();
  const matched=cy.nodes().filter(n=>
    (n.data('label')||'').toLowerCase().includes(ql)||
    (n.data('path')||'').toLowerCase().includes(ql)||
    (n.data('lang')||'').toLowerCase().includes(ql)
  );
  cy.nodes().addClass('dimmed').removeClass('highlighted');
  cy.edges().addClass('dimmed');
  matched.removeClass('dimmed').addClass('highlighted');
  matched.connectedEdges().removeClass('dimmed');
}

// Lang filters
(function(){
  const langs=[...new Set(GRAPH_DATA.nodes.map(n=>n.lang||'unknown'))].sort();
  const div=document.getElementById('lang-filters');
  const hdr=document.createElement('div');
  hdr.textContent='Languages';
  hdr.style.cssText='font-size:11px;color:var(--text2);margin-bottom:4px';
  div.appendChild(hdr);
  langs.forEach(l=>{
    const lbl=document.createElement('label');
    const cb=document.createElement('input');
    cb.type='checkbox';cb.checked=true;cb.dataset.lang=l;
    cb.addEventListener('change',applyLangFilter);
    const txt=document.createTextNode(' '+l);
    lbl.appendChild(cb);lbl.appendChild(txt);
    div.appendChild(lbl);
  });
})();

// Edge type filters
(function(){
  const types=[...new Set(GRAPH_DATA.edges.map(e=>e.type||'imports'))].sort();
  const div=document.getElementById('edge-filters');
  const hdr=document.createElement('div');
  hdr.textContent='Edge types';
  hdr.style.cssText='font-size:11px;color:var(--text2);margin-bottom:4px';
  div.appendChild(hdr);
  types.forEach(t=>{
    const lbl=document.createElement('label');
    const cb=document.createElement('input');
    cb.type='checkbox';cb.checked=true;cb.dataset.etype=t;
    cb.addEventListener('change',applyEdgeFilter);
    const dot=document.createElement('span');
    dot.style.cssText='display:inline-block;width:8px;height:8px;border-radius:50%;background:'+edgeColor(t)+';margin:0 4px';
    const txt=document.createTextNode(t);
    lbl.appendChild(cb);lbl.appendChild(dot);lbl.appendChild(txt);
    div.appendChild(lbl);
  });
})();

function applyLangFilter(){
  const hidden=new Set([...document.querySelectorAll('#lang-filters input:not(:checked)')].map(c=>c.dataset.lang));
  cy.nodes().forEach(n=>{ if(hidden.has(n.data('lang')||'unknown'))n.hide();else n.show(); });
}
function applyEdgeFilter(){
  const hidden=new Set([...document.querySelectorAll('#edge-filters input:not(:checked)')].map(c=>c.dataset.etype));
  cy.edges().forEach(e=>{ if(hidden.has(e.data('type')))e.hide();else e.show(); });
}

// Node info panel (all textContent, no raw HTML)
cy.on('tap','node',function(evt){
  const n=evt.target.data();
  const panel=document.getElementById('info-content');
  panel.textContent='';
  function addField(label,value){
    const wrap=document.createElement('div');
    wrap.className='info-field';
    const lbl=document.createElement('div');
    lbl.className='info-label';
    lbl.textContent=label;
    const val=document.createElement('div');
    val.className='info-value';
    val.textContent=value;
    wrap.appendChild(lbl);
    wrap.appendChild(val);
    panel.appendChild(wrap);
  }
  addField('Path', n.path||'—');
  addField('Language', n.lang||'—');
  addField('Size', fmtBytes(n.size_bytes));
  addField('Connections', String(n.degree||0));
  addField('Community', n.community||'—');
  if(n.context) addField('Context', n.context.substring(0,300));
});

cy.on('tap',function(evt){
  if(evt.target===cy){
    const panel=document.getElementById('info-content');
    panel.textContent='';
    const hint=document.createElement('p');
    hint.style.cssText='color:var(--text2);font-size:12px';
    hint.textContent='Click a node to inspect.';
    panel.appendChild(hint);
  }
});

function fmtBytes(b){
  if(!b)return '0 B';
  if(b<1024)return b+' B';
  if(b<1048576)return (b/1024).toFixed(1)+' KB';
  return (b/1048576).toFixed(1)+' MB';
}
document.getElementById('btn-png').addEventListener('click',function(){
  const url=cy.png({full:true,scale:2});
  const a=document.createElement('a');
  a.href=url;a.download='icmg-graph.png';a.click();
});
document.getElementById('btn-theme').addEventListener('click',function(){
  document.body.classList.toggle('light');
});
document.getElementById('btn-sidebar').addEventListener('click',function(){
  document.getElementById('sidebar').classList.toggle('collapsed');
});
cy.ready(function(){cy.fit(30);});
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
