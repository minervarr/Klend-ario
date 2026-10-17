#include "completion.hh"

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace klend_ario::cli {
namespace {

// Completion is generated from one table. The table is the source of truth for
// which option belongs to which verb; the three shells only differ in syntax.
// Keep this in sync with `Verb`/`parseArgs` (completion_test pins the surface).

struct OptionSpec {
    std::string_view name;
    bool takesValue = false;
    std::string_view description;
    std::string_view zshValue; // empty => ":value:"
};

struct VerbSpec {
    std::string_view name;
    std::vector<std::string_view> options;
    std::string_view extraWords; // e.g. "$verbs" or "bash zsh fish"
};

constexpr OptionSpec kOptions[] = {
    {"--db", true, "database path", ":path:_files"},
    {"--key-file", true, "passphrase file", ":path:_files"},
    {"--format", true, "output or input format", ":format:"},
    {"--json", false, "JSON output (JSONL for export/import)", ""},
    {"--no-header", false, "omit the header row", ""},
    {"--from", true, "range start", ":instant:"},
    {"--to", true, "range end", ":instant:"},
    {"--type", true, "event type", ":type:(event sleep wake alarm)"},
    {"--search", true, "literal text", ":text:"},
    {"--min-importance", true, "minimum importance (0..100)", ":value:"},
    {"--max-importance", true, "maximum importance (0..100)", ":value:"},
    {"--min-urgency", true, "minimum urgency (0..100)", ":value:"},
    {"--max-urgency", true, "maximum urgency (0..100)", ":value:"},
    {"--sort", true, "order", ":sort:(start -start importance urgency updated title)"},
    {"--limit", true, "maximum rows", ":value:"},
    {"--offset", true, "skip rows", ":value:"},
    {"--days", true, "agenda window in days", ":value:"},
    {"--min-duration", true, "shortest free slot", ":duration:"},
    {"--visible", false, "only visible rows", ""},
    {"--hidden-only", false, "only hidden rows", ""},
    {"--include-hidden", false, "include hidden rows", ""},
    {"--country", true, "holiday country", ":country:"},
    {"--year", true, "holiday year", ":value:"},
    {"--title", true, "event title", ":text:"},
    {"--start", true, "start instant", ":instant:"},
    {"--end", true, "end instant", ":instant:"},
    {"--duration", true, "duration", ":duration:"},
    {"--shift", true, "shift", ":duration:"},
    {"--importance", true, "importance (0..100)", ":value:"},
    {"--urgency", true, "urgency (0..100)", ":value:"},
    {"--alarm-offset", true, "seconds before start", ":value:"},
    {"--notes", true, "notes", ":text:"},
    {"--clear-notes", false, "clear notes", ""},
    {"--hidden", false, "hide in views", ""},
    {"--unhide", false, "show in views", ""},
    {"--replace", false, "clear before import", ""},
    {"--preserve-ids", false, "keep ids on import", ""},
    {"--dry-run", false, "parse only", ""},
};

const std::vector<std::string_view> kVerbs = {
    "add",        "edit",      "duplicate", "list",       "today",
    "week",       "next",      "agenda",    "search",     "conflicts",
    "free",       "alarms",    "stats",     "doctor",     "export",
    "import",     "completion", "show",     "rm",         "holidays",
    "help",
};

const OptionSpec &optionSpec(std::string_view name) {
    for (const OptionSpec &option : kOptions) {
        if (option.name == name) {
            return option;
        }
    }
    static const OptionSpec fallback{"--unknown", false, "option", ""};
    return fallback;
}

std::string join(const std::vector<std::string_view> &items, std::string_view separator) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out += separator;
        }
        out += items[i];
    }
    return out;
}

std::vector<VerbSpec> verbSpecs() {
    const std::vector<std::string_view> db = {"--db", "--key-file"};
    const std::vector<std::string_view> format = {"--format", "--json"};
    const std::vector<std::string_view> range = {"--from", "--to", "--type"};
    const std::vector<std::string_view> visibility = {
        "--visible", "--hidden-only", "--include-hidden"};
    const std::vector<std::string_view> queryFilters = {
        "--search",      "--min-importance", "--max-importance",
        "--min-urgency", "--max-urgency",
    };

    auto merge = [](std::initializer_list<std::vector<std::string_view>> groups) {
        std::vector<std::string_view> out;
        for (const std::vector<std::string_view> &group : groups) {
            out.insert(out.end(), group.begin(), group.end());
        }
        return out;
    };

    std::vector<VerbSpec> specs;
    auto add = [&](std::string_view name, std::vector<std::string_view> options,
                   std::string_view extra = "") {
        specs.push_back({name, std::move(options), extra});
    };

    add("add", merge({db, format,
                      {"--title", "--start", "--end", "--duration", "--type", "--importance",
                       "--urgency", "--alarm-offset", "--notes", "--hidden"}}));
    add("edit", merge({db, format,
                       {"--title", "--start", "--end", "--duration", "--shift", "--type",
                        "--importance", "--urgency", "--alarm-offset", "--notes",
                        "--clear-notes", "--hidden", "--unhide"}}));
    add("duplicate", merge({db, format, {"--start", "--shift"}}));

    for (const std::string_view name : {"list", "today", "week", "next", "agenda", "search"}) {
        std::vector<std::string_view> options =
            merge({db, format, {"--no-header"}, range, queryFilters});
        if (name != "next") {
            options.push_back("--sort");
        }
        options.push_back("--limit");
        options.push_back("--offset");
        if (name == "agenda") {
            options.push_back("--days");
        }
        add(name, merge({options, visibility}));
    }

    add("conflicts",
        merge({db, format, {"--no-header"}, range, {"--limit"}, visibility}));
    add("free", merge({db, format, {"--no-header"}, range, {"--limit", "--min-duration"},
                       visibility}));
    add("alarms", merge({db, format, {"--no-header"}, range, {"--limit"}, visibility}));
    add("stats", merge({db, format, {"--no-header"}, range, visibility}));
    add("doctor", merge({db, format, {"--no-header"}}));
    add("export", merge({db, format, range, visibility}));
    add("import", merge({db, format, {"--replace", "--preserve-ids", "--dry-run"}}));
    add("completion", {}, "bash zsh fish");
    add("show", merge({db, format, {"--no-header"}}));
    add("rm", merge({db, format}));
    add("holidays", merge({{"--format", "--json", "--no-header"}, {"--country", "--year"}}));
    add("help", {}, "$verbs");
    return specs;
}

std::string zshOption(const OptionSpec &option) {
    std::string out = "'";
    out += option.name;
    out += '[';
    out += option.description;
    out += ']';
    if (option.takesValue) {
        out += option.zshValue.empty() ? ":value:" : std::string(option.zshValue);
    }
    out += "'";
    return out;
}

std::string bashScript(const std::vector<VerbSpec> &specs) {
    std::string out =
        "# bash completion for klend_ario_cli\n"
        "# generated by `klend_ario_cli completion bash`\n"
        "_klend_ario_cli()\n"
        "{\n"
        "    local cur prev words\n"
        "    COMPREPLY=()\n"
        "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
        "    local verbs=\"";
    out += join(kVerbs, " ");
    out += "\"\n"
           "    if [[ $COMP_CWORD -eq 1 ]]; then\n"
           "        COMPREPLY=( $(compgen -W \"$verbs --help --version\" -- \"$cur\") )\n"
           "        return 0\n"
           "    fi\n"
           "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
           "    if [[ $prev == \"--db\" || $prev == \"--key-file\" ]]; then\n"
           "        COMPREPLY=( $(compgen -f -- \"$cur\") )\n"
           "        return 0\n"
           "    fi\n"
           "    if [[ ${COMP_WORDS[1]} == import && $cur != -* ]]; then\n"
           "        COMPREPLY=( $(compgen -f -- \"$cur\") )\n"
           "        return 0\n"
           "    fi\n"
           "    case \"${COMP_WORDS[1]}\" in\n";
    for (const VerbSpec &spec : specs) {
        out += "        ";
        out += spec.name;
        out += ") words=\"";
        for (const std::string_view option : spec.options) {
            out += option;
            out += ' ';
        }
        if (!spec.extraWords.empty()) {
            out += spec.extraWords;
            out += ' ';
        }
        out += "--help -h\";;\n";
    }
    out +=
        "        *) words=\"--help -h\";;\n"
        "    esac\n"
        "    COMPREPLY=( $(compgen -W \"$words\" -- \"$cur\") )\n"
        "}\n"
        "complete -F _klend_ario_cli klend_ario_cli\n";
    return out;
}

std::string zshScript(const std::vector<VerbSpec> &specs) {
    std::string out =
        "#compdef klend_ario_cli\n"
        "# generated by `klend_ario_cli completion zsh`\n"
        "_klend_ario_cli()\n"
        "{\n"
        "    local -a verbs\n"
        "    verbs=(";
    out += join(kVerbs, " ");
    out += ")\n"
           "    _arguments '1:command:->command' '*::arg:->args'\n"
           "    case $state in\n"
           "        command)\n"
           "            _describe 'command' verbs\n"
           "            compadd -- --help --version\n"
           "            ;;\n"
           "        args)\n"
           "            case $words[2] in\n";
    for (const VerbSpec &spec : specs) {
        out += "                ";
        out += spec.name;
        out += ")\n";
        if (spec.name == "help") {
            out += "                    compadd -- $verbs\n";
        } else if (spec.name == "completion") {
            out += "                    compadd -- bash zsh fish\n";
        } else {
            out += "                    _arguments \\\n";
            for (const std::string_view option : spec.options) {
                out += "                        ";
                out += zshOption(optionSpec(option));
                out += " \\\n";
            }
            out += "                        '-h[show help]' '--help[show help]'";
            if (spec.name == "import") {
                out += " ':file:_files'";
            }
            out += "\n";
        }
        out += "                    ;;\n";
    }
    out +=
        "                *) ;;\n"
        "            esac\n"
        "            ;;\n"
        "    esac\n"
        "}\n"
        "_klend_ario_cli \"$@\"\n";
    return out;
}

std::string fishScript(const std::vector<VerbSpec> &specs) {
    std::string out =
        "# fish completion for klend_ario_cli\n"
        "# generated by `klend_ario_cli completion fish`\n"
        "complete -c klend_ario_cli -f\n"
        "complete -c klend_ario_cli -n '__fish_use_subcommand' -a '";
    out += join(kVerbs, " ");
    out += "'\n";

    std::map<std::string_view, std::vector<std::string_view>> byOption;
    for (const VerbSpec &spec : specs) {
        for (const std::string_view option : spec.options) {
            byOption[option].push_back(spec.name);
        }
    }

    for (const OptionSpec &option : kOptions) {
        const auto found = byOption.find(option.name);
        if (found == byOption.end()) {
            continue;
        }
        out += "complete -c klend_ario_cli -n '__fish_seen_subcommand_from ";
        out += join(found->second, " ");
        out += "'";
        if (option.name == "-h") {
            out += " -s h";
        } else {
            out += " -l ";
            out += option.name.substr(2);
        }
        if (option.takesValue) {
            out += " -r";
        }
        if (option.name == "--db" || option.name == "--key-file") {
            out += " -F";
        }
        out += " -d '";
        out += option.description;
        out += "'\n";
    }

    out +=
        "complete -c klend_ario_cli -s h -l help -d 'show help'\n"
        "complete -c klend_ario_cli -s V -l version -d 'print version'\n"
        "complete -c klend_ario_cli -n '__fish_seen_subcommand_from completion' -a 'bash zsh fish'\n"
        "complete -c klend_ario_cli -n '__fish_seen_subcommand_from help' -a '";
    out += join(kVerbs, " ");
    out += "'\n"
           "complete -c klend_ario_cli -n '__fish_seen_subcommand_from import' -F\n";
    return out;
}

} // namespace

std::string completionScript(std::string_view shell) {
    const std::vector<VerbSpec> specs = verbSpecs();
    if (shell == "bash") {
        return bashScript(specs);
    }
    if (shell == "zsh") {
        return zshScript(specs);
    }
    if (shell == "fish") {
        return fishScript(specs);
    }
    return {};
}

} // namespace klend_ario::cli
