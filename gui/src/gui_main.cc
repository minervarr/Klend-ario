// klend_ario_gui — the weekly view.
//
// app_shell owns main() on Wayland; this file defines what runs once the
// platform bootstrap is done. The command line is small on purpose: this slice
// is a viewer, so it has nothing to say beyond which database and which key.

#include <cstdio>
#include <cstring>

#include "app_main.hh"
#include "week_view.hh"

namespace {

void printUsage() {
    std::fputs(
        "usage: klend_ario_gui [--db <path>] [--key-file <path>] [--hidden]\n"
        "\n"
        "The weekly view. Read-only for now; edit with klend_ario_cli or the MCP server.\n"
        "\n"
        "  --db <path>       database file (default: the app's state database)\n"
        "  --key-file <path> first line is the passphrase (or set $KLEND_ARIO_KEY)\n"
        "  --hidden          show hidden blocks (sleep) instead of hiding them\n"
        "  --help            this text\n"
        "\n"
        "keys: Left/Right previous/next week · T today · H toggle hidden · Esc quit\n",
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
        if (std::strcmp(arg, "--db") == 0 || std::strcmp(arg, "--key-file") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", arg);
                return 2;
            }
            if (std::strcmp(arg, "--db") == 0) {
                options.dbPath = argv[++i];
            } else {
                options.keyFile = argv[++i];
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
