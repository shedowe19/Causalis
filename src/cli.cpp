#include "causalis/engine.hpp"
#include "causalis/page.hpp"
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) { ++i; continue; }
        const std::size_t count = first >= 0xC2 && first <= 0xDF ? 2 :
            first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
        if (!count || count > text.size() - i) return false;
        for (std::size_t j = 1; j < count; ++j)
            if ((static_cast<unsigned char>(text[i+j]) & 0xC0) != 0x80) return false;
        const auto second = static_cast<unsigned char>(text[i+1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) return false;
        i += count;
    }
    return true;
}
std::string escape(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                result += "\\u00";
                result += digits[c >> 4]; result += digits[c & 15];
            } else result += static_cast<char>(c);
        }
    }
    return result;
}
void emit_string(std::string_view value) { std::cout << '"' << escape(value) << '"'; }
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Causalis Engine 0.2.0 — experimental renderer\nUsage: causalis-cli FILE.html [WIDTH] [--reading] [--run-scripts] [--source]\n";
            return 2;
        }
        float width = 960;
        bool has_width=false, include_source=false;
        causalis::PageOptions options;
        for (int index=2;index<argc;++index) {
            std::string_view value(argv[index]);
            if (value=="--reading") {options.reading_view=true;continue;}
            if (value=="--run-scripts") {options.run_scripts=true;continue;}
            if (value=="--source") {include_source=true;continue;}
            if (has_width) throw std::runtime_error("Unknown option or duplicate WIDTH.");
            auto result = std::from_chars(value.data(), value.data() + value.size(), width);
            if (result.ec != std::errc{} || result.ptr != value.data()+value.size() ||
                !std::isfinite(width) || width < 160 || width > 4096) {
                throw std::runtime_error("WIDTH must be a number from 160 to 4096.");
            }
            has_width=true;
        }
        std::filesystem::path input(argv[1]);
        std::ifstream stream(input, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open input file.");
        std::string html;
        constexpr std::size_t limit = 2 * 1024 * 1024;
        char buffer[8192];
        while (stream.read(buffer, sizeof(buffer)) || stream.gcount()) {
            if (html.size() + static_cast<std::size_t>(stream.gcount()) > limit)
                throw std::runtime_error("Input exceeds the 2 MiB prototype limit.");
            html.append(buffer, static_cast<std::size_t>(stream.gcount()));
        }
        if (stream.bad()) throw std::runtime_error("Input read failed.");
        if (html.starts_with("\xEF\xBB\xBF")) html.erase(0,3);
        if (!valid_utf8(html)) throw std::runtime_error("Input must be valid UTF-8.");
        auto page = causalis::render_page(std::move(html),width,options);
        const auto& frame = page.frame;
        std::cout << "{\"engine\":\"Causalis 0.2.0\",\"mode\":\"offline-experimental\",\"title\":";
        emit_string(frame.title);
        std::cout << ",\"width\":" << frame.width << ",\"height\":" << frame.height << ",\"commands\":[";
        bool first=true;
        for (const auto& command : frame.display_list) {
            if (!first) std::cout << ',';
            first=false;
            const auto& b = command.bounds;
            std::cout << "{\"kind\":\"" << (command.kind == causalis::PaintKind::Text ? "text" : "rect")
                << "\",\"x\":" << b.x << ",\"y\":" << b.y << ",\"width\":" << b.width
                << ",\"height\":" << b.height << ",\"color\":[" << static_cast<int>(command.color.r)
                << ',' << static_cast<int>(command.color.g) << ',' << static_cast<int>(command.color.b)
                << "],\"font_size\":" << command.font_size << ",\"bold\":" << (command.bold?"true":"false")
                << ",\"source_id\":" << command.source_id << ",\"text\":";
            emit_string(command.text); std::cout << ",\"link\":"; emit_string(command.link); std::cout << '}';
        }
        std::cout << "],\"diagnostics\":[";
        first=true;
        for (const auto& diagnostic : frame.diagnostics) {
            if (!first) std::cout << ',';
            first=false;
            std::cout << "{\"category\":"; emit_string(diagnostic.category);
            std::cout << ",\"message\":"; emit_string(diagnostic.message); std::cout << '}';
        }
        std::cout << "],\"script_console\":[";
        first=true;
        for (const auto& line:page.console) {
            if (!first) std::cout << ',';
            first=false;emit_string(line);
        }
        std::cout << "],\"script_diagnostics\":[";
        first=true;
        for (const auto& line:page.script_diagnostics) {
            if (!first) std::cout << ',';
            first=false;emit_string(line);
        }
        std::cout << ']';
        if (include_source) {std::cout << ",\"document_html\":";emit_string(page.document_html);}
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "causalis-cli: " << e.what() << '\n';
        return 1;
    }
}
