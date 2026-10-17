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

constexpr const char *kVersion = "0.1.0";

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
    "                       A read-only server also opens the database read-only\n"
    "  -h, --help           this text\n"
    "  --version            print the version\n";

std::string defaultDbPath(bool create) {
    const arc::fs::Layout layout = arc::fs::layout("klend_ario");
    if (create) {
        arc::fs::ensureLayout(layout);
    }
    return arc::fs::join(layout.state, "klend_ario.db");
}

bool parsePermissions(const std::string &list, klend_ario::mcp::Permissions &out,
                      std::string &error) {
    // `out = {}` would apply the permissive defaults (all true); a parsed list
    // is exactly the tiers named, so start from none.
    out.read = false;
    out.write = false;
    out.destructive = false;
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const std::string token = list.substr(start, comma == std::string::npos
                                                         ? std::string::npos
                                                         : comma - start);
        if (token == "read") {
            out.read = true;
        } else if (token == "write") {
            out.write = true;
        } else if (token == "destructive") {
            out.destructive = true;
        } else if (token.empty()) {
            error = "empty permission token in --permissions";
            return false;
        } else {
            error = "unknown permission \"" + token + "\" (use read, write, destructive)";
            return false;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (!out.read && !out.write && !out.destructive) {
        error = "--permissions needs at least one of read, write, destructive";
        return false;
    }
    return true;
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
        auto takeValue = [&](const char *name, std::string &slot) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", name);
                return false;
            }
            slot = argv[++i];
            return true;
        };
        if (arg == "--db") {
            if (!takeValue("--db", dbPath)) {
                return 2;
            }
        } else if (arg == "--key-file") {
            if (!takeValue("--key-file", keyFile)) {
                return 2;
            }
        } else if (arg == "--permissions") {
            if (!takeValue("--permissions", permissions)) {
                return 2;
            }
        } else if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--version") {
            std::printf("klend_ario_mcp_server %s\n", kVersion);
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }

    klend_ario::mcp::Permissions perms;
    std::string permissionError;
    if (!parsePermissions(permissions, perms, permissionError)) {
        std::fprintf(stderr, "error: %s\n", permissionError.c_str());
        return 2;
    }
    // A deployment that can only read should not be able to create or migrate
    // a database by accident. This is the same SQLite mode `doctor` uses.
    const bool readOnly = perms.read && !perms.write && !perms.destructive;

    std::string keyError;
    const std::string passphrase = resolvePassphrase(keyFile, keyError);
    if (!keyError.empty()) {
        std::fprintf(stderr, "error: %s\n", keyError.c_str());
        return 1;
    }

    const std::string path = dbPath.empty() ? defaultDbPath(!readOnly) : dbPath;
    auto opened =
        readOnly ? (passphrase.empty() ? klend_ario::Db::openReadOnly(path)
                                       : klend_ario::Db::openEncryptedReadOnly(path, passphrase))
                 : (passphrase.empty() ? klend_ario::Db::open(path)
                                       : klend_ario::Db::openEncrypted(path, passphrase));
    if (!opened) {
        std::fprintf(stderr, "error: %s\n", opened.error().message.c_str());
        if (readOnly) {
            std::fprintf(stderr,
                         "hint: a read-only server never creates or migrates a database; run a "
                         "write once, or enable the write tier\n");
        }
        return 1;
    }

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
