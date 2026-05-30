#include "bt/MonitorServer.h"

#include <any>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>

#include <httplib.h>

#include "bt/DecisionEmitter.h"
#include "bt/Status.h"
#include "bt/TreeSerializer.h"

namespace bt {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) { return ""; }
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

std::string_view statusName(Status status) {
    switch (status) {
        case Status::SUCCESS: return "SUCCESS";
        case Status::FAILURE: return "FAILURE";
        case Status::RUNNING: return "RUNNING";
    }
    return "UNKNOWN";
}

std::string anyToJson(const std::any& value) {
    if (value.type() == typeid(int))         { return std::to_string(std::any_cast<int>(value)); }
    if (value.type() == typeid(bool))        { return std::any_cast<bool>(value) ? "true" : "false"; }
    if (value.type() == typeid(float))       { return std::to_string(std::any_cast<float>(value)); }
    if (value.type() == typeid(double))      { return std::to_string(std::any_cast<double>(value)); }
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

struct MonitorServer::Impl {
    httplib::Server server;
    std::thread thread;
};

MonitorServer::MonitorServer(const BehaviorTree& tree, const DecisionEmitter& emitter,
                             std::string_view uiDir)
    : tree_(&tree), emitter_(&emitter), uiDir_(uiDir), impl_(std::make_unique<Impl>()) {}

MonitorServer::~MonitorServer() {
    if (running_) { stop(); }
}

void MonitorServer::start(int port) {
    impl_->server.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        const std::string html = readFile(uiDir_ + "/viewer.html");
        if (html.empty()) { res.status = 404; res.set_content("viewer.html not found", "text/plain"); return; }
        res.set_content(html, "text/html");
    });

    impl_->server.Get("/viewer.js", [this](const httplib::Request&, httplib::Response& res) {
        const std::string js = readFile(uiDir_ + "/viewer.js");
        if (js.empty()) { res.status = 404; res.set_content("viewer.js not found", "text/plain"); return; }
        res.set_content(js, "application/javascript");
    });

    impl_->server.Get("/simulator", [this](const httplib::Request&, httplib::Response& res) {
        const std::string html = readFile(uiDir_ + "/simulator.html");
        if (html.empty()) { res.status = 404; res.set_content("simulator.html not found", "text/plain"); return; }
        res.set_content(html, "text/html");
    });

    impl_->server.Get("/simulator.js", [this](const httplib::Request&, httplib::Response& res) {
        const std::string js = readFile(uiDir_ + "/simulator.js");
        if (js.empty()) { res.status = 404; res.set_content("simulator.js not found", "text/plain"); return; }
        res.set_content(js, "application/javascript");
    });

    impl_->server.Get("/tree", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(TreeSerializer::toJson(tree_->root()), "application/json");
    });

    impl_->server.Get("/history", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(serializeHistory(*emitter_), "application/json");
    });

    impl_->thread = std::thread([this, port] {
        impl_->server.listen("localhost", port);
    });

    while (!impl_->server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    running_ = true;
}

void MonitorServer::stop() noexcept {
    impl_->server.stop();
    if (impl_->thread.joinable()) { impl_->thread.join(); }
    running_ = false;
}

std::string MonitorServer::getTree() const {
    return TreeSerializer::toJson(tree_->root());
}

std::string MonitorServer::getHistory() const {
    return serializeHistory(*emitter_);
}

}  // namespace bt
