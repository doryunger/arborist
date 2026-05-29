const STATUS_COLOR = { SUCCESS: '#a6e3a1', FAILURE: '#f38ba8', RUNNING: '#fab387' };
const DEFAULT_COLOR = '#585b70';
let network = null;
let nodeIds = {};

function buildGraph(node, nodes, edges, parentId) {
  const id = node.name;
  nodeIds[id] = true;
  nodes.push({ id, label: node.name + '\n[' + node.type + ']',
    color: { background: DEFAULT_COLOR, border: '#7f849c' },
    font: { color: '#cdd6f4', size: 11 },
    shape: 'box' });
  if (parentId) edges.push({ from: parentId, to: id, color: { color: '#585b70' } });
  (node.children || []).forEach(c => buildGraph(c, nodes, edges, id));
}

async function loadTree() {
  try {
    const res = await fetch('/tree');
    const tree = await res.json();
    const nodes = [], edges = [];
    buildGraph(tree, nodes, edges, null);
    const container = document.getElementById('graph');
    const data = { nodes: new vis.DataSet(nodes), edges: new vis.DataSet(edges) };
    const options = {
      layout: { hierarchical: { direction: 'UD', sortMethod: 'directed', levelSeparation: 80 } },
      physics: false,
      edges: { arrows: 'to' }
    };
    if (network) network.destroy();
    network = new vis.Network(container, data, options);
    network._nodeData = data.nodes;
  } catch (e) { console.error('tree load failed', e); }
}

async function refreshHistory() {
  try {
    const res = await fetch('/history');
    const history = await res.json();
    if (!history.length) return;
    const latest = history[history.length - 1];
    document.getElementById('tick-info').textContent =
      'tick #' + latest.tick + ' — ' + (latest.behavior || '(none)') + ' → ' + latest.status;
    if (network && network._nodeData) {
      const updates = Object.keys(nodeIds).map(id => ({
        id, color: { background: DEFAULT_COLOR, border: '#7f849c' }
      }));
      (latest.activePath || []).forEach(entry => {
        const upd = updates.find(u => u.id === entry.name);
        if (upd) upd.color = {
          background: STATUS_COLOR[entry.status] || DEFAULT_COLOR,
          border: '#cdd6f4'
        };
      });
      network._nodeData.update(updates);
    }
    const list = document.getElementById('history-list');
    list.innerHTML = history.slice(-20).reverse().map(r =>
      `<div class="record ${r.status}"><span class="label">#${r.tick}</span> ${r.behavior || '—'} <span style="color:${STATUS_COLOR[r.status]||'#cdd6f4'}">${r.status}</span></div>`
    ).join('');
  } catch (e) { console.error('history refresh failed', e); }
}

loadTree();
setInterval(refreshHistory, 500);
