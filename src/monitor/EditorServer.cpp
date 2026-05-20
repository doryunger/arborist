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
  #main { display: flex; flex: 1; overflow: hidden; }
  .panel { padding: 14px; overflow-y: auto; }
  #contracts { width: 340px; border-right: 1px solid #313244; display: flex; flex-direction: column; gap: 14px; }
  #schema-panel { flex: 1; display: flex; flex-direction: column; gap: 10px; overflow: hidden; padding: 14px; }
  h2 { margin: 0 0 8px; font-size: 12px; text-transform: uppercase; letter-spacing: 0.08em; color: #89b4fa; }
  .item { margin-bottom: 6px; padding: 7px 10px; border-radius: 5px; background: #181825; border: 1px solid #313244; font-size: 11px; }
  .item .name { color: #cba6f7; font-weight: bold; }
  .item .intent { color: #a6adc8; margin-top: 2px; }
  .item .deps { margin-top: 3px; }
  .tag { display: inline-block; padding: 1px 5px; border-radius: 3px; font-size: 10px; margin: 1px; }
  .tag.read  { background: #1e3a5f; color: #89b4fa; }
  .tag.write { background: #3a1e1e; color: #f38ba8; }
  .tag.key   { background: #1e3a2e; color: #a6e3a1; }
  #schema-view { display: flex; flex-direction: column; flex: 1; overflow: hidden; gap: 10px; }
  #schema-yaml { flex: 1; background: #181825; border: 1px solid #313244; border-radius: 5px; padding: 10px; font-size: 11px; white-space: pre; overflow: auto; color: #a6e3a1; min-height: 0; }
  #analyze-section { border-top: 1px solid #313244; padding-top: 10px; }
  .issue { padding: 5px 8px; border-radius: 4px; margin-bottom: 5px; font-size: 11px; }
  .issue.ERROR   { background: #3a1e1e; border-left: 3px solid #f38ba8; color: #f38ba8; }
  .issue.WARNING { background: #3a2e1e; border-left: 3px solid #fab387; color: #fab387; }
  .issue.clean   { background: #1e3a2e; border-left: 3px solid #a6e3a1; color: #a6e3a1; }
  .metric { display: inline-block; margin-right: 14px; font-size: 11px; color: #a6adc8; }
  .metric span { color: #cdd6f4; font-weight: bold; }
  button { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px 14px; font-size: 11px; font-family: monospace; cursor: pointer; }
  button:hover { background: #45475a; }
  .view-btn { padding: 4px 10px; font-size: 10px; }
  .view-btn.active { background: #1e3a5f; color: #89b4fa; border-color: #89b4fa; }
  .toolbar { display: flex; gap: 8px; align-items: center; }
  .path-label { font-size: 10px; color: #585b70; flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  #graph-view { display: flex; flex-direction: column; flex: 1; overflow: hidden; gap: 8px; }
  #behavior-tabs { display: flex; gap: 6px; flex-wrap: wrap; }
  .beh-pill { padding: 3px 10px; border-radius: 12px; font-size: 11px; cursor: pointer; background: #313244; border: 1px solid #45475a; color: #cdd6f4; }
  .beh-pill.active { background: #1e3a5f; border-color: #89b4fa; color: #89b4fa; }
  #svg-container { flex: 1; overflow: auto; background: #181825; border: 1px solid #313244; border-radius: 5px; padding: 10px; min-height: 0; }
  #status-bar { padding: 3px 18px; background: #11111b; font-size: 10px; color: #585b70; border-top: 1px solid #313244; }
</style>
</head>
<body>
<div id="header">
  <h1>Arborist — Editor</h1>
  <span class="badge disconnected" id="conn-badge">connecting...</span>
  <span id="schema-path" style="font-size:11px;color:#585b70;"></span>
</div>
<div id="main">
  <div class="panel" id="contracts">
    <div>
      <h2>Actions</h2>
      <div id="actions-list"><em style="color:#585b70;font-size:11px">loading...</em></div>
    </div>
    <div>
      <h2>Conditions</h2>
      <div id="conditions-list"><em style="color:#585b70;font-size:11px">loading...</em></div>
    </div>
    <div>
      <h2>Blackboard Keys</h2>
      <div id="blackboard-list"><em style="color:#585b70;font-size:11px">loading...</em></div>
    </div>
  </div>
  <div id="schema-panel">
    <div class="toolbar">
      <h2 style="margin:0">Schema</h2>
      <button onclick="loadSchema()">&#x21BA; Reload</button>
      <button onclick="runAnalysis()">&#x26A1; Analyze</button>
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
      <div id="behavior-tabs"></div>
      <div id="svg-container"><em style="color:#585b70;font-size:11px">Click Graph to render</em></div>
    </div>
  </div>
</div>
<div id="status-bar">Ready</div>
<script>
const setStatus = s => { document.getElementById('status-bar').textContent = s; };

let treeData = null;
let selectedBehavior = 0;

function showView(view) {
  const isSchema = view === 'schema';
  document.getElementById('schema-view').style.display = isSchema ? 'flex' : 'none';
  document.getElementById('graph-view').style.display  = isSchema ? 'none' : 'flex';
  document.getElementById('btn-schema').classList.toggle('active', isSchema);
  document.getElementById('btn-graph').classList.toggle('active', !isSchema);
  if (!isSchema && !treeData) { loadTree(); }
}

async function loadContracts() {
  try {
    const [actRes, condRes, bbRes] = await Promise.all([
      fetch('/api/actions'), fetch('/api/conditions'), fetch('/api/blackboard')
    ]);
    const actions    = await actRes.json();
    const conditions = await condRes.json();
    const blackboard = await bbRes.json();

    document.getElementById('conn-badge').textContent = 'connected';
    document.getElementById('conn-badge').classList.remove('disconnected');

    document.getElementById('actions-list').innerHTML = actions.map(a => `
      <div class="item">
        <div class="name">${a.name}</div>
        ${a.intent ? `<div class="intent">${a.intent}</div>` : ''}
        <div class="deps">
          ${(a.reads||[]).map(r=>`<span class="tag read">r: ${r}</span>`).join('')}
          ${(a.writes||[]).map(w=>`<span class="tag write">w: ${w}</span>`).join('')}
        </div>
      </div>`).join('') || '<em style="color:#585b70;font-size:11px">none registered</em>';

    document.getElementById('conditions-list').innerHTML = conditions.map(c => `
      <div class="item">
        <div class="name">${c.name}</div>
        ${c.intent ? `<div class="intent">${c.intent}</div>` : ''}
        <div class="deps">
          ${(c.reads||[]).map(r=>`<span class="tag read">r: ${r}</span>`).join('')}
        </div>
      </div>`).join('') || '<em style="color:#585b70;font-size:11px">none registered</em>';

    document.getElementById('blackboard-list').innerHTML = blackboard.map(k => `
      <div class="item">
        <span class="tag key">${k.key}</span>
        <span style="font-size:10px;color:#a6adc8;margin-left:6px">${k.type}</span>
      </div>`).join('') || '<em style="color:#585b70;font-size:11px">none declared</em>';

    setStatus('Contracts loaded — ' + actions.length + ' actions, ' + conditions.length + ' conditions');
  } catch (e) {
    document.getElementById('conn-badge').textContent = 'disconnected';
    document.getElementById('conn-badge').classList.add('disconnected');
    setStatus('Connection failed: ' + e.message);
  }
}

async function loadSchema() {
  try {
    const res = await fetch('/api/schema');
    const data = await res.json();
    document.getElementById('schema-yaml').textContent = data.yaml || '(no schema file configured)';
    document.getElementById('file-path-label').textContent = data.path || '';
    document.getElementById('schema-path').textContent = data.path ? '— ' + data.path : '';
    treeData = null;
    setStatus('Schema loaded from ' + (data.path || 'memory'));
  } catch (e) {
    document.getElementById('schema-yaml').textContent = 'Error: ' + e.message;
    setStatus('Schema load failed: ' + e.message);
  }
}

async function runAnalysis() {
  setStatus('Running analysis...');
  try {
    const res = await fetch('/api/analyze');
    const data = await res.json();
    const section = document.getElementById('analyze-section');
    section.style.display = 'block';
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
      `<span class="metric">max width <span>${m.maxWidth||0}</span></span>` +
      `<span class="metric">avg branching <span>${(m.avgBranchingFactor||0).toFixed(1)}</span></span>`;
    const issues = data.issues || [];
    if (issues.length === 0) {
      document.getElementById('issues-list').innerHTML = '<div class="issue clean">No issues detected</div>';
    } else {
      document.getElementById('issues-list').innerHTML = issues.map(i =>
        `<div class="issue ${i.severity}"><strong>${i.severity}</strong> [${i.path}] ${i.message}</div>`
      ).join('');
    }
    setStatus('Analysis complete — ' + issues.length + ' issue(s)');
  } catch (e) {
    setStatus('Analysis failed: ' + e.message);
  }
}

async function loadTree() {
  try {
    const res = await fetch('/api/tree');
    const data = await res.json();
    if (data.error) {
      document.getElementById('svg-container').innerHTML =
        `<p style="color:#f38ba8;padding:14px;font-size:11px">Error: ${data.error}</p>`;
      setStatus('Tree error: ' + data.error);
      return;
    }
    treeData = data;
    selectedBehavior = 0;
    renderBehaviorTabs();
    renderBehaviorTree(0);
    setStatus('Tree loaded — ' + (treeData.behaviors||[]).length + ' behavior(s)');
  } catch (e) {
    document.getElementById('svg-container').innerHTML =
      `<p style="color:#f38ba8;padding:14px;font-size:11px">Failed: ${e.message}</p>`;
    setStatus('Tree load failed: ' + e.message);
  }
}

function renderBehaviorTabs() {
  if (!treeData) { return; }
  document.getElementById('behavior-tabs').innerHTML =
    (treeData.behaviors||[]).map((beh, idx) =>
      `<button class="beh-pill${idx === selectedBehavior ? ' active' : ''}" onclick="selectBehavior(${idx})">${beh.name}${beh.condition ? ' ['+beh.condition+']' : ''}</button>`
    ).join('');
}

function selectBehavior(idx) {
  selectedBehavior = idx;
  renderBehaviorTabs();
  renderBehaviorTree(idx);
}

const NODE_W = 120, NODE_H = 40, V_GAP = 56, H_GAP = 16;

function computeWidth(node) {
  if (!node.children || node.children.length === 0) { node._w = NODE_W; return; }
  node.children.forEach(computeWidth);
  const childW = node.children.reduce((s, c) => s + c._w, 0) + H_GAP * (node.children.length - 1);
  node._w = Math.max(NODE_W, childW);
}

function assignPos(node, x, y) {
  node._x = x; node._y = y;
  if (!node.children || node.children.length === 0) { return; }
  const childW = node.children.reduce((s, c) => s + c._w, 0) + H_GAP * (node.children.length - 1);
  let cx = x - childW / 2;
  for (const child of node.children) {
    assignPos(child, cx + child._w / 2, y + NODE_H + V_GAP);
    cx += child._w + H_GAP;
  }
}

const NODE_COLORS = {
  sequence:'#89b4fa', selector:'#cba6f7', parallel:'#94e2d5',
  action:'#a6e3a1', condition:'#fab387'
};

function renderBehaviorTree(idx) {
  if (!treeData || !treeData.behaviors || !treeData.behaviors[idx]) { return; }
  const beh = treeData.behaviors[idx];
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
  for (const node of all) {
    const col = NODE_COLORS[node.type] || '#cdd6f4';
    const nx  = node._x - NODE_W / 2;
    const nm  = (node.name||'').length > 14 ? (node.name||'').slice(0,13)+'…' : (node.name||'');
    nodes += `<rect x="${nx}" y="${node._y}" width="${NODE_W}" height="${NODE_H}" rx="6" fill="#181825" stroke="${col}" stroke-width="1.5"/>`;
    nodes += `<text x="${node._x}" y="${node._y+14}" text-anchor="middle" fill="${col}" font-size="9" font-family="monospace">${(node.type||'').toUpperCase()}</text>`;
    nodes += `<text x="${node._x}" y="${node._y+29}" text-anchor="middle" fill="#cdd6f4" font-size="11" font-family="monospace">${nm}</text>`;
  }
  document.getElementById('svg-container').innerHTML =
    `<svg width="${svgW}" height="${svgH}">${edges}${nodes}</svg>`;
}

loadContracts();
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

std::string schemaNodeToJson(const SchemaNode& node, std::size_t& nextId) {
    const std::size_t myId = nextId++;
    std::string out = R"({"id":)";
    out += std::to_string(myId);
    out += R"(,"type":)";    out += jsonString(schemaNodeTypeName(node.type));
    out += R"(,"name":)";    out += jsonString(node.name);
    out += R"(,"children":[)";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i > 0) { out += ','; }
        out += schemaNodeToJson(*node.children[i], nextId);
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
            out += schemaNodeToJson(*beh.tree, nextId);
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

EditorServer::EditorServer(const RegistryStore& store, std::string_view schemaPath)
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

bool EditorServer::saveSchema(std::string_view yaml) const {
    return writeFile(schemaPath_, yaml);
}

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
