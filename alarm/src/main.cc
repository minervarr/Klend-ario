// klend_ario_alarmd — the alarm engine (merged.md §6).
//
// A separate long-running frontend: it polls the calendar and fires each event's
// alarms (T-offset and T=0) through a Notifier. It can be shipped on its own
// later, which is why it is its own binary rather than a mode of the CLI.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include "arc/fs/paths.hh"
#include "klend_ario/db.hh"
#include "klend_ario/event.hh"

#include "engine.hh"
#include "notifier.hh"

namespace {

const char *kUsage = "klend_ario_alarmd — fires calendar alarms as they come due\n"
                     "\n"
                     "usage: klend_ario_alarmd [options]\n"
                     "\n"
                     "options:\n"
                     "  --db PATH           database file (default: the app's state dir)\n"
                     "  --key-file PATH     read the passphrase from this file\n"
                     "                      (else $KLEND_ARIO_KEY; else plaintext)\n"
                     "  --interval SECONDS  how often to poll (default 30)\n"
                     "  --horizon SECONDS   catch-up window on the first poll (default 60)\n"
                     "  --notify auto|log   auto = libnotify when available (default),\n"
                     "                      log = always write to stderr\n"
                     "  --once              poll once and exit\n"
                     "  -h, --help          this text\n";

std::string defaultDbPath() {
    const arc::fs::Layout layout = arc::fs::layout("klend_ario");
    arc::fs::ensureLayout(layout, arc::fs::LayoutScope::App);
    return arc::fs::join(layout.state, "klend_ario.db");
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
    std::string notify = "auto";
    long long interval = 30;
    long long horizon = 60;
    bool once = false;

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
        } else if (arg == "--interval") {
            std::string value;
            takeValue(value);
            interval = std::strtoll(value.c_str(), nullptr, 10);
        } else if (arg == "--horizon") {
            std::string value;
            takeValue(value);
            horizon = std::strtoll(value.c_str(), nullptr, 10);
        } else if (arg == "--notify") {
            takeValue(notify);
        } else if (arg == "--once") {
            once = true;
        } else if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (interval < 1) {
        interval = 1;
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

    auto notifier = notify == "log" ? klend_ario::alarm::makeLogNotifier()
                                    : klend_ario::alarm::makeNotifier();
    klend_ario::alarm::AlarmEngine engine(opened.take(), *notifier);

    if (once) {
        engine.poll(klend_ario::utcNowSeconds(), horizon);
        return 0;
    }
    for (;;) {
        engine.poll(klend_ario::utcNowSeconds(), horizon);
        std::this_thread::sleep_for(std::chrono::seconds(interval));
    }
}
