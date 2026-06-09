const STATUS_COLOR = { SUCCESS: '#a6e3a1', FAILURE: '#f38ba8', RUNNING: '#fab387' };
const DEFAULT_COLOR = '#585b70';
let network = null;
let nodeIds = {};   // name -> [numericId, ...]
let nodeCounter = 0;

function buildGraph(node, nodes, edges, parentId) {
  const id = nodeCounter++;
  if (!nodeIds[node.name]) nodeIds[node.name] = [];
  nodeIds[node.name].push(id);
  nodes.push({ id, label: node.name + '\n[' + node.type + ']',
    color: { background: DEFAULT_COLOR, border: '#7f849c' },
    font: { color: '#cdd6f4', size: 11 },
    shape: 'box' });
  if (parentId !== null) edges.push({ from: parentId, to: id, color: { color: '#585b70' } });
  (node.children || []).forEach(c => buildGraph(c, nodes, edges, id));
}

async function loadTree() {
  try {
    nodeIds = {};
    nodeCounter = 0;
    const res = await fetch('/tree');
    const tree = await res.json();
    const nodes = [], edges = [];
    buildGraph(tree, nodes, edges, null);
    const container = document.getElementById('graph');
    const data = { nodes: new vis.DataSet(nodes), edges: new vis.DataSet(edges) };
    const options = {
      layout: { hierarchical: { direction: 'UD', sortMethod: 'directed', levelSeparation: 100, nodeSpacing: 140, treeSpacing: 180 } },
      physics: false,
      edges: { arrows: 'to' },
      interaction: { zoomView: true, dragView: true }
    };
    if (network) network.destroy();
    network = new vis.Network(container, data, options);
    network.once('afterDrawing', () => network.fit({ animation: false }));
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
      const updates = Object.values(nodeIds).flat().map(id => ({
        id, color: { background: DEFAULT_COLOR, border: '#7f849c' }
      }));
      (latest.activePath || []).forEach(entry => {
        (nodeIds[entry.name] || []).forEach(id => {
          const upd = updates.find(u => u.id === id);
          if (upd) upd.color = {
            background: STATUS_COLOR[entry.status] || DEFAULT_COLOR,
            border: '#cdd6f4'
          };
        });
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
