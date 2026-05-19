#include "bt/MonitorServer.h"

#include <any>
#include <string>
#include <thread>
#include <typeinfo>

#include <httplib.h>

#include "bt/DecisionEmitter.h"
#include "bt/Status.h"
#include "bt/TreeSerializer.h"

namespace bt {

// ── Viewer HTML ───────────────────────────────────────────────────────────────

static constexpr std::string_view kViewerHtml = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Arborist — Live Tree Viewer</title>
<script src="https://unpkg.com/vis-network/standalone/umd/vis-network.min.js"></script>
<style>
  body { margin: 0; background: #1e1e2e; color: #cdd6f4; font-family: monospace; display: flex; flex-direction: column; height: 100vh; }
  #header { padding: 8px 16px; background: #181825; border-bottom: 1px solid #313244; display: flex; align-items: center; gap: 16px; }
  #header h1 { margin: 0; font-size: 16px; color: #89b4fa; }
  #tick-info { font-size: 13px; color: #a6e3a1; }
  #container { display: flex; flex: 1; overflow: hidden; }
  #graph { flex: 1; }
  #sidebar { width: 280px; background: #181825; border-left: 1px solid #313244; overflow-y: auto; padding: 12px; font-size: 12px; }
  #sidebar h2 { margin: 0 0 8px; font-size: 13px; color: #89b4fa; }
  .record { margin-bottom: 6px; padding: 6px 8px; border-radius: 4px; background: #1e1e2e; border: 1px solid #313244; }
  .record.SUCCESS { border-color: #a6e3a1; }
  .record.FAILURE { border-color: #f38ba8; }
  .record.RUNNING { border-color: #fab387; }
  .label { font-weight: bold; }
</style>
</head>
<body>
<div id="header">
  <h1>Arborist — Live Tree Viewer</h1>
  <span id="tick-info">waiting for ticks...</span>
</div>
<div id="container">
  <div id="graph"></div>
  <div id="sidebar">
    <h2>Tick History</h2>
    <div id="history-list"></div>
  </div>
</div>
<script>
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
</script>
</body>
</html>)html";

// ── JSON helpers ──────────────────────────────────────────────────────────────

namespace {

std::string_view statusName(Status status) {
    switch (status) {
        case Status::SUCCESS: return "SUCCESS";
        case Status::FAILURE: return "FAILURE";
        case Status::RUNNING: return "RUNNING";
    }
    return "UNKNOWN";
}

std::string anyToJson(const std::any& value) {
    if (value.type() == typeid(int)) {
        return std::to_string(std::any_cast<int>(value));
    }
    if (value.type() == typeid(bool)) {
        return std::any_cast<bool>(value) ? "true" : "false";
    }
    if (value.type() == typeid(float)) {
        return std::to_string(std::any_cast<float>(value));
    }
    if (value.type() == typeid(double)) {
        return std::to_string(std::any_cast<double>(value));
    }
    if (value.type() == typeid(std::string)) {
        std::string out = "\"";
        out += std::any_cast<const std::string&>(value);
        out += "\"";
        return out;
    }
    std::string out = "\"<";
    out += value.type().name();
    out += ">\"";
    return out;
}

std::string serializeHistory(const DecisionEmitter& emitter) {
    std::string out = "[";
    bool isFirst = true;
    for (const auto& record : emitter.history()) {
        if (!isFirst) { out += ','; }
        out += R"({"tick":)";
        out += std::to_string(record.tickNumber);
        out += R"(,"behavior":")";
        out += record.behaviorName;
        out += R"(","status":")";
        out += statusName(record.result);
        out += R"(","activePath":[)";
        for (std::size_t idx = 0; idx < record.activePath.size(); ++idx) {
            if (idx > 0) { out += ','; }
            out += R"({"name":")";
            out += record.activePath[idx].name;
            out += R"(","status":")";
            out += statusName(record.activePath[idx].status);
            out += R"("})";
        }
        out += R"(],"blackboard":{)";
        bool isFirstKey = true;
        for (const auto& [key, val] : record.blackboardSnapshot) {
            if (!isFirstKey) { out += ','; }
            out += '"';
            out += key;
            out += "\":";
            out += anyToJson(val);
            isFirstKey = false;
        }
        out += "}}";
        isFirst = false;
    }
    out += "]";
    return out;
}

}  // namespace

// ── MonitorServer::Impl ───────────────────────────────────────────────────────

struct MonitorServer::Impl {
    httplib::Server server;
    std::thread thread;
};

// ── MonitorServer ─────────────────────────────────────────────────────────────

MonitorServer::MonitorServer(const BehaviorTree& tree, const DecisionEmitter& emitter)
    : tree_(&tree), emitter_(&emitter), impl_(std::make_unique<Impl>()) {}

MonitorServer::~MonitorServer() {
    if (running_) {
        stop();
    }
}

void MonitorServer::start(int port) {
    impl_->server.Get("/", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(std::string(kViewerHtml), "text/html");
    });

    impl_->server.Get("/tree", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(TreeSerializer::toJson(tree_->root()), "application/json");
    });

    impl_->server.Get("/history", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(serializeHistory(*emitter_), "application/json");
    });

    impl_->thread = std::thread([this, port] {
        impl_->server.listen("localhost", port);
    });

    while (!impl_->server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));  // NOLINT(readability-magic-numbers)
    }
    running_ = true;
}

void MonitorServer::stop() noexcept {
    impl_->server.stop();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    running_ = false;
}

std::string MonitorServer::getTree() const {
    return TreeSerializer::toJson(tree_->root());
}

std::string MonitorServer::getHistory() const {
    return serializeHistory(*emitter_);
}

}  // namespace bt
