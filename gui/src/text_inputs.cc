#include "text_inputs.hh"

#include <set>

namespace klend_ario::gui {
namespace {

// Decode one UTF-8 codepoint. Malformed input yields U+FFFD and advances one
// byte, so a bad title can never make the collector loop forever. This mirrors
// the replacement policy in the font engine's utf8.hh; it is duplicated here
// because klend_ario_view is built without the GUI/font-engine include path.
uint32_t nextCodepoint(const std::string &text, size_t &i) {
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80) {
        return first;
    }

    int extra = 0;
    uint32_t cp = 0;
    if ((first & 0xE0) == 0xC0) {
        extra = 1;
        cp = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        extra = 2;
        cp = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        extra = 3;
        cp = first & 0x07;
    } else {
        return 0xFFFD;
    }

    for (int k = 0; k < extra; ++k) {
        if (i >= text.size()) {
            return 0xFFFD;
        }
        const auto next = static_cast<unsigned char>(text[i]);
        if ((next & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        ++i;
        cp = (cp << 6) | (next & 0x3F);
    }
    return cp;
}

void addCodepoints(std::set<uint32_t> &out, const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        out.insert(nextCodepoint(text, i));
    }
}

} // namespace

TextInputs collectTextInputs(const WeekModel &week, const UiMetrics &metrics) {
    std::set<uint32_t> codepoints;

    // Printable ASCII covers every static label: day letters, "<"/"today"/">",
    // the empty-state sentence, digits, the clock colon and month abbreviations.
    for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) {
        codepoints.insert(cp);
    }
    codepoints.insert(0x00B7); // · in the week label
    codepoints.insert(0x2026); // … appended by fitText()

    addCodepoints(codepoints, week.weekLabel);
    for (const Event &event : week.events) {
        addCodepoints(codepoints, event.title);
    }

    // The exact sizes the scene's text ops use. `dayDateSize` is deliberately
    // absent: no text op draws at it today. Add it here with the first one that
    // does, or textWidth() answers 0 for that role.
    std::set<int> sizes;
    for (const int size : {metrics.weekLabelSize, metrics.navLabelSize,
                           metrics.dayLetterSize, metrics.eventTimeSize,
                           metrics.eventTitleSize}) {
        if (size > 0) {
            sizes.insert(size);
        }
    }

    TextInputs out;
    out.codepoints.assign(codepoints.begin(), codepoints.end());
    out.sizesPx.assign(sizes.begin(), sizes.end());
    return out;
}

} // namespace klend_ario::gui
