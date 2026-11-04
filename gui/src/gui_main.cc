// klend_ario_gui — the weekly view.
//
// app_shell owns main() on Wayland; this file defines what runs once the
// platform bootstrap is done. The command line stays small: which database,
// which key, and whether this deployment is allowed to write at all.

#include <cstdio>
#include <cstring>

#include "app_main.hh"
#include "week_view.hh"

namespace {

void printUsage() {
    std::fputs(
        "usage: klend_ario_gui [--db <path>] [--key-file <path>] [--hidden] [--fullscreen]\n"
        "\n"
        "The weekly view and editor.\n"
        "\n"
        "  --db <path>       database file (default: the app's state database)\n"
        "  --key-file <path> first line is the passphrase (or set $KLEND_ARIO_KEY)\n"
        "  --hidden          show hidden blocks (sleep) instead of hiding them\n"
        "  --read-only       viewer only: no editing, and the file must exist\n"
        "  --screenshot-dir  where F12 writes PNGs (default: the app's data dir)\n"
        "  --fullscreen      fill the monitor instead of a 1280x720 window\n"
        "  --help            this text\n"
        "\n"
        "keys: Left/Right week · T today · H hidden · N new · Enter edit · Esc close/quit\n"
        "      F12 screenshot\n",
        stderr);
}

} // namespace

int app_shell_main(int argc, char **argv) {
    klend_ario::gui::ViewOptions options;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage();
            return 0;
        }
        if (std::strcmp(arg, "--hidden") == 0) {
            options.showHidden = true;
            continue;
        }
        if (std::strcmp(arg, "--read-only") == 0) {
            options.readOnly = true;
            continue;
        }
        if (std::strcmp(arg, "--fullscreen") == 0) {
            options.fullscreen = true;
            continue;
        }
        if (std::strcmp(arg, "--db") == 0 || std::strcmp(arg, "--key-file") == 0 ||
            std::strcmp(arg, "--screenshot-dir") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", arg);
                return 2;
            }
            if (std::strcmp(arg, "--db") == 0) {
                options.dbPath = argv[++i];
            } else if (std::strcmp(arg, "--key-file") == 0) {
                options.keyFile = argv[++i];
            } else {
                options.screenshotDir = argv[++i];
            }
            continue;
        }
        std::fprintf(stderr, "error: unknown option: %s\n", arg);
        printUsage();
        return 2;
    }

    klend_ario::gui::WeekView view;
    if (!view.create(options)) {
        return 1;
    }
    view.run();
    return 0;
}
