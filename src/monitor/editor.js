const setStatus = s => { document.getElementById('status-bar').textContent = s; };

let treeData   = null;
let issueMap   = {};
let registeredActions    = new Set();
let registeredConditions = new Set();
let selectedBehavior = 0;
let selectedNodeId   = null;
let latestTickState  = null;
const TICK_STATUS_COLORS = { SUCCESS: '#a6e3a1', FAILURE: '#f38ba8', RUNNING: '#fab387' };
let isDirty       = false;
let nextNodeId    = 0;
let dragNodeId    = null;
let dragOverId    = null;
let nodePositions = {};

function genId() { return nextNodeId++; }

function isLeaf(type) { return type === 'action' || type === 'condition'; }

function nodeRegistered(node) {
  if (node.type === 'action')    { return registeredActions.has(node.name); }
  if (node.type === 'condition') { return registeredConditions.has(node.name); }
  return true;
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

const NODE_W = 130, NODE_H = 36, ROW_GAP = 14, DEPTH_IND = 34, ML = 28, MT = 20;

function assignPosV(node, depth, sibIdx, sibCount, yRef) {
  node._depth    = depth;
  node._sibIdx   = sibIdx;
  node._sibCount = sibCount;
  node._x = ML + depth * DEPTH_IND + NODE_W / 2;
  node._y = yRef.y;
  yRef.y += NODE_H + ROW_GAP;
  const kids = node.children || [];
  for (let i = 0; i < kids.length; i++) {
    assignPosV(kids[i], depth + 1, i, kids.length, yRef);
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
  const yRef = { y: MT };
  assignPosV(root, 0, -1, 0, yRef);
  const all = [];
  function collect(n) { all.push(n); (n.children||[]).forEach(collect); }
  collect(root);
  nodePositions = {};
  for (const node of all) { nodePositions[node.id] = { x: node._x, y: node._y }; }
  const maxDepth = Math.max(...all.map(n => n._depth));
  const svgW = ML * 2 + (maxDepth + 1) * DEPTH_IND + NODE_W;
  const svgH = Math.max(...all.map(n => n._y)) + NODE_H + MT;
  let edges = '', nodes = '';
  for (const node of all) {
    for (const child of (node.children||[])) {
      const px = node._x, py = node._y + NODE_H;
      const cy = child._y + NODE_H / 2, cx = child._x - NODE_W / 2;
      edges += `<path d="M ${px} ${py} V ${cy} H ${cx}" stroke="#313244" stroke-width="1.5" fill="none"/>`;
    }
  }
  const ISSUE_COLORS = { ERROR: '#f38ba8', WARNING: '#fab387' };
  const activePathMap = {};
  if (latestTickState && latestTickState.activePath) {
    for (const ap of latestTickState.activePath) { activePathMap[ap.name] = ap.status; }
  }
  for (const node of all) {
    const col      = node._edited ? '#f9e2af' : (NODE_COLORS[node.type] || '#cdd6f4');
    const nx       = node._x - NODE_W / 2;
    const nm       = (node.name||'').length > 15 ? (node.name||'').slice(0,14)+'…' : (node.name||'');
    const sel      = node.id === selectedNodeId;
    const issueSev = issueMap[node.path];
    const issueCol = issueSev ? ISSUE_COLORS[issueSev] : null;
    const strW     = sel ? '2.5' : '1.5';
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
    nodes += `<rect x="${nx}" y="${node._y}" width="${NODE_W}" height="${NODE_H}" rx="6" fill="#181825" stroke="${col}" stroke-width="${strW}" style="cursor:grab" onclick="selectNode(${node.id})" onmousedown="onDragStart(${node.id})" onmouseenter="onDragEnter(${node.id})" onmouseleave="onDragLeave(${node.id})"/>`;
    nodes += `<text x="${node._x}" y="${node._y+13}" text-anchor="middle" fill="${col}" font-size="9" font-family="monospace" pointer-events="none">${(node.type||'').toUpperCase()}</text>`;
    nodes += `<text x="${node._x}" y="${node._y+27}" text-anchor="middle" fill="#cdd6f4" font-size="11" font-family="monospace" pointer-events="none">${nm}</text>`;
    if (isLeaf(node.type)) {
      const regCol = nodeRegistered(node) ? '#a6e3a1' : '#f38ba8';
      nodes += `<circle cx="${nx+6}" cy="${node._y+6}" r="4" fill="${regCol}" opacity="0.9"/>`;
    }
    if (node._sibCount > 1) {
      nodes += `<text x="${nx-8}" y="${node._y+NODE_H/2+4}" text-anchor="middle" fill="#585b70" font-size="10" font-family="monospace" pointer-events="none">${node._sibIdx+1}</text>`;
    }
    if (issueCol) {
      nodes += `<circle cx="${nx+NODE_W-4}" cy="${node._y+4}" r="5" fill="${issueCol}"/>`;
      nodes += `<text x="${nx+NODE_W-4}" y="${node._y+8}" text-anchor="middle" fill="#1e1e2e" font-size="8" font-family="monospace" font-weight="bold" pointer-events="none">${issueSev === 'ERROR' ? '!' : '?'}</text>`;
    }
  }
  document.getElementById('svg-container').innerHTML =
    `<svg width="${svgW}" height="${svgH}">${edges}${nodes}</svg>`;
}

function onDragStart(nodeId) {
  dragNodeId = nodeId;
  dragOverId = null;
  document.getElementById('svg-container').style.cursor = 'grabbing';
}
function onDragEnter(nodeId) {
  if (dragNodeId === null || nodeId === dragNodeId) { return; }
  const root = currentRoot();
  if (!root) { return; }
  const src = findParentOf(root, dragNodeId);
  const dst = findParentOf(root, nodeId);
  if (!src || !dst || src.parent !== dst.parent) { return; }
  dragOverId = nodeId;
  const old = document.getElementById('drag-highlight');
  if (old) { old.remove(); }
  const pos = nodePositions[nodeId];
  const svg = document.querySelector('#svg-container svg');
  if (!pos || !svg) { return; }
  const hl = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
  hl.setAttribute('id', 'drag-highlight');
  hl.setAttribute('x',  pos.x - NODE_W/2 - 3);
  hl.setAttribute('y',  pos.y - 3);
  hl.setAttribute('width',  NODE_W + 6);
  hl.setAttribute('height', NODE_H + 6);
  hl.setAttribute('rx', '8');
  hl.setAttribute('fill', 'none');
  hl.setAttribute('stroke', '#89b4fa');
  hl.setAttribute('stroke-width', '2');
  hl.setAttribute('stroke-dasharray', '4,3');
  hl.setAttribute('opacity', '0.85');
  hl.setAttribute('pointer-events', 'none');
  svg.appendChild(hl);
}
function onDragLeave(nodeId) {
  if (dragOverId === nodeId) {
    dragOverId = null;
    const old = document.getElementById('drag-highlight');
    if (old) { old.remove(); }
  }
}
function onDragEnd() {
  document.getElementById('svg-container').style.cursor = '';
  const old = document.getElementById('drag-highlight');
  if (old) { old.remove(); }
  if (dragNodeId !== null && dragOverId !== null) {
    const root = currentRoot();
    if (root) {
      const src = findParentOf(root, dragNodeId);
      const dst = findParentOf(root, dragOverId);
      if (src && dst && src.parent === dst.parent) {
        const tmp = src.parent.children[src.index];
        src.parent.children[src.index] = dst.parent.children[dst.index];
        dst.parent.children[dst.index] = tmp;
        markDirty();
        renderBehaviorTree(selectedBehavior);
      }
    }
  }
  dragNodeId = null;
  dragOverId = null;
}
document.addEventListener('mouseup', onDragEnd);

async function pollTickState() {
  try {
    const res = await fetch('/api/tickstate');
    const ts  = await res.json();
    if (ts.tick > 0) {
      const changed = !latestTickState || latestTickState.tick !== ts.tick;
      latestTickState = ts;
      const graphVisible = document.getElementById('graph-view').style.display !== 'none';
      if (changed && treeData && graphVisible && !isDirty && selectedNodeId === null) {
        renderBehaviorTree(selectedBehavior);
      }
    }
  } catch (_) {}
}

setInterval(pollTickState, 500);
loadSchema();
