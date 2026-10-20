// The in-game translator (native/core/ingame_i18n.cpp) gives what the app's translator
// (ui/i18n.js) gives. tools/test_ingame_assets.py writes the cases with Node from ui/i18n.js.
// usage: ingame_i18n_test <ui/i18n-xx.json> <cases.json> <decimalComma 0|1>
//   cases.json: {"English text": "translation" or null (no translation), ...}
#include "../core/ingame_i18n.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string Read(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
}  // namespace

int main(int argc, char** argv) {
    using namespace ep::ingame;
    if (argc != 4) {
        std::fprintf(stderr, "usage: ingame_i18n_test <dictionary.json> <cases.json> <decimalComma 0|1>\n");
        return 2;
    }
    json::Value dictionary, cases;
    if (!json::Parse(Read(argv[1]), dictionary) || !json::Parse(Read(argv[2]), cases)) {
        std::fprintf(stderr, "cannot read the dictionary or the cases\n");
        return 2;
    }
    const i18n::Translator translate(dictionary, argv[3][0] == '1');
    int failed = 0, checked = 0;
    for (const auto& [text, expected] : cases.members) {
        ++checked;
        const auto got = translate.Translate(text);
        const bool same = expected.IsString() ? got && *got == expected.text : !got;
        if (same) continue;
        if (++failed <= 25)
            std::printf("FAIL %s\n  expected: %s\n  got:      %s\n", text.c_str(), expected.IsString() ? expected.text.c_str() : "(none)", got ? got->c_str() : "(none)");
    }
    std::printf("%d of %d texts translated as the app does\n", checked - failed, checked);
    return failed ? 1 : 0;
}
