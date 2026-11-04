#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "text_inputs.hh"

using namespace klend_ario;
using namespace klend_ario::gui;

static bool has(const std::vector<uint32_t> &codepoints, uint32_t cp) {
    return std::binary_search(codepoints.begin(), codepoints.end(), cp);
}

int main() {
    WeekModel week;
    week.weekLabel = "Week 42 \xC2\xB7 Oct 2026";

    Event accented;
    accented.title = "D\xC3\xAD" "a \xE2\x80\xA6"; // "Día …"
    week.events.push_back(accented);

    Event plain;
    plain.title = "Ship the GUI";
    week.events.push_back(plain);

    UiMetrics metrics;
    metrics.weekLabelSize = 27;
    metrics.navLabelSize = 20;
    metrics.dayLetterSize = 22;
    metrics.dayDateSize = 18; // reserved today: it must not be baked
    metrics.eventTimeSize = 16;
    metrics.eventTitleSize = 19;

    const TextInputs inputs = collectTextInputs(week, metrics);

    // Sorted, unique, and exactly the roles the scene draws at.
    assert(std::is_sorted(inputs.codepoints.begin(), inputs.codepoints.end()));
    assert(std::adjacent_find(inputs.codepoints.begin(), inputs.codepoints.end()) ==
           inputs.codepoints.end());
    assert(std::is_sorted(inputs.sizesPx.begin(), inputs.sizesPx.end()));
    assert((inputs.sizesPx == std::vector<int>{16, 19, 20, 22, 27}));

    // Static labels, the ellipsis fitText() appends, and the week label's dot.
    assert(has(inputs.codepoints, 'A'));
    assert(has(inputs.codepoints, '0'));
    assert(has(inputs.codepoints, ':'));
    assert(has(inputs.codepoints, '<'));
    assert(has(inputs.codepoints, '>'));
    assert(has(inputs.codepoints, 0x00B7));
    assert(has(inputs.codepoints, 0x2026));

    // A title's non-ASCII text is collected too.
    assert(has(inputs.codepoints, 0x00ED)); // í
    assert(has(inputs.codepoints, 'S'));

    // Printable ASCII only: a title can never drag control bytes into the bake.
    assert(!has(inputs.codepoints, 0x000A));

    // The modal adds field values at its own sizes; non-ASCII text is collected
    // there too, and merging keeps both sorted and unique.
    const TextInputs editor =
        collectEditorTextInputs({"D\xC3\xAD" "a", "09:00", "Deep work"}, {16, 20});
    assert(std::is_sorted(editor.codepoints.begin(), editor.codepoints.end()));
    assert(std::adjacent_find(editor.codepoints.begin(), editor.codepoints.end()) ==
           editor.codepoints.end());
    assert((editor.sizesPx == std::vector<int>{16, 20}));
    assert(has(editor.codepoints, 0x00ED)); // í
    assert(has(editor.codepoints, ':'));

    const TextInputs merged = mergeTextInputs(inputs, editor);
    assert(std::is_sorted(merged.codepoints.begin(), merged.codepoints.end()));
    assert(std::adjacent_find(merged.codepoints.begin(), merged.codepoints.end()) ==
           merged.codepoints.end());
    assert(std::is_sorted(merged.sizesPx.begin(), merged.sizesPx.end()));
    assert((merged.sizesPx == std::vector<int>{16, 19, 20, 22, 27}));

    std::printf("text_inputs_test: all assertions passed\n");
    return 0;
}
