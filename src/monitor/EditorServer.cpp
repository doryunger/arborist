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
        out += R"({"name":)";   out += jsonString(act.name);
        out += R"(,"intent":)"; out += jsonString(act.intent);
        out += R"(,"reads":)";  out += jsonStringArray(act.reads);
        out += R"(,"writes":)"; out += jsonStringArray(act.writes);
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
    out += R"(,"maxDepth":)";   out += std::to_string(report.maxDepth);
    out += R"(,"maxWidth":)";   out += std::to_string(report.maxWidth);
    out += R"(,"avgBranchingFactor":)"; out += std::to_string(report.avgBranchingFactor);
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

struct EditorServer::Impl {
    httplib::Server server;
    std::thread     thread;
};

EditorServer::EditorServer(RegistryStore& store, std::string_view schemaPath,
                           std::string_view uiDir)
    : store_(&store), schemaPath_(schemaPath), uiDir_(uiDir), impl_(std::make_unique<Impl>()) {}

EditorServer::~EditorServer() {
    if (running_) { stop(); }
}

std::string EditorServer::getActionsJson()    const { return actionsToJson(store_->allActions()); }
std::string EditorServer::getConditionsJson() const { return conditionsToJson(store_->allConditions()); }
std::string EditorServer::getBlackboardJson() const { return blackboardToJson(store_->allStateKeys()); }

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
    return analyzeSchema(readFile(schemaPath_), store_->allActions(), store_->allConditions());
}

std::string EditorServer::getTreeJson() const {
    const std::string yaml = readFile(schemaPath_);
    if (yaml.empty()) { return R"({"error":"no schema file configured"})"; }
    try {
        return schemaDocToTreeJson(SchemaParser::parse(yaml));
    } catch (const std::exception& exc) {
        return R"({"error":)" + jsonString(exc.what()) + '}';
    }
}

void EditorServer::attachTree(BehaviorTree* tree, const LoaderRegistry& reg) noexcept {
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
    out += R"(,"behavior":")"; out += rec.behaviorName;
    out += R"(","status":")";
    switch (rec.result) {
        case Status::SUCCESS: out += "SUCCESS"; break;
        case Status::FAILURE: out += "FAILURE"; break;
        case Status::RUNNING: out += "RUNNING"; break;
    }
    out += R"(","activePath":[)";
    for (std::size_t i = 0; i < rec.activePath.size(); ++i) {
        if (i > 0) { out += ','; }
        out += R"({"name":")"; out += rec.activePath[i].name;
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
        } catch (...) {}
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
    impl_->server.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        const std::string html = readFile(uiDir_ + "/editor.html");
        if (html.empty()) { res.status = 404; res.set_content("editor.html not found", "text/plain"); return; }
        res.set_content(html, "text/html");
    });

    impl_->server.Get("/editor.js", [this](const httplib::Request&, httplib::Response& res) {
        const std::string js = readFile(uiDir_ + "/editor.js");
        if (js.empty()) { res.status = 404; res.set_content("editor.js not found", "text/plain"); return; }
        res.set_content(js, "application/javascript");
    });

    impl_->server.Get("/api/actions", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getActionsJson(), "application/json");
    });
    impl_->server.Get("/api/conditions", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getConditionsJson(), "application/json");
    });
    impl_->server.Get("/api/blackboard", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getBlackboardJson(), "application/json");
    });
    impl_->server.Get("/api/schema", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getSchemaJson(), "application/json");
    });
    impl_->server.Get("/api/analyze", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getAnalyzeJson(), "application/json");
    });
    impl_->server.Get("/api/tree", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getTreeJson(), "application/json");
    });
    impl_->server.Get("/api/tickstate", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getTickStateJson(), "application/json");
    });

    impl_->server.Post("/api/schema", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string yamlContent = extractJsonStringField(req.body, "yaml");
        if (yamlContent.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"missing yaml field in request body"})", "application/json");
            return;
        }
        try { std::ignore = SchemaParser::parse(yamlContent); }
        catch (const std::exception& exc) {
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

    impl_->server.Put("/api/actions", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string name = extractJsonStringField(req.body, "name");
        if (name.empty()) { res.status = 400; res.set_content(R"({"error":"name required"})", "application/json"); return; }
        putAction(name, extractJsonStringField(req.body, "intent"),
                  extractJsonStringArray(req.body, "reads"),
                  extractJsonStringArray(req.body, "writes"));
        res.set_content(R"({"ok":true})", "application/json");
    });
    impl_->server.Delete("/api/actions", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string name = req.get_param_value("name");
        if (name.empty()) { res.status = 400; res.set_content(R"({"error":"name required"})", "application/json"); return; }
        removeAction(name);
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Put("/api/conditions", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string name = extractJsonStringField(req.body, "name");
        if (name.empty()) { res.status = 400; res.set_content(R"({"error":"name required"})", "application/json"); return; }
        putCondition(name, extractJsonStringField(req.body, "intent"),
                     extractJsonStringArray(req.body, "reads"));
        res.set_content(R"({"ok":true})", "application/json");
    });
    impl_->server.Delete("/api/conditions", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string name = req.get_param_value("name");
        if (name.empty()) { res.status = 400; res.set_content(R"({"error":"name required"})", "application/json"); return; }
        removeCondition(name);
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->server.Put("/api/blackboard", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string key = extractJsonStringField(req.body, "key");
        if (key.empty()) { res.status = 400; res.set_content(R"({"error":"key required"})", "application/json"); return; }
        putStateKey(key, extractJsonStringField(req.body, "type"));
        res.set_content(R"({"ok":true})", "application/json");
    });
    impl_->server.Delete("/api/blackboard", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string key = req.get_param_value("key");
        if (key.empty()) { res.status = 400; res.set_content(R"({"error":"key required"})", "application/json"); return; }
        removeStateKey(key);
        res.set_content(R"({"ok":true})", "application/json");
    });

    impl_->thread = std::thread([this, port] {
        impl_->server.listen("localhost", port);
    });

    while (!impl_->server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    running_ = true;
}

void EditorServer::stop() noexcept {
    impl_->server.stop();
    if (impl_->thread.joinable()) { impl_->thread.join(); }
    running_ = false;
}

}  // namespace bt
