#pragma once

#include <memory>
#include <string>

#include "bt/BehaviorTree.h"
#include "bt/DecisionEmitter.h"

namespace bt {

// Embedded HTTP server for the live tree viewer.
// Construction is cheap — nothing starts until start(port) is called.
// Intended for dev/debug use only: wire it up with a flag at init time,
// never construct it in production builds where no viewer is needed.
class MonitorServer {
public:
    MonitorServer(const BehaviorTree& tree, const DecisionEmitter& emitter,
                  std::string_view uiDir = "");
    ~MonitorServer();

    MonitorServer(const MonitorServer&) = delete;
    MonitorServer& operator=(const MonitorServer&) = delete;
    MonitorServer(MonitorServer&&) = delete;
    MonitorServer& operator=(MonitorServer&&) = delete;

    // Start listening on the given port. Blocks until the server is ready.
    void start(int port = 8080);

    // Stop the server and join the listener thread.
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return running_; }

    // Direct data accessors — return the same JSON that the HTTP endpoints serve.
    // Useful for tests and for embedding the server output in other tools.
    [[nodiscard]] std::string getTree() const;
    [[nodiscard]] std::string getHistory() const;

private:
    const BehaviorTree* tree_;
    const DecisionEmitter* emitter_;
    std::string uiDir_;
    bool running_{false};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bt
