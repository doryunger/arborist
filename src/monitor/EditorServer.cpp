#include "bt/EditorServer.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <httplib.h>

#include "bt/ComplexityAnalyzer.h"
#include "bt/PathExplorer.h"
#include "bt/RegistrySpec.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaNode.h"
#include "bt/SchemaParser.h"
#include "bt/Status.h"

namespace bt {

// ── Editor UI HTML ─────────────────────────────────────────────────────────────

static constexpr std::string_view kEditorHtml = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Arborist — Behavior Tree Editor</title>
<style>
  *, *::before, *::after { box-sizing: border-box; }
  body { margin: 0; background: #1e1e2e; color: #cdd6f4; font-family: monospace; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
  #header { padding: 10px 18px; background: #181825; border-bottom: 1px solid #313244; display: flex; align-items: center; gap: 14px; }
  #header h1 { margin: 0; font-size: 15px; color: #89b4fa; letter-spacing: 0.05em; }
  .badge { font-size: 11px; padding: 2px 8px; border-radius: 10px; background: #a6e3a1; color: #1e1e2e; font-weight: bold; }
  .badge.disconnected { background: #f38ba8; }
  .badge.pending { background: #f9e2af; color: #1e1e2e; }
  #main { display: flex; flex: 1; overflow: hidden; }
  .panel { padding: 14px; overflow-y: auto; }
  #pending-panel { width: 260px; border-right: 1px solid #313244; display: flex; flex-direction: column; gap: 8px; flex-shrink: 0; }
  .pending-hdr { display: flex; align-items: center; gap: 8px; margin-bottom: 4px; }
  .pending-hdr h2 { flex: 1; margin: 0; }
  .change-card { background: #181825; border: 1px solid #45475a; border-left: 3px solid #f9e2af; border-radius: 5px; padding: 7px 9px; font-size: 11px; cursor: pointer; transition: border-color 0.15s; }
  .change-card:hover { border-color: #f9e2af; background: #1e1e2e; }
  .change-beh { font-size: 10px; color: #6c7086; margin-bottom: 2px; }
  .change-name { color: #f9e2af; font-weight: bold; margin-bottom: 4px; }
  .change-row { display: flex; align-items: center; gap: 4px; font-size: 10px; margin-bottom: 2px; }
  .change-label { color: #6c7086; min-width: 36px; }
  .change-old { color: #f38ba8; text-decoration: line-through; }
  .change-arrow { color: #585b70; }
  .change-new { color: #a6e3a1; }
  .change-revert { font-size: 9px; padding: 1px 6px; margin-top: 5px; }
  #schema-panel { flex: 1; display: flex; flex-direction: column; gap: 10px; overflow: hidden; padding: 14px; }
  h2 { margin: 0 0 6px; font-size: 11px; text-transform: uppercase; letter-spacing: 0.08em; color: #89b4fa; }
  #schema-view { display: flex; flex-direction: column; flex: 1; overflow: hidden; gap: 10px; }
  #schema-yaml { flex: 1; background: #181825; border: 1px solid #313244; border-radius: 5px; padding: 10px; font-size: 11px; white-space: pre; overflow: auto; color: #a6e3a1; min-height: 0; }
  #analyze-section { border-top: 1px solid #313244; padding-top: 10px; }
  .issue { padding: 5px 8px; border-radius: 4px; margin-bottom: 5px; font-size: 11px; }
  .issue.ERROR   { background: #3a1e1e; border-left: 3px solid #f38ba8; color: #f38ba8; }
  .issue.WARNING { background: #3a2e1e; border-left: 3px solid #fab387; color: #fab387; }
  .issue.clean   { background: #1e3a2e; border-left: 3px solid #a6e3a1; color: #a6e3a1; }
  .metric { display: inline-block; margin-right: 14px; font-size: 11px; color: #a6adc8; }
  .metric span { color: #cdd6f4; font-weight: bold; }
  button { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px 12px; font-size: 11px; font-family: monospace; cursor: pointer; }
  button:hover { background: #45475a; }
  button:disabled { opacity: 0.4; cursor: default; }
  .view-btn { padding: 3px 9px; font-size: 10px; }
  .view-btn.active { background: #1e3a5f; color: #89b4fa; border-color: #89b4fa; }
  .btn-danger { color: #f38ba8; border-color: #f38ba8; }
  .btn-danger:hover { background: #3a1e1e; }
  .btn-validate { color: #f9e2af; border-color: #f9e2af; }
  .btn-validate:hover { background: #2e2a1e; }
  .toolbar { display: flex; gap: 6px; align-items: center; }
  .path-label { font-size: 10px; color: #585b70; flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  #graph-view { display: flex; flex-direction: column; flex: 1; overflow: hidden; gap: 6px; }
  .graph-toolbar { display: flex; gap: 6px; align-items: center; flex-wrap: wrap; }
  .beh-dropdown { position: relative; }
  #beh-trigger { display: flex; align-items: center; gap: 8px; min-width: 200px; max-width: 320px; }
  #beh-trigger-label { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; text-align: left; }
  .beh-arrow { flex-shrink: 0; font-size: 9px; color: #6c7086; }
  #beh-menu { position: absolute; top: calc(100% + 4px); left: 0; min-width: 280px; background: #181825; border: 1px solid #45475a; border-radius: 5px; z-index: 100; box-shadow: 0 6px 20px rgba(0,0,0,0.5); }
  #beh-search { width: 100%; background: #11111b; border: none; border-bottom: 1px solid #313244; color: #cdd6f4; font-family: monospace; font-size: 11px; padding: 7px 10px; outline: none; border-radius: 5px 5px 0 0; }
  #beh-search::placeholder { color: #585b70; }
  #beh-items { max-height: 240px; overflow-y: auto; }
  .beh-item { padding: 6px 10px; font-size: 11px; cursor: pointer; display: flex; align-items: center; gap: 8px; }
  .beh-item:hover { background: #313244; }
  .beh-item.active { background: #1e3a5f; color: #89b4fa; }
  .beh-item-name { flex: 1; }
  .beh-item-cond { font-size: 10px; color: #6c7086; }
  .graph-body { display: flex; gap: 8px; flex: 1; min-height: 0; }
  #svg-container { flex: 1; overflow: auto; background: #181825; border: 1px solid #313244; border-radius: 5px; padding: 10px; min-height: 0; }
  #edit-panel { width: 196px; background: #181825; border: 1px solid #313244; border-radius: 5px; padding: 10px; display: flex; flex-direction: column; gap: 6px; overflow-y: auto; flex-shrink: 0; }
  #edit-panel label { font-size: 10px; color: #6c7086; margin-top: 4px; display: block; }
  #edit-panel input, #edit-panel select { width: 100%; background: #1e1e2e; border: 1px solid #45475a; border-radius: 3px; color: #cdd6f4; font-family: monospace; font-size: 11px; padding: 4px 6px; }
  #edit-panel input:focus, #edit-panel select:focus { outline: none; border-color: #89b4fa; }
  .edit-row { display: flex; gap: 5px; }
  .edit-row button { flex: 1; padding: 4px 6px; font-size: 10px; }
  #edit-empty { font-size: 11px; color: #585b70; padding: 6px 0; }
  #edit-reg-section { font-size: 10px; padding: 2px 0; }
  #edit-prev-section { border-top: 1px solid #313244; margin-top: 6px; padding-top: 6px; }
  #edit-prev-section h2 { color: #f9e2af; }
  .prev-val { font-size: 11px; color: #a6adc8; padding: 1px 0; }
  #status-bar { padding: 3px 18px; background: #11111b; font-size: 10px; color: #585b70; border-top: 1px solid #313244; }
</style>
</head>
<body>
<div id="header">
  <h1>Arborist &#x2014; Editor</h1>
  <span class="badge disconnected" id="conn-badge">connecting...</span>
  <span id="schema-path" style="font-size:11px;color:#585b70;"></span>
</div>
<div id="main">
  <div class="panel" id="pending-panel">
    <div class="pending-hdr">
      <h2>Pending Changes</h2>
      <span class="badge pending" id="pending-count-badge" style="display:none">0</span>
    </div>
    <div id="pending-list"><em style="color:#585b70;font-size:11px">No pending changes</em></div>
  </div>
  <div id="schema-panel">
    <div class="toolbar">
      <h2 style="margin:0">Schema</h2>
      <button onclick="loadSchema()">&#x21BA; Reload</button>
      <button onclick="runAnalysis()">&#x26A1; Analyze</button>
      <button id="validate-btn" class="btn-validate" onclick="validateChanges()" style="display:none">&#x2714; Validate</button>
      <button class="view-btn active" id="btn-schema" onclick="showView('schema')">YAML</button>
      <button class="view-btn" id="btn-graph" onclick="showView('graph')">Graph</button>
      <span class="path-label" id="file-path-label"></span>
    </div>
    <div id="schema-view">
      <pre id="schema-yaml">loading...</pre>
      <div id="analyze-section" style="display:none">
        <h2>Analysis</h2>
        <div id="metrics-row"></div>
        <div id="issues-list"></div>
      </div>
    </div>
    <div id="graph-view" style="display:none">
      <div class="graph-toolbar">
        <div class="beh-dropdown" id="beh-dropdown">
          <button id="beh-trigger" onclick="toggleBehDropdown()">
            <span id="beh-trigger-label">select behavior</span>
            <span class="beh-arrow">&#x25BE;</span>
          </button>
          <div id="beh-menu" style="display:none">
            <input id="beh-search" type="text" placeholder="Search behaviors..." oninput="filterBehItems()">
            <div id="beh-items"></div>
          </div>
        </div>
        <button onclick="addBehavior()">+ Behavior</button>
        <button id="save-btn" onclick="saveTree()">Save Schema</button>
      </div>
      <div class="graph-body">
        <div id="svg-container"><em style="color:#585b70;font-size:11px">Click Graph to render</em></div>
        <div id="edit-panel">
          <div id="edit-empty">Select a node</div>
          <div id="edit-form" style="display:none">
            <h2>Edit Node</h2>
            <label>Type</label>
            <select id="edit-type" onchange="onEditTypeChange()">
              <option value="action">action</option>
              <option value="condition">condition</option>
              <option value="sequence">sequence</option>
              <option value="selector">selector</option>
              <option value="parallel">parallel</option>
            </select>
            <label>Name</label>
            <input id="edit-name" type="text" placeholder="node_name">
            <div id="edit-policy-row" style="display:none">
              <label>Policy</label>
              <select id="edit-policy">
                <option value="all">all</option>
                <option value="any">any</option>
                <option value="threshold">threshold</option>
              </select>
            </div>
            <div class="edit-row" style="margin-top:6px">
              <button onclick="applyNodeEdit()">Apply</button>
              <button class="btn-danger" onclick="deleteSelectedNode()">Delete</button>
            </div>
            <div id="edit-add-child" style="display:none;margin-top:4px">
              <button style="width:100%" onclick="addChildToSelected()">+ Add Child</button>
            </div>
            <div id="edit-move-row" class="edit-row" style="display:none;margin-top:4px">
              <button onclick="moveSelectedNode(-1)">&#x2190; Left</button>
              <button onclick="moveSelectedNode(1)">Right &#x2192;</button>
            </div>
            <div id="edit-reg-section" style="display:none"></div>
            <div id="edit-prev-section" style="display:none">
              <h2>Previous Values</h2>
              <label>Type</label><div class="prev-val" id="edit-prev-type"></div>
              <label>Name</label><div class="prev-val" id="edit-prev-name"></div>
              <label>Policy</label><div class="prev-val" id="edit-prev-policy"></div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>
<div id="status-bar">Ready</div>
<script>
const setStatus = s => { document.getElementById('status-bar').textContent = s; };

let treeData   = null;
let issueMap   = {};   // path -> 'ERROR' | 'WARNING'
let registeredActions    = new Set();
let registeredConditions = new Set();
let selectedBehavior = 0;
let selectedNodeId   = null;
let latestTickState  = null;   // most recent /api/tickstate response
const TICK_STATUS_COLORS = { SUCCESS: '#a6e3a1', FAILURE: '#f38ba8', RUNNING: '#fab387' };
let isDirty    = false;
let nextNodeId = 0;

function genId() { return nextNodeId++; }

function isLeaf(type) { return type === 'action' || type === 'condition'; }

function nodeRegistered(node) {
  if (node.type === 'action')    { return registeredActions.has(node.name); }
  if (node.type === 'condition') { return registeredConditions.has(node.name); }
  return true;  // composites are always considered registered
}

function collectPendingChanges() {
  if (!treeData) { return []; }
  const changes = [];
  for (let behIdx = 0; behIdx < (treeData.behaviors || []).length; behIdx++) {
    const beh = treeData.behaviors[behIdx];
    if (!beh.root) { continue; }
    (function walk(node) {
      if (node._edited) { changes.push({ behName: beh.name, behIdx, node }); }
      (node.children || []).forEach(walk);
    }(beh.root));
  }
  return changes;
}

function updatePendingPanel() {
  const changes = collectPendingChanges();
  const badge       = document.getElementById('pending-count-badge');
  const list        = document.getElementById('pending-list');
  const validateBtn = document.getElementById('validate-btn');
  badge.textContent    = String(changes.length);
  badge.style.display  = changes.length > 0 ? 'inline-block' : 'none';
  validateBtn.style.display = changes.length > 0 ? 'inline-block' : 'none';
  if (changes.length === 0) {
    list.innerHTML = '<em style="color:#585b70;font-size:11px">No pending changes</em>';
    return;
  }
  list.innerHTML = changes.map(({ behName, node }) => {
    const prev = node._prev || {};
    const typeChanged   = prev.type   !== node.type;
    const nameChanged   = prev.name   !== node.name;
    const policyChanged = node.type === 'parallel' && prev.policy !== node.policy;
    const rows = [
      typeChanged   ? `<div class="change-row"><span class="change-label">type</span><span class="change-old">${prev.type||''}</span><span class="change-arrow">&#x2192;</span><span class="change-new">${node.type}</span></div>` : '',
      nameChanged   ? `<div class="change-row"><span class="change-label">name</span><span class="change-old">${prev.name||''}</span><span class="change-arrow">&#x2192;</span><span class="change-new">${node.name}</span></div>` : '',
      policyChanged ? `<div class="change-row"><span class="change-label">policy</span><span class="change-old">${prev.policy||''}</span><span class="change-arrow">&#x2192;</span><span class="change-new">${node.policy}</span></div>` : '',
    ].join('');
    return `<div class="change-card" onclick="jumpToChange(${node.id})">` +
      `<div class="change-beh">${behName}</div>` +
      `<div class="change-name">${node.name || '(unnamed)'}</div>` +
      rows +
      `<button class="btn-danger change-revert" onclick="event.stopPropagation();revertNode(${node.id})">&#x21BA; Revert</button>` +
      `</div>`;
  }).join('');
}

function jumpToChange(nodeId) {
  if (!treeData) { return; }
  for (let idx = 0; idx < (treeData.behaviors || []).length; idx++) {
    const beh = treeData.behaviors[idx];
    if (beh.root && findNodeById(beh.root, nodeId)) {
      selectedBehavior = idx;
      selectedNodeId   = nodeId;
      showView('graph');
      renderBehaviorTabs();
      renderBehaviorTree(idx);
      updateEditPanel(findNodeById(beh.root, nodeId));
      return;
    }
  }
}

function revertNode(nodeId) {
  if (!treeData) { return; }
  for (const beh of (treeData.behaviors || [])) {
    if (!beh.root) { continue; }
    const node = findNodeById(beh.root, nodeId);
    if (node && node._edited) {
      const prev  = node._prev;
      node.type   = prev.type;
      node.name   = prev.name;
      node.policy = prev.policy;
      node.children = prev.children;
      delete node._edited;
      delete node._prev;
      updatePendingPanel();
      renderBehaviorTabs();
      renderBehaviorTree(selectedBehavior);
      if (selectedNodeId === nodeId) { updateEditPanel(findNodeById(currentRoot(), nodeId)); }
      return;
    }
  }
}

async function validateChanges() {
  if (!treeData) { return; }
  function clearEdited(node) {
    delete node._edited;
    delete node._prev;
    (node.children || []).forEach(clearEdited);
  }
  (treeData.behaviors || []).forEach(beh => { if (beh.root) { clearEdited(beh.root); } });
  await saveTree();
  updatePendingPanel();
  renderBehaviorTree(selectedBehavior);
  if (selectedNodeId !== null) { updateEditPanel(findNodeById(currentRoot(), selectedNodeId)); }
}

function isComposite(type) {
  return type === 'sequence' || type === 'selector' || type === 'parallel';
}

function findNodeById(node, targetId) {
  if (node.id === targetId) { return node; }
  for (const child of (node.children || [])) {
    const found = findNodeById(child, targetId);
    if (found) { return found; }
  }
  return null;
}

function findParentOf(node, targetId) {
  for (let childIdx = 0; childIdx < (node.children || []).length; childIdx++) {
    if (node.children[childIdx].id === targetId) { return { parent: node, index: childIdx }; }
    const result = findParentOf(node.children[childIdx], targetId);
    if (result) { return result; }
  }
  return null;
}

function currentRoot() {
  if (!treeData || !treeData.behaviors[selectedBehavior]) { return null; }
  return treeData.behaviors[selectedBehavior].root;
}

function showView(view) {
  const isSchema = view === 'schema';
  document.getElementById('schema-view').style.display = isSchema ? 'flex' : 'none';
  document.getElementById('graph-view').style.display  = isSchema ? 'none' : 'flex';
  document.getElementById('btn-schema').classList.toggle('active', isSchema);
  document.getElementById('btn-graph').classList.toggle('active', !isSchema);
  if (!isSchema && !treeData) { loadTree(); }
}

async function loadSchema() {
  try {
    const res  = await fetch('/api/schema');
    const data = await res.json();
    document.getElementById('conn-badge').textContent = 'connected';
    document.getElementById('conn-badge').classList.remove('disconnected');
    document.getElementById('schema-yaml').textContent = data.yaml || '(no schema file configured)';
    document.getElementById('file-path-label').textContent = data.path || '';
    document.getElementById('schema-path').textContent = data.path ? '&#x2014; ' + data.path : '';
    treeData = null;
    isDirty  = false;
    document.getElementById('save-btn').textContent = 'Save Schema';
    document.getElementById('save-btn').style.color = '';
    setStatus('Schema loaded from ' + (data.path || 'memory'));
  } catch (e) {
    document.getElementById('conn-badge').textContent = 'disconnected';
    document.getElementById('conn-badge').classList.add('disconnected');
    document.getElementById('schema-yaml').textContent = 'Error: ' + e.message;
    setStatus('Schema load failed: ' + e.message);
  }
}

async function runAnalysis() {
  setStatus('Running analysis...');
  try {
    const res  = await fetch('/api/analyze');
    const data = await res.json();
    document.getElementById('analyze-section').style.display = 'block';
    if (data.error) {
      document.getElementById('issues-list').innerHTML = `<div class="issue ERROR">${data.error}</div>`;
      document.getElementById('metrics-row').innerHTML = '';
      setStatus('Analysis error: ' + data.error);
      return;
    }
    const m = data.metrics || {};
    document.getElementById('metrics-row').innerHTML =
      `<span class="metric">nodes <span>${m.totalNodes||0}</span></span>` +
      `<span class="metric">max depth <span>${m.maxDepth||0}</span></span>` +
      `<span class="metric">avg branching <span>${(m.avgBranchingFactor||0).toFixed(1)}</span></span>`;
    const issues = data.issues || [];
    document.getElementById('issues-list').innerHTML = issues.length === 0
      ? '<div class="issue clean">No issues detected</div>'
      : issues.map(i => `<div class="issue ${i.severity}"><strong>${i.severity}</strong> [${i.path}] ${i.message}</div>`).join('');
    setStatus('Analysis complete — ' + issues.length + ' issue(s)');
  } catch (e) { setStatus('Analysis failed: ' + e.message); }
}

async function loadTree() {
  try {
    const [treeRes, analyzeRes, actionsRes, conditionsRes] = await Promise.all([
      fetch('/api/tree'), fetch('/api/analyze'),
      fetch('/api/actions'), fetch('/api/conditions')
    ]);
    const data    = await treeRes.json();
    const analyze = await analyzeRes.json();
    const actions    = await actionsRes.json();
    const conditions = await conditionsRes.json();
    registeredActions    = new Set((actions    || []).map(a => a.name));
    registeredConditions = new Set((conditions || []).map(c => c.name));
    if (data.error) {
      document.getElementById('svg-container').innerHTML =
        `<p style="color:#f38ba8;padding:14px;font-size:11px">Error: ${data.error}</p>`;
      setStatus('Tree error: ' + data.error);
      return;
    }
    treeData = data;
    // Build issue map: path -> worst severity
    issueMap = {};
    for (const issue of (analyze.issues || [])) {
      const existing = issueMap[issue.path];
      if (!existing || (issue.severity === 'ERROR')) { issueMap[issue.path] = issue.severity; }
    }
    nextNodeId = 0;
    function scanIds(node) {
      if (node.id >= nextNodeId) { nextNodeId = node.id + 1; }
      (node.children||[]).forEach(scanIds);
    }
    (treeData.behaviors||[]).forEach(beh => { if (beh.root) { scanIds(beh.root); } });
    selectedBehavior = 0;
    selectedNodeId   = null;
    updateEditPanel(null);
    renderBehaviorTabs();
    renderBehaviorTree(0);
    const issueCount = Object.keys(issueMap).length;
    setStatus('Tree loaded — ' + (treeData.behaviors||[]).length + ' behavior(s)' +
              (issueCount > 0 ? ', ' + issueCount + ' path(s) with issues' : ''));
  } catch (e) {
    document.getElementById('svg-container').innerHTML =
      `<p style="color:#f38ba8;padding:14px;font-size:11px">Failed: ${e.message}</p>`;
    setStatus('Tree load failed: ' + e.message);
  }
}

function behaviorIssues(beh) {
  if (!beh.root) { return ''; }
  const all = [];
  function collect(n) { all.push(n); (n.children||[]).forEach(collect); }
  collect(beh.root);
  const hasPaths = all.some(n => issueMap[n.path]);
  const hasRoot  = issueMap['root'];
  const sev = hasPaths ? (all.some(n => issueMap[n.path] === 'ERROR') ? 'ERROR' : 'WARNING')
                       : (hasRoot || null);
  if (!sev) { return ''; }
  const col = sev === 'ERROR' ? '#f38ba8' : '#fab387';
  return ` <span style="color:${col};font-size:9px">&#x25CF;</span>`;
}

let behMenuOpen = false;

function toggleBehDropdown() {
  behMenuOpen = !behMenuOpen;
  document.getElementById('beh-menu').style.display = behMenuOpen ? 'block' : 'none';
  if (behMenuOpen) {
    const search = document.getElementById('beh-search');
    search.value = '';
    filterBehItems();
    search.focus();
  }
}

function closeBehDropdown() {
  behMenuOpen = false;
  document.getElementById('beh-menu').style.display = 'none';
}

function filterBehItems() {
  const query = document.getElementById('beh-search').value.toLowerCase();
  document.querySelectorAll('.beh-item').forEach(el => {
    el.style.display = el.dataset.name.toLowerCase().includes(query) ? 'flex' : 'none';
  });
}

document.addEventListener('click', e => {
  if (behMenuOpen && !document.getElementById('beh-dropdown').contains(e.target)) {
    closeBehDropdown();
  }
});

function renderBehaviorTabs() {
  if (!treeData) { return; }
  const beh = treeData.behaviors[selectedBehavior];
  const label = beh ? beh.name + (beh.condition ? ' [' + beh.condition + ']' : '') : 'select behavior';
  document.getElementById('beh-trigger-label').textContent = label;
  document.getElementById('beh-items').innerHTML =
    (treeData.behaviors || []).map((b, idx) =>
      `<div class="beh-item${idx === selectedBehavior ? ' active' : ''}" data-name="${b.name}" onclick="selectBehavior(${idx})">` +
      `<span class="beh-item-name">${b.name}</span>` +
      (b.condition ? `<span class="beh-item-cond">[${b.condition}]</span>` : '') +
      behaviorIssues(b) +
      `</div>`
    ).join('');
}

function selectBehavior(idx) {
  selectedBehavior = idx;
  selectedNodeId   = null;
  updateEditPanel(null);
  closeBehDropdown();
  renderBehaviorTabs();
  renderBehaviorTree(idx);
}

function selectNode(nodeId) {
  selectedNodeId = nodeId;
  const root = currentRoot();
  const node = root ? findNodeById(root, nodeId) : null;
  updateEditPanel(node);
  renderBehaviorTree(selectedBehavior);
}

function updateEditPanel(node) {
  const emptyEl = document.getElementById('edit-empty');
  const formEl  = document.getElementById('edit-form');
  if (!node) {
    emptyEl.style.display = 'block';
    formEl.style.display  = 'none';
    document.getElementById('edit-reg-section').style.display  = 'none';
    document.getElementById('edit-prev-section').style.display = 'none';
    return;
  }
  emptyEl.style.display = 'none';
  formEl.style.display  = 'flex';
  formEl.style.flexDirection = 'column';
  document.getElementById('edit-type').value   = node.type   || 'action';
  document.getElementById('edit-name').value   = node.name   || '';
  document.getElementById('edit-policy').value = node.policy || 'all';
  onEditTypeChange();
  const root       = currentRoot();
  const parentInfo = root ? findParentOf(root, node.id) : null;
  document.getElementById('edit-move-row').style.display = parentInfo ? 'flex' : 'none';
  const regSection = document.getElementById('edit-reg-section');
  if (isLeaf(node.type)) {
    const registered = nodeRegistered(node);
    regSection.style.display = 'block';
    regSection.innerHTML =
      `<span style="color:${registered ? '#a6e3a1' : '#f38ba8'};font-size:10px">` +
      `${registered ? '&#x25CF; registered' : '&#x25CF; not registered'}</span>`;
  } else {
    regSection.style.display = 'none';
  }
  const prevSection = document.getElementById('edit-prev-section');
  if (node._edited && node._prev) {
    prevSection.style.display = 'block';
    document.getElementById('edit-prev-type').textContent   = node._prev.type   || '';
    document.getElementById('edit-prev-name').textContent   = node._prev.name   || '';
    document.getElementById('edit-prev-policy').textContent = node._prev.policy || 'all';
  } else {
    prevSection.style.display = 'none';
  }
}

function onEditTypeChange() {
  const type = document.getElementById('edit-type').value;
  document.getElementById('edit-policy-row').style.display  = type === 'parallel'   ? 'block' : 'none';
  document.getElementById('edit-add-child').style.display   = isComposite(type)     ? 'block' : 'none';
}

function applyNodeEdit() {
  if (selectedNodeId === null) { return; }
  const root = currentRoot();
  if (!root) { return; }
  const node = findNodeById(root, selectedNodeId);
  if (!node) { return; }
  if (!node._edited) {
    node._prev    = { type: node.type, name: node.name, policy: node.policy, children: node.children ? node.children.slice() : [] };
    node._edited  = true;
  }
  node.type   = document.getElementById('edit-type').value;
  node.name   = document.getElementById('edit-name').value.trim();
  node.policy = document.getElementById('edit-policy').value;
  if (!isComposite(node.type)) { node.children = []; }
  markDirty();
  updatePendingPanel();
  renderBehaviorTree(selectedBehavior);
  updateEditPanel(findNodeById(currentRoot(), selectedNodeId));
}

function deleteSelectedNode() {
  if (selectedNodeId === null) { return; }
  const root = currentRoot();
  if (!root || root.id === selectedNodeId) { setStatus('Cannot delete the root node'); return; }
  const result = findParentOf(root, selectedNodeId);
  if (!result) { return; }
  result.parent.children.splice(result.index, 1);
  selectedNodeId = null;
  updateEditPanel(null);
  markDirty();
  renderBehaviorTree(selectedBehavior);
}

function addChildToSelected() {
  if (selectedNodeId === null) { return; }
  const root = currentRoot();
  if (!root) { return; }
  const node = findNodeById(root, selectedNodeId);
  if (!node || !isComposite(node.type)) { return; }
  node.children = node.children || [];
  node.children.push({ id: genId(), type: 'action', name: 'new_action', children: [] });
  markDirty();
  renderBehaviorTree(selectedBehavior);
}

function moveSelectedNode(dir) {
  if (selectedNodeId === null) { return; }
  const root = currentRoot();
  if (!root) { return; }
  const result = findParentOf(root, selectedNodeId);
  if (!result) { return; }
  const { parent, index } = result;
  const newIdx = index + dir;
  if (newIdx < 0 || newIdx >= parent.children.length) { return; }
  const tmp = parent.children[index];
  parent.children[index]  = parent.children[newIdx];
  parent.children[newIdx] = tmp;
  markDirty();
  renderBehaviorTree(selectedBehavior);
}

function addBehavior() {
  const rawName = window.prompt('Behavior name:', 'new_behavior');
  if (!rawName) { return; }
  const behName = rawName.trim();
  if (!treeData) { treeData = { behaviors: [] }; }
  treeData.behaviors.push({
    name: behName, condition: '', interruptible: true,
    root: { id: genId(), type: 'sequence', name: 'root', children: [] }
  });
  selectedBehavior = treeData.behaviors.length - 1;
  selectedNodeId   = null;
  updateEditPanel(null);
  markDirty();
  renderBehaviorTabs();
  renderBehaviorTree(selectedBehavior);
}

function markDirty() {
  isDirty = true;
  const btn = document.getElementById('save-btn');
  btn.textContent  = 'Save Schema *';
  btn.style.color  = '#fab387';
}

async function saveTree() {
  if (!treeData) { setStatus('Nothing to save'); return; }
  const yamlText = treeToYaml(treeData);
  try {
    const res  = await fetch('/api/schema', {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ yaml: yamlText })
    });
    const data = await res.json();
    if (data.error) {
      setStatus('Save failed: ' + data.error);
    } else {
      isDirty = false;
      const btn = document.getElementById('save-btn');
      btn.textContent = 'Save Schema';
      btn.style.color = '';
      setStatus('Schema saved');
    }
  } catch (e) { setStatus('Save failed: ' + e.message); }
}

function nodeToYaml(node, indent) {
  let out = indent + 'type: ' + node.type + '\n';
  out += indent + 'name: ' + (node.name || '') + '\n';
  if (node.intent) { out += indent + 'intent: ' + node.intent + '\n'; }
  if (node.type === 'parallel') {
    const pol = node.policy || 'all';
    out += indent + 'policy: ' + pol + '\n';
    if (pol === 'threshold') { out += indent + 'threshold: ' + (node.threshold || 1) + '\n'; }
  }
  if (node.children && node.children.length > 0) {
    out += indent + 'children:\n';
    for (const child of node.children) {
      const childYaml = nodeToYaml(child, indent + '    ');
      out += indent + '  - ' + childYaml.slice(indent.length + 4);
    }
  }
  return out;
}

function treeToYaml(data) {
  let yaml = 'schema_version: "1.0"\nbehaviors:\n';
  for (const beh of (data.behaviors || [])) {
    yaml += '  - name: ' + beh.name + '\n';
    if (beh.condition) { yaml += '    when: ' + beh.condition + '\n'; }
    if (beh.interruptible === false) { yaml += '    interruptible: false\n'; }
    if (beh.root) {
      yaml += '    tree:\n';
      yaml += nodeToYaml(beh.root, '      ');
    }
  }
  return yaml;
}

const NODE_W = 120, NODE_H = 40, V_GAP = 56, H_GAP = 16;

function computeWidth(node) {
  if (!node.children || node.children.length === 0) { node._w = NODE_W; return; }
  node.children.forEach(computeWidth);
  const childW = node.children.reduce((s, c) => s + c._w, 0) + H_GAP * (node.children.length - 1);
  node._w = Math.max(NODE_W, childW);
}

function assignPos(node, posX, posY) {
  node._x = posX; node._y = posY;
  if (!node.children || node.children.length === 0) { return; }
  const childW = node.children.reduce((s, c) => s + c._w, 0) + H_GAP * (node.children.length - 1);
  let cx = posX - childW / 2;
  for (const child of node.children) {
    assignPos(child, cx + child._w / 2, posY + NODE_H + V_GAP);
    cx += child._w + H_GAP;
  }
}

const NODE_COLORS = {
  sequence:'#89b4fa', selector:'#cba6f7', parallel:'#94e2d5',
  action:'#a6e3a1', condition:'#fab387'
};

function renderBehaviorTree(idx) {
  if (!treeData || !treeData.behaviors || !treeData.behaviors[idx]) { return; }
  const beh  = treeData.behaviors[idx];
  const root = beh.root;
  if (!root) {
    document.getElementById('svg-container').innerHTML =
      '<p style="padding:14px;font-size:11px;color:#585b70">No tree data</p>';
    return;
  }
  computeWidth(root);
  const svgW = root._w + 60;
  assignPos(root, svgW / 2, 24);
  const all = [];
  function collect(n) { all.push(n); (n.children||[]).forEach(collect); }
  collect(root);
  const svgH = Math.max(...all.map(n => n._y)) + NODE_H + 30;
  let edges = '', nodes = '';
  for (const node of all) {
    for (const child of (node.children||[])) {
      edges += `<line x1="${node._x}" y1="${node._y+NODE_H}" x2="${child._x}" y2="${child._y}" stroke="#45475a" stroke-width="1.5"/>`;
    }
  }
  const ISSUE_COLORS = { ERROR: '#f38ba8', WARNING: '#fab387' };
  // Build active-path lookup from latest tick state for overlay.
  const activePathMap = {};
  if (latestTickState && latestTickState.activePath) {
    for (const ap of latestTickState.activePath) { activePathMap[ap.name] = ap.status; }
  }
  for (const node of all) {
    const col     = node._edited ? '#f9e2af' : (NODE_COLORS[node.type] || '#cdd6f4');
    const nx      = node._x - NODE_W / 2;
    const nm      = (node.name||'').length > 14 ? (node.name||'').slice(0,13)+'…' : (node.name||'');
    const sel     = node.id === selectedNodeId;
    const issueSev = issueMap[node.path];
    const issueCol = issueSev ? ISSUE_COLORS[issueSev] : null;
    const strW    = sel ? '2.5' : '1.5';
    const activeStatus = activePathMap[node.name];
    const activeCol    = activeStatus ? TICK_STATUS_COLORS[activeStatus] : null;
    if (activeCol) {
      nodes += `<rect x="${nx-4}" y="${node._y-4}" width="${NODE_W+8}" height="${NODE_H+8}" rx="9" fill="${activeCol}" opacity="0.12"/>`;
      nodes += `<rect x="${nx-4}" y="${node._y-4}" width="${NODE_W+8}" height="${NODE_H+8}" rx="9" fill="none" stroke="${activeCol}" stroke-width="2.5" opacity="0.85"/>`;
    }
    if (issueCol) {
      nodes += `<rect x="${nx-4}" y="${node._y-4}" width="${NODE_W+8}" height="${NODE_H+8}" rx="9" fill="${issueCol}" opacity="0.15"/>`;
      nodes += `<rect x="${nx-4}" y="${node._y-4}" width="${NODE_W+8}" height="${NODE_H+8}" rx="9" fill="none" stroke="${issueCol}" stroke-width="1.5" opacity="0.6"/>`;
    }
    if (sel) {
      nodes += `<rect x="${nx-3}" y="${node._y-3}" width="${NODE_W+6}" height="${NODE_H+6}" rx="8" fill="none" stroke="${col}" stroke-width="1" opacity="0.4"/>`;
    }
    nodes += `<rect x="${nx}" y="${node._y}" width="${NODE_W}" height="${NODE_H}" rx="6" fill="#181825" stroke="${col}" stroke-width="${strW}" style="cursor:pointer" onclick="selectNode(${node.id})"/>`;
    nodes += `<text x="${node._x}" y="${node._y+14}" text-anchor="middle" fill="${col}" font-size="9" font-family="monospace" pointer-events="none">${(node.type||'').toUpperCase()}</text>`;
    nodes += `<text x="${node._x}" y="${node._y+29}" text-anchor="middle" fill="#cdd6f4" font-size="11" font-family="monospace" pointer-events="none">${nm}</text>`;
    if (isLeaf(node.type)) {
      const regCol = nodeRegistered(node) ? '#a6e3a1' : '#f38ba8';
      const regX   = nx + 6;
      const regY   = node._y + 6;
      nodes += `<circle cx="${regX}" cy="${regY}" r="4" fill="${regCol}" opacity="0.9"/>`;
    }
    if (issueCol) {
      nodes += `<circle cx="${nx+NODE_W-4}" cy="${node._y+4}" r="5" fill="${issueCol}"/>`;
      nodes += `<text x="${nx+NODE_W-4}" y="${node._y+8}" text-anchor="middle" fill="#1e1e2e" font-size="8" font-family="monospace" font-weight="bold" pointer-events="none">${issueSev === 'ERROR' ? '!' : '?'}</text>`;
    }
  }
  document.getElementById('svg-container').innerHTML =
    `<svg width="${svgW}" height="${svgH}">${edges}${nodes}</svg>`;
}

async function pollTickState() {
  try {
    const res = await fetch('/api/tickstate');
    const ts  = await res.json();
    if (ts.tick > 0) {
      const changed = !latestTickState || latestTickState.tick !== ts.tick;
      latestTickState = ts;
      // Only re-render when the graph view is visible, no pending edits, and
      // the tick actually advanced — avoid disrupting user interaction.
      const graphVisible = document.getElementById('graph-view').style.display !== 'none';
      if (changed && treeData && graphVisible && !isDirty && selectedNodeId === null) {
        renderBehaviorTree(selectedBehavior);
      }
    }
  } catch (_) {}
}

setInterval(pollTickState, 500);
loadSchema();
</script>
</body>
</html>)html";

// ── JSON helpers ───────────────────────────────────────────────────────────────

namespace {

std::string jsonString(std::string_view str) {
    std::string out = "\"";
    for (char chr : str) {
        if (chr == '"')       { out += "\\\""; }
        else if (chr == '\\') { out += "\\\\"; }
        else if (chr == '\n') { out += "\\n";  }
        else if (chr == '\r') { out += "\\r";  }
        else if (chr == '\t') { out += "\\t";  }
        else                  { out += chr;    }
    }
    out += '"';
    return out;
}

std::string jsonStringArray(const std::vector<std::string>& vec) {
    std::string out = "[";
    for (std::size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) { out += ','; }
        out += jsonString(vec[i]);
    }
    out += ']';
    return out;
}

std::string actionsToJson(const std::vector<ActionSpec>& actions) {
    std::string out = "[";
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) { out += ','; }
        const auto& act = actions[i];
        out += R"({"name":)";         out += jsonString(act.name);
        out += R"(,"intent":)";       out += jsonString(act.intent);
        out += R"(,"reads":)";        out += jsonStringArray(act.reads);
        out += R"(,"writes":)";       out += jsonStringArray(act.writes);
        out += '}';
    }
    out += ']';
    return out;
}

std::string conditionsToJson(const std::vector<ConditionSpec>& conditions) {
    std::string out = "[";
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        if (i > 0) { out += ','; }
        const auto& cond = conditions[i];
        out += R"({"name":)";   out += jsonString(cond.name);
        out += R"(,"intent":)"; out += jsonString(cond.intent);
        out += R"(,"reads":)";  out += jsonStringArray(cond.reads);
        out += '}';
    }
    out += ']';
    return out;
}

std::string blackboardToJson(const std::vector<StateKeySpec>& keys) {
    std::string out = "[";
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) { out += ','; }
        out += R"({"key":)";  out += jsonString(keys[i].key);
        out += R"(,"type":)"; out += jsonString(keys[i].type);
        out += '}';
    }
    out += ']';
    return out;
}

std::string readFile(const std::string& path) {
    if (path.empty()) { return ""; }
    std::ifstream file(path);
    if (!file.is_open()) { return ""; }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool writeFile(const std::string& path, std::string_view content) {
    if (path.empty()) { return false; }
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) { return false; }
    file << content;
    return file.good();
}

// Parse a single JSON string value for a given key, e.g. "yaml":"..."
// Returns empty string if key not found. Simple extraction, not a full parser.
std::string extractJsonStringField(const std::string& json, std::string_view key) {
    const std::string search = "\"" + std::string(key) + "\"";
    const auto keyPos = json.find(search);
    if (keyPos == std::string::npos) { return ""; }
    const auto colonPos = json.find(':', keyPos + search.size());
    if (colonPos == std::string::npos) { return ""; }
    auto start = json.find('"', colonPos + 1);
    if (start == std::string::npos) { return ""; }
    ++start;
    std::string result;
    bool escape = false;
    for (std::size_t i = start; i < json.size(); ++i) {
        if (escape) {
            if      (json[i] == '"')  { result += '"';  }
            else if (json[i] == '\\') { result += '\\'; }
            else if (json[i] == 'n')  { result += '\n'; }
            else if (json[i] == 'r')  { result += '\r'; }
            else if (json[i] == 't')  { result += '\t'; }
            else                      { result += json[i]; }
            escape = false;
        } else if (json[i] == '\\') {
            escape = true;
        } else if (json[i] == '"') {
            break;
        } else {
            result += json[i];
        }
    }
    return result;
}

// Parse one JSON string starting just after its opening quote; advances pos past closing quote.
std::string parseJsonStr(const std::string& json, std::size_t& pos) {
    std::string item;
    bool esc = false;
    while (pos < json.size()) {
        if (esc) {
            if      (json[pos] == '"')  { item += '"';  }
            else if (json[pos] == '\\') { item += '\\'; }
            else if (json[pos] == 'n')  { item += '\n'; }
            else                        { item += json[pos]; }
            esc = false;
        } else if (json[pos] == '\\') {
            esc = true;
        } else if (json[pos] == '"') {
            ++pos; break;
        } else {
            item += json[pos];
        }
        ++pos;
    }
    return item;
}

std::vector<std::string> extractJsonStringArray(const std::string& json, std::string_view key) {
    const std::string search = "\"" + std::string(key) + "\"";
    const auto keyPos = json.find(search);
    if (keyPos == std::string::npos) { return {}; }
    const auto colonPos = json.find(':', keyPos + search.size());
    if (colonPos == std::string::npos) { return {}; }
    const auto arrStart = json.find('[', colonPos + 1);
    if (arrStart == std::string::npos) { return {}; }
    std::vector<std::string> result;
    std::size_t pos = arrStart + 1;
    while (pos < json.size()) {
        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) != 0)) { ++pos; }
        if (pos >= json.size() || json[pos] == ']') { break; }
        if (json[pos] != '"') { ++pos; continue; }
        ++pos;
        result.push_back(parseJsonStr(json, pos));
        while (pos < json.size() && json[pos] != ',' && json[pos] != ']') { ++pos; }
        if (pos < json.size() && json[pos] == ',') { ++pos; }
    }
    return result;
}

std::string_view schemaNodeTypeName(SchemaNodeType type) {
    switch (type) {
        case SchemaNodeType::SEQUENCE:  return "sequence";
        case SchemaNodeType::SELECTOR:  return "selector";
        case SchemaNodeType::PARALLEL:  return "parallel";
        case SchemaNodeType::ACTION:    return "action";
        case SchemaNodeType::CONDITION: return "condition";
    }
    return "unknown";
}

std::string schemaNodeToJson(const SchemaNode& node, std::size_t& nextId,
                              const std::string& path) {
    const std::size_t myId = nextId++;
    std::string out = R"({"id":)";
    out += std::to_string(myId);
    out += R"(,"type":)";   out += jsonString(schemaNodeTypeName(node.type));
    out += R"(,"name":)";   out += jsonString(node.name);
    out += R"(,"path":)";   out += jsonString(path);
    out += R"(,"intent":)"; out += jsonString(node.intent);
    if (node.type == SchemaNodeType::PARALLEL) {
        std::string_view policyStr = "all";
        if      (node.policy == SchemaPolicy::ANY)       { policyStr = "any"; }
        else if (node.policy == SchemaPolicy::THRESHOLD) { policyStr = "threshold"; }
        out += R"(,"policy":)";    out += jsonString(policyStr);
        out += R"(,"threshold":)"; out += std::to_string(node.threshold);
    }
    out += R"(,"children":[)";
    for (std::size_t idx = 0; idx < node.children.size(); ++idx) {
        if (idx > 0) { out += ','; }
        out += schemaNodeToJson(*node.children[idx], nextId,
                                path + "/" + node.children[idx]->name);
    }
    out += "]}";
    return out;
}

std::string schemaDocToTreeJson(const SchemaDoc& doc) {
    std::string out = R"({"behaviors":[)";
    std::size_t nextId = 0;
    for (std::size_t i = 0; i < doc.behaviors.size(); ++i) {
        if (i > 0) { out += ','; }
        const auto& beh = doc.behaviors[i];
        out += R"({"name":)";          out += jsonString(beh.name);
        out += R"(,"condition":)";     out += jsonString(beh.condition);
        out += R"(,"interruptible":)"; out += (beh.interruptible ? "true" : "false");
        out += R"(,"root":)";
        if (beh.tree) {
            out += schemaNodeToJson(*beh.tree, nextId, beh.tree->name);
        } else {
            out += "null";
        }
        out += '}';
    }
    out += "]}";
    return out;
}

std::string analyzeSchema(const std::string& yamlContent,
                           const std::vector<ActionSpec>& actions,
                           const std::vector<ConditionSpec>& conditions) {
    if (yamlContent.empty()) {
        return R"({"error":"no schema file configured"})";
    }

    SchemaDoc doc;
    try {
        doc = SchemaParser::parse(yamlContent);
    } catch (const std::exception& exc) {
        return R"({"error":)" + jsonString(exc.what()) + '}';
    }

    // Build mock registry from declared contracts.
    LoaderRegistry reg;
    for (const auto& action : actions) {
        reg.actions[action.name] = [] { return Status::SUCCESS; };
    }
    for (const auto& condition : conditions) {
        reg.conditions[condition.name] = [] { return false; };
    }

    std::optional<BehaviorTree> treeOpt;
    try {
        treeOpt = SchemaLoader::load(doc, reg);
    } catch (const std::exception& exc) {
        return R"({"error":)" + jsonString(exc.what()) + '}';
    }

    const auto report = ComplexityAnalyzer::analyze(*treeOpt);

    std::string out = R"({"metrics":{"totalNodes":)";
    out += std::to_string(report.totalNodes);
    out += R"(,"maxDepth":)";
    out += std::to_string(report.maxDepth);
    out += R"(,"maxWidth":)";
    out += std::to_string(report.maxWidth);
    out += R"(,"avgBranchingFactor":)";
    out += std::to_string(report.avgBranchingFactor);
    out += R"(},"issues":[)";

    for (std::size_t i = 0; i < report.issues.size(); ++i) {
        if (i > 0) { out += ','; }
        const auto& issue = report.issues[i];
        const std::string_view sev = issue.isError() ? "ERROR" : "WARNING";
        out += R"({"severity":)"; out += jsonString(sev);
        out += R"(,"path":)";     out += jsonString(issue.nodePath);
        out += R"(,"message":)";  out += jsonString(issue.message);
        out += '}';
    }
    out += "]}";
    return out;
}

}  // namespace

// ── EditorServer::Impl ────────────────────────────────────────────────────────

struct EditorServer::Impl {
    httplib::Server server;
    std::thread     thread;
};

// ── EditorServer ──────────────────────────────────────────────────────────────

EditorServer::EditorServer(RegistryStore& store, std::string_view schemaPath)
    : store_(&store), schemaPath_(schemaPath), impl_(std::make_unique<Impl>()) {}

EditorServer::~EditorServer() {
    if (running_) { stop(); }
}

std::string EditorServer::getActionsJson() const {
    return actionsToJson(store_->allActions());
}

std::string EditorServer::getConditionsJson() const {
    return conditionsToJson(store_->allConditions());
}

std::string EditorServer::getBlackboardJson() const {
    return blackboardToJson(store_->allStateKeys());
}

std::string EditorServer::getSchemaJson() const {
    const std::string yaml = readFile(schemaPath_);
    std::string out = R"({"path":)";
    out += jsonString(schemaPath_);
    out += R"(,"yaml":)";
    out += jsonString(yaml);
    out += '}';
    return out;
}

std::string EditorServer::getAnalyzeJson() const {
    const std::string yaml = readFile(schemaPath_);
    return analyzeSchema(yaml, store_->allActions(), store_->allConditions());
}

std::string EditorServer::getTreeJson() const {
    const std::string yaml = readFile(schemaPath_);
    if (yaml.empty()) {
        return R"({"error":"no schema file configured"})";
    }
    try {
        const SchemaDoc doc = SchemaParser::parse(yaml);
        return schemaDocToTreeJson(doc);
    } catch (const std::exception& exc) {
        return R"({"error":)" + jsonString(exc.what()) + '}';
    }
}

void EditorServer::attachTree(BehaviorTree* tree,
                               const LoaderRegistry& reg) noexcept {
    attachedTree_ = tree;
    attachedReg_  = &reg;
}

void EditorServer::attachEmitter(DecisionEmitter* emitter) noexcept {
    attachedEmitter_ = emitter;
}

std::string EditorServer::getTickStateJson() const {
    if (attachedEmitter_ == nullptr || attachedEmitter_->history().empty()) {
        return R"({"tick":0,"behavior":"","status":"RUNNING","activePath":[]})";
    }
    const auto& rec = attachedEmitter_->history().back();
    std::string out = R"({"tick":)";
    out += std::to_string(rec.tickNumber);
    out += R"(,"behavior":")";
    out += rec.behaviorName;
    out += R"(","status":")";
    switch (rec.result) {
        case Status::SUCCESS: out += "SUCCESS"; break;
        case Status::FAILURE: out += "FAILURE"; break;
        case Status::RUNNING: out += "RUNNING"; break;
    }
    out += R"(","activePath":[)";
    for (std::size_t i = 0; i < rec.activePath.size(); ++i) {
        if (i > 0) { out += ','; }
        out += R"({"name":")";
        out += rec.activePath[i].name;
        out += R"(","status":")";
        switch (rec.activePath[i].status) {
            case Status::SUCCESS: out += "SUCCESS"; break;
            case Status::FAILURE: out += "FAILURE"; break;
            case Status::RUNNING: out += "RUNNING"; break;
        }
        out += R"("})";
    }
    out += "]}";
    return out;
}

bool EditorServer::saveSchema(std::string_view yaml) {
    if (!writeFile(schemaPath_, yaml)) { return false; }
    if (attachedTree_ != nullptr) {
        try {
            attachedTree_->reload(SchemaLoader::load(yaml, *attachedReg_));
        } catch (...) {
            // Schema saved but reload failed (e.g. unknown action name).
            // Leave the running tree unchanged.
        }
    }
    return true;
}

void EditorServer::putAction(std::string_view name, std::string_view intent,
                              const std::vector<std::string>& reads,
                              const std::vector<std::string>& writes) {
    store_->upsertAction({std::string(name), std::string(intent), reads, writes});
}

void EditorServer::putCondition(std::string_view name, std::string_view intent,
                                 const std::vector<std::string>& reads) {
    store_->upsertCondition({std::string(name), std::string(intent), reads});
}

void EditorServer::putStateKey(std::string_view key, std::string_view type) {
    store_->upsertStateKey(key, type);
}

void EditorServer::removeAction(std::string_view name)    { store_->removeAction(name);    }
void EditorServer::removeCondition(std::string_view name) { store_->removeCondition(name); }
void EditorServer::removeStateKey(std::string_view key)   { store_->removeStateKey(key);   }

void EditorServer::start(int port) {
    impl_->server.Get("/", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(std::string(kEditorHtml), "text/html");
    });

    impl_->server.Get("/api/actions", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getActionsJson(), "application/json");
    });

    impl_->server.Get("/api/conditions", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getConditionsJson(), "application/json");
    });

    impl_->server.Get("/api/blackboard", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getBlackboardJson(), "application/json");
    });

    impl_->server.Get("/api/schema", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getSchemaJson(), "application/json");
    });

    impl_->server.Post("/api/schema", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string yamlContent = extractJsonStringField(req.body, "yaml");
        if (yamlContent.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"missing yaml field in request body"})", "application/json");
            return;
        }
        // Validate the YAML before saving.
        try {
            std::ignore = SchemaParser::parse(yamlContent);
        } catch (const std::exception& exc) {
            res.status = 422;
            res.set_content(R"({"error":)" + jsonString(exc.what()) + '}', "application/json");
            return;
        }
        if (!saveSchema(yamlContent)) {
            res.status = 500;
            res.set_content(R"({"error":"failed to write schema file"})", "application/json");
            return;
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Get("/api/analyze", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getAnalyzeJson(), "application/json");
    });

    impl_->server.Get("/api/tree", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getTreeJson(), "application/json");
    });

    impl_->server.Get("/api/tickstate", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(getTickStateJson(), "application/json");
    });

    impl_->server.Put("/api/actions", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string name = extractJsonStringField(req.body, "name");
        if (name.empty()) { res.status = 400; res.set_content(R"({"error":"name required"})", "application/json"); return; }
        putAction(name, extractJsonStringField(req.body, "intent"),
                  extractJsonStringArray(req.body, "reads"),
                  extractJsonStringArray(req.body, "writes"));
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Delete("/api/actions/:name", [this](const httplib::Request& req, httplib::Response& res) {
        removeAction(req.path_params.at("name"));
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Put("/api/conditions", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string name = extractJsonStringField(req.body, "name");
        if (name.empty()) { res.status = 400; res.set_content(R"({"error":"name required"})", "application/json"); return; }
        putCondition(name, extractJsonStringField(req.body, "intent"),
                     extractJsonStringArray(req.body, "reads"));
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Delete("/api/conditions/:name", [this](const httplib::Request& req, httplib::Response& res) {
        removeCondition(req.path_params.at("name"));
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Put("/api/blackboard", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string key = extractJsonStringField(req.body, "key");
        if (key.empty()) { res.status = 400; res.set_content(R"({"error":"key required"})", "application/json"); return; }
        putStateKey(key, extractJsonStringField(req.body, "type"));
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Delete("/api/blackboard/:key", [this](const httplib::Request& req, httplib::Response& res) {
        removeStateKey(req.path_params.at("key"));
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->thread = std::thread([this, port] {
        impl_->server.listen("localhost", port);
    });

    while (!impl_->server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));  // NOLINT(readability-magic-numbers)
    }
    running_ = true;
}

void EditorServer::stop() noexcept {
    impl_->server.stop();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    running_ = false;
}

}  // namespace bt
