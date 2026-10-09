#include "causalis/engine.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::uint32_t state = 0xCA05A115U;
std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
void check(const causalis::Frame& frame) {
    if (!std::isfinite(frame.width) || !std::isfinite(frame.height))
        throw std::runtime_error("Non-finite frame geometry");
    for (const auto& command : frame.display_list) {
        const auto& box = command.bounds;
        if (!std::isfinite(box.x) || !std::isfinite(box.y) ||
            !std::isfinite(box.width) || !std::isfinite(box.height) ||
            box.width < 0 || box.height < 0)
            throw std::runtime_error("Invalid paint geometry");
    }
}
}

int main() {
    try {
        const std::string tokens[] = {"<div>", "</div>", "<p>", "</p>",
            "<script>", "</scriptx>", "</script>", "<style>", "</style>",
            "<template>", "</template>", "<a href='&amp;'>", "</a>",
            "<!--", "-->", "<", ">", "&", "&#xD800;", "alpha βeta ",
            "#item{color:#123456;}", "<span style='margin:1e30px;'>", "</span>",
            "<input type=password>", "<img src=https://example.invalid>", "\"'"};
        for (int sample = 0; sample < 2000; ++sample) {
            std::string html;
            const unsigned count = 1 + next() % 180;
            for (unsigned i = 0; i < count; ++i) html += tokens[next() % std::size(tokens)];
            check(causalis::render(html, static_cast<float>(1 + next() % 1800)));
        }
        check(causalis::render(std::string(250000, '&'), 960));
        check(causalis::render("<p style='font-size:4px;'>" + std::string(250000, 'x') + "</p>", 32768));
        std::cout << "2000 deterministic malformed documents and 2 long-input cases passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
