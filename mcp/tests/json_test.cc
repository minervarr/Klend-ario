#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "json.hh"

using namespace klend_ario::mcp;

namespace {

Json parse(const std::string &text) {
    Json json;
    std::string error;
    const bool ok = parseJson(text, json, error);
    assert(ok);
    (void)error;
    return json;
}

void testParseObject() {
    Json json = parse(R"({"a":1,"b":[true,false,null],"c":"x"})");
    assert(json.isObject());
    assert(json.find("a")->asInteger() == 1);
    assert(json.find("b")->isArray());
    assert(json.find("b")->items().size() == 3);
    assert(json.find("b")->items()[0].asBool());
    assert(!json.find("b")->items()[1].asBool());
    assert(json.find("b")->items()[2].isNull());
    assert(json.find("c")->asString() == "x");
    assert(json.find("missing") == nullptr);
}

void testNumberFormatting() {
    // Integers must not gain a ".0": an event id is 42, not 42.0.
    assert(parse("42").dump() == "42");
    assert(parse("-7").dump() == "-7");
    assert(parse("1.5").dump() == "1.5");
}

void testStringEscapes() {
    Json json = parse(R"("a\"b\\c\n\t")");
    assert(json.asString() == "a\"b\\c\n\t");
    assert(parse(json.dump()).asString() == json.asString());
}

void testUnicodeEscape() {
    // \u00e9 is U+00E9, which as UTF-8 is 0xC3 0xA9.
    assert(parse(R"("\u00e9")").asString() == "\xc3\xa9");
}

void testObjectRoundTrip() {
    const std::string text = R"({"id":3,"hidden":true,"notes":"a\nb"})";
    assert(parse(text).dump() == text);
}

void testErrors() {
    Json json;
    std::string error;
    assert(!parseJson("{\"a\":}", json, error) && !error.empty());
    assert(!parseJson("", json, error));
    assert(!parseJson("[1,2", json, error));
    assert(!parseJson("nul", json, error));
    assert(!parseJson("{} trailing", json, error));
    assert(!parseJson("\"unterminated", json, error));
}

} // namespace

int main() {
    testParseObject();
    testNumberFormatting();
    testStringEscapes();
    testUnicodeEscape();
    testObjectRoundTrip();
    testErrors();
    std::printf("json_test: all assertions passed\n");
    return 0;
}
