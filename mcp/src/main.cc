// klend_ario_mcp_server — the local MCP server (merged.md §7).
//
// One JSON-RPC message per line on stdin, one response per line on stdout, and
// nothing else on stdout ever (a stray print corrupts the protocol). It is a
// frontend: it opens the database through core and never reaches past it.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "arc/fs/paths.hh"
#include "klend_ario/db.hh"

#include "server.hh"

namespace {

const char *kUsage =
    "klend_ario_mcp_server — a local MCP server for the calendar\n"
    "\n"
    "usage: klend_ario_mcp_server [options]\n"
    "\n"
    "options:\n"
    "  --db PATH            database file (default: the app's state dir)\n"
    "  --key-file PATH      read the passphrase from this file\n"
    "                       (else $KLEND_ARIO_KEY; else plaintext)\n"
    "  --permissions LIST   which tiers are enabled: any of\n"
    "                       read,write,destructive (default: all)\n"
    "  -h, --help           this text\n";

std::string defaultDbPath() {
    const arc::fs::Layout layout = arc::fs::layout("klend_ario");
    arc::fs::ensureLayout(layout);
    return arc::fs::join(layout.state, "klend_ario.db");
}

bool csvContains(const std::string &list, const std::string &token) {
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const std::string piece = list.substr(start, comma == std::string::npos
                                                        ? std::string::npos
                                                        : comma - start);
        if (piece == token) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

std::string resolvePassphrase(const std::string &keyFile, std::string &error) {
    if (!keyFile.empty()) {
        std::ifstream in(keyFile, std::ios::binary);
        std::string line;
        if (!std::getline(in, line)) {
            error = "cannot read key file: " + keyFile;
            return {};
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return line;
    }
    if (const char *env = std::getenv("KLEND_ARIO_KEY"); env != nullptr) {
        return env;
    }
    return {};
}

} // namespace

int main(int argc, char **argv) {
    std::string dbPath;
    std::string keyFile;
    std::string permissions = "read,write,destructive";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto takeValue = [&](std::string &slot) {
            if (i + 1 < argc) {
                slot = argv[++i];
            }
        };
        if (arg == "--db") {
            takeValue(dbPath);
        } else if (arg == "--key-file") {
            takeValue(keyFile);
        } else if (arg == "--permissions") {
            takeValue(permissions);
        } else if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }

    std::string keyError;
    const std::string passphrase = resolvePassphrase(keyFile, keyError);
    if (!keyError.empty()) {
        std::fprintf(stderr, "error: %s\n", keyError.c_str());
        return 1;
    }

    const std::string path = dbPath.empty() ? defaultDbPath() : dbPath;
    auto opened =
        passphrase.empty() ? klend_ario::Db::open(path) : klend_ario::Db::openEncrypted(path, passphrase);
    if (!opened) {
        std::fprintf(stderr, "error: %s\n", opened.error().message.c_str());
        return 1;
    }

    klend_ario::mcp::Permissions perms;
    perms.read = csvContains(permissions, "read");
    perms.write = csvContains(permissions, "write");
    perms.destructive = csvContains(permissions, "destructive");

    klend_ario::mcp::Server server(opened.take(), perms);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        const std::string response = server.handle(line);
        if (!response.empty()) {
            std::fputs(response.c_str(), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }
    }
    return 0;
}
