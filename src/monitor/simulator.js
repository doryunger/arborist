// ── Colors ────────────────────────────────────────────────────────────────────
const STATUS_COLOR = {
  SUCCESS: { background: '#1a3d2b', border: '#a6e3a1', font: '#a6e3a1' },
  FAILURE: { background: '#3d1a2b', border: '#f38ba8', font: '#f38ba8' },
  RUNNING: { background: '#3d2a1a', border: '#fab387', font: '#fab387' },
};

const NODE_TYPE = {
  Sequence:  { shape: 'box',       bg: '#1e2540', border: '#89b4fa' },
  Selector:  { shape: 'ellipse',   bg: '#2a1e40', border: '#cba6f7' },
  Parallel:  { shape: 'hexagon',   bg: '#2a2a1e', border: '#f9e2af' },
  Action:    { shape: 'roundRect', bg: '#1e3028', border: '#a6e3a1' },
  Condition: { shape: 'diamond',   bg: '#301e28', border: '#f38ba8' },
};
const FALLBACK_TYPE = { shape: 'box', bg: '#313244', border: '#7f849c' };

// ── State ─────────────────────────────────────────────────────────────────────
let network      = null;
let allHistory   = [];
let currentIndex = -1;
let isLive       = true;
let isPlaying    = false;
let playTimer    = null;
let nodeDefaults = {};   // name → { background, border }
let nodeTypeMap  = {};   // name → type string
let allEdgeIds   = [];   // all edge IDs for bulk reset

// ── Tooltip helper ────────────────────────────────────────────────────────────
function makeTooltip(name, type, status) {
  const sc = STATUS_COLOR[status];
  const statusColor = sc ? sc.font : '#6c7086';
  return '<div style="background:#181825;border:1px solid #313244;padding:8px 12px;' +
    'border-radius:6px;font-family:monospace;font-size:12px;line-height:1.7">' +
    '<b style="color:#cdd6f4">' + name + '</b><br>' +
    '<span style="color:#6c7086">type: </span><span style="color:#89b4fa">' + (type || '?') + '</span><br>' +
    '<span style="color:#6c7086">status: </span><span style="color:' + statusColor + '">' + (status || '—') + '</span>' +
    '</div>';
}

// ── Tree building ─────────────────────────────────────────────────────────────
function buildGraph(node, nodes, edges, parentId) {
  const t = NODE_TYPE[node.type] || FALLBACK_TYPE;
  nodeDefaults[node.name] = { background: t.bg, border: t.border };
  nodeTypeMap[node.name]  = node.type || 'Unknown';

  nodes.push({
    id:    node.name,
    label: node.name,
    shape: t.shape,
    color: { background: t.bg, border: t.border },
    font:  { color: '#cdd6f4', size: 15, face: 'monospace' },
    widthConstraint: { minimum: 100, maximum: 210 },
    margin: 12,
    title: makeTooltip(node.name, node.type, null),
  });

  if (parentId) {
    const edgeId = parentId + '→' + node.name;
    allEdgeIds.push(edgeId);
    edges.push({
      id:    edgeId,
      from:  parentId,
      to:    node.name,
      color: { color: '#45475a', opacity: 0.8 },
      width: 1,
    });
  }

  (node.children || []).forEach(c => buildGraph(c, nodes, edges, node.name));
}

async function loadTree() {
  try {
    const res = await fetch('/tree');
    const tree = await res.json();
    const nodes = [], edges = [];
    buildGraph(tree, nodes, edges, null);

    const container = document.getElementById('graph');
    const data = {
      nodes: new vis.DataSet(nodes),
      edges: new vis.DataSet(edges),
    };
    const options = {
      layout: {
        hierarchical: {
          direction:       'UD',
          sortMethod:      'directed',
          levelSeparation: 150,
          nodeSpacing:     200,
          treeSpacing:     200,
        },
      },
      physics: false,
      edges: {
        arrows: { to: { enabled: true, scaleFactor: 0.7 } },
        smooth: { type: 'cubicBezier', forceDirection: 'vertical', roundness: 0.3 },
      },
      interaction: {
        hover:        true,
        zoomView:     true,
        dragView:     true,
        tooltipDelay: 150,
      },
      nodes: {
        borderWidth:         2,
        borderWidthSelected: 3,
        shadow: { enabled: true, color: 'rgba(0,0,0,0.4)', size: 8, x: 2, y: 2 },
      },
    };

    if (network) network.destroy();
    network = new vis.Network(container, data, options);
    network._nodeData = data.nodes;
    network._edgeData = data.edges;
    network.fit({ animation: false });
  } catch (e) {
    console.error('tree load failed', e);
  }
}

// ── Render a single tick ──────────────────────────────────────────────────────
function renderTick(index) {
  if (index < 0 || index >= allHistory.length) return;
  currentIndex = index;
  const record = allHistory[index];

  // Build status map from active path
  const statusMap = {};
  (record.activePath || []).forEach(e => { statusMap[e.name] = e.status; });

  // Build active edge set from consecutive pairs in active path
  const activeEdgeSet = new Set();
  const path = record.activePath || [];
  for (let i = 0; i < path.length - 1; i++) {
    activeEdgeSet.add(path[i].name + '→' + path[i + 1].name);
  }

  // Update nodes
  if (network && network._nodeData) {
    const nodeUpdates = Object.keys(nodeDefaults).map(id => {
      const status = statusMap[id];
      const sc = status ? STATUS_COLOR[status] : null;
      return {
        id,
        color: sc ? { background: sc.background, border: sc.border } : nodeDefaults[id],
        font:  { color: sc ? sc.font : '#cdd6f4', size: 15, face: 'monospace' },
        title: makeTooltip(id, nodeTypeMap[id], status),
      };
    });
    network._nodeData.update(nodeUpdates);
  }

  // Update edges — highlight active path, dim the rest
  if (network && network._edgeData) {
    const edgeUpdates = allEdgeIds.map(id => ({
      id,
      color: activeEdgeSet.has(id)
        ? { color: '#89b4fa', opacity: 1 }
        : { color: '#313244', opacity: 0.35 },
      width: activeEdgeSet.has(id) ? 3 : 1,
    }));
    network._edgeData.update(edgeUpdates);
  }

  // Update header badges
  const tickBadge     = document.getElementById('tick-badge');
  const behaviorBadge = document.getElementById('behavior-badge');
  const statusBadge   = document.getElementById('status-badge');
  tickBadge.textContent = 'tick #' + record.tick;
  if (record.behavior) {
    behaviorBadge.textContent = record.behavior;
    behaviorBadge.style.display = 'inline-block';
  }
  if (record.status) {
    const sc = STATUS_COLOR[record.status];
    statusBadge.textContent      = record.status;
    statusBadge.style.display    = 'inline-block';
    statusBadge.style.background = sc ? sc.background  : '#313244';
    statusBadge.style.color      = sc ? sc.font        : '#cdd6f4';
    statusBadge.style.borderColor = sc ? sc.border     : 'transparent';
  }

  syncControls();
}

// ── Controls ──────────────────────────────────────────────────────────────────
function syncControls() {
  const slider  = document.getElementById('scrubber');
  const counter = document.getElementById('tick-counter');
  const liveDot = document.getElementById('live-dot');

  slider.max   = Math.max(0, allHistory.length - 1);
  slider.value = currentIndex;
  counter.textContent = (currentIndex + 1) + ' / ' + allHistory.length;

  const atEnd = currentIndex === allHistory.length - 1;
  liveDot.textContent = atEnd ? '● LIVE' : '○ REPLAY';
  liveDot.className   = atEnd ? '' : 'paused';
}

function seek(index) {
  const clamped = Math.max(0, Math.min(index, allHistory.length - 1));
  isLive = clamped === allHistory.length - 1;
  renderTick(clamped);
}

function stepPrev() { isLive = false; seek(currentIndex - 1); }

function stepNext() {
  const next = currentIndex + 1;
  if (next >= allHistory.length) { isLive = true; return; }
  isLive = next === allHistory.length - 1;
  seek(next);
}

function togglePlay() {
  isPlaying = !isPlaying;
  document.getElementById('btn-play').textContent = isPlaying ? '⏸ Pause' : '⏵ Play';
  if (isPlaying) {
    isLive = false;
    playTimer = setInterval(() => {
      if (currentIndex >= allHistory.length - 1) {
        togglePlay();
        isLive = true;
        return;
      }
      seek(currentIndex + 1);
    }, 250);
  } else {
    clearInterval(playTimer);
    playTimer = null;
  }
}

// ── History polling ───────────────────────────────────────────────────────────
async function pollHistory() {
  try {
    const res = await fetch('/history');
    const history = await res.json();
    if (!history.length) return;

    const firstLoad = allHistory.length === 0;
    allHistory = history;

    if (firstLoad) {
      renderTick(allHistory.length - 1);
      return;
    }

    // Always update the slider range
    const slider = document.getElementById('scrubber');
    slider.max = allHistory.length - 1;
    document.getElementById('tick-counter').textContent =
      (currentIndex + 1) + ' / ' + allHistory.length;

    if (isLive && !isPlaying) {
      renderTick(allHistory.length - 1);
    }
  } catch (e) {
    console.error('history poll failed', e);
  }
}

// ── Wire up controls ──────────────────────────────────────────────────────────
document.getElementById('btn-prev').addEventListener('click', stepPrev);
document.getElementById('btn-next').addEventListener('click', stepNext);
document.getElementById('btn-play').addEventListener('click', togglePlay);
document.getElementById('scrubber').addEventListener('input', e => {
  if (isPlaying) togglePlay();
  seek(parseInt(e.target.value, 10));
});

// ── Boot ──────────────────────────────────────────────────────────────────────
loadTree();
setInterval(pollHistory, 500);
