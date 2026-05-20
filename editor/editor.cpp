// Arborist Visual Editor — standalone launcher.
//
// Starts the behavior tree editor with a persistent SQLite registry and an
// optional schema YAML file.  On first run the registry is created empty;
// on subsequent runs it picks up where you left off.
//
// Usage:
//   bt_editor [options]
//
// Options:
//   --db     <path>   Registry database  (default: arborist_registry.db)
//   --schema <path>   Schema YAML file   (default: arborist_schema.yaml)
//   --port   <port>   HTTP port          (default: 8081)
//   --help            Print this message

#include <iostream>
#include <string>

#include "bt/EditorServer.h"
#include "bt/RegistryStore.h"

namespace {

struct Options {
    std::string dbPath     = "arborist_registry.db";
    std::string schemaPath = "arborist_schema.yaml";
    int         port       = 8081;
    bool        help       = false;
};

Options parseArgs(int argc, char** argv) {
    Options opts;
    for (int idx = 1; idx < argc; ++idx) {
        std::string arg = argv[idx];
        if (arg == "--help" || arg == "-h") {
            opts.help = true;
        } else if (arg == "--db" && idx + 1 < argc) {
            opts.dbPath = argv[++idx];
        } else if (arg == "--schema" && idx + 1 < argc) {
            opts.schemaPath = argv[++idx];
        } else if (arg == "--port" && idx + 1 < argc) {
            opts.port = std::stoi(argv[++idx]);
        }
    }
    return opts;
}

void printHelp(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --db     <path>   Registry database  (default: arborist_registry.db)\n"
              << "  --schema <path>   Schema YAML file   (default: arborist_schema.yaml)\n"
              << "  --port   <port>   HTTP port          (default: 8081)\n"
              << "  --help            Print this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    const Options opts = parseArgs(argc, argv);

    if (opts.help) {
        printHelp(argv[0]);
        return 0;
    }

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║              Arborist Visual Editor                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "  Registry : " << opts.dbPath     << "\n";
    std::cout << "  Schema   : " << opts.schemaPath << "\n";
    std::cout << "  Port     : " << opts.port       << "\n\n";

    bt::RegistryStore store(opts.dbPath);

    const auto actions    = store.allActions();
    const auto conditions = store.allConditions();
    const auto keys       = store.allStateKeys();

    std::cout << "  Loaded   : " << actions.size()    << " actions, "
                                 << conditions.size() << " conditions, "
                                 << keys.size()       << " blackboard keys\n\n";

    bt::EditorServer editor(store, opts.schemaPath);
    editor.start(opts.port);

    std::cout << "┌──────────────────────────────────────────────────────────────┐\n";
    std::cout << "│  Editor ready:   http://localhost:" << opts.port;
    // Pad to fixed width
    std::string portStr = std::to_string(opts.port);
    std::string pad(26 - portStr.size(), ' ');
    std::cout << pad << "│\n";
    std::cout << "│                                                              │\n";
    std::cout << "│  REST endpoints:                                             │\n";
    std::cout << "│    GET  /api/actions      list registered actions            │\n";
    std::cout << "│    GET  /api/conditions   list registered conditions         │\n";
    std::cout << "│    GET  /api/blackboard   list blackboard keys               │\n";
    std::cout << "│    GET  /api/schema       current schema YAML                │\n";
    std::cout << "│    POST /api/schema       save updated schema YAML           │\n";
    std::cout << "│    GET  /api/analyze      run complexity analyzer            │\n";
    std::cout << "│                                                              │\n";
    std::cout << "│  Press Enter to stop.                                        │\n";
    std::cout << "└──────────────────────────────────────────────────────────────┘\n\n";

    std::cin.get();

    editor.stop();
    std::cout << "\nEditor stopped.\n";
    return 0;
}
