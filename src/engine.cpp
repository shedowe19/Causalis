#include "causalis/engine.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace causalis {
namespace {

constexpr std::size_t kMaxInput = 2U * 1024U * 1024U;
constexpr std::size_t kMaxNodes = 50000;
constexpr std::size_t kMaxDepth = 128;
constexpr std::size_t kMaxRules = 4096;
constexpr std::size_t kMaxCommands = 100000;
constexpr float kMaxDimension = 1000000.0F;

bool ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && ascii_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && ascii_space(s.back())) s.remove_suffix(1);
    return s;
}
char lower_char(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_char(c);
    return out;
}
bool starts_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower_char(s[i]) != lower_char(prefix[i])) return false;
    return true;
}
bool name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':';
}
std::size_t utf8_length(std::string_view s, std::size_t at) {
    unsigned char c = static_cast<unsigned char>(s[at]);
    std::size_t n = c < 0x80 ? 1 : (c >= 0xC2 && c < 0xE0 ? 2 :
        (c < 0xF0 && c >= 0xE0 ? 3 : (c >= 0xF0 && c < 0xF5 ? 4 : 1)));
    if (at + n > s.size()) return 1;
    for (std::size_t i = 1; i < n; ++i)
        if ((static_cast<unsigned char>(s[at + i]) & 0xC0U) != 0x80U) return 1;
    return n;
}
void append_codepoint(std::string& out, std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) cp = 0xFFFDU;
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}
std::string entities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') { out.push_back(s[i++]); continue; }
        // Search only the supported entity window. Searching the entire tail
        // for every '&' makes an ampersand-only document quadratic.
        auto tail = s.substr(i + 1, std::min<std::size_t>(32, s.size() - i - 1));
        auto relative_end = tail.find(';');
        if (relative_end == std::string_view::npos) { out.push_back(s[i++]); continue; }
        auto end = i + 1 + relative_end;
        auto e = s.substr(i + 1, end - i - 1);
        if (e == "amp") out += '&';
        else if (e == "lt") out += '<';
        else if (e == "gt") out += '>';
        else if (e == "quot") out += '"';
        else if (e == "apos" || e == "#39") out += '\'';
        else if (e == "nbsp") append_codepoint(out, 0xA0);
        else if (e == "mdash") append_codepoint(out, 0x2014);
        else if (e == "ndash") append_codepoint(out, 0x2013);
        else if (e == "hellip") append_codepoint(out, 0x2026);
        else if (e == "copy") append_codepoint(out, 0xA9);
        else if (!e.empty() && e.front() == '#') {
            e.remove_prefix(1);
            int base = 10;
            if (!e.empty() && (e.front() == 'x' || e.front() == 'X')) { base = 16; e.remove_prefix(1); }
            std::uint32_t cp{};
            auto [ptr, ec] = std::from_chars(e.data(), e.data() + e.size(), cp, base);
            if (e.empty() || ec != std::errc{} || ptr != e.data() + e.size()) {
                out.append(s.substr(i, end - i + 1));
            } else append_codepoint(out, cp);
        } else out.append(s.substr(i, end - i + 1));
        i = end + 1;
    }
    return out;
}

struct Attr { std::string name, value; };
struct Node {
    std::string tag, text;
    std::vector<Attr> attrs;
    std::vector<std::size_t> children;
    unsigned id{};
    std::string_view attr(std::string_view key) const {
        for (const auto& a : attrs) if (a.name == key) return a.value;
        return {};
    }
    bool has_attr(std::string_view key) const {
        for (const auto& a : attrs) if (a.name == key) return true;
        return false;
    }
};

class Engine {
public:
    Frame frame;
    std::vector<Node> nodes;
    MeasureText measure;
    bool commands_limited{};
    explicit Engine(float width, MeasureText callback) : measure(std::move(callback)) {
        frame.width = std::isfinite(width) ? std::clamp(width, 1.0F, 32768.0F) : 1024.0F;
        nodes.push_back(Node{"#document", {}, {}, {}, 0});
    }
    void diag(std::string category, std::string message) {
        for (const auto& d : frame.diagnostics)
            if (d.category == category && d.message == message) return;
        if (frame.diagnostics.size() < 100) frame.diagnostics.push_back({std::move(category), std::move(message)});
    }
    std::size_t add_node(Node node, std::size_t parent) {
        node.id = static_cast<unsigned>(nodes.size());
        auto index = nodes.size();
        nodes.push_back(std::move(node));
        nodes[parent].children.push_back(index);
        return index;
    }
    void parse(std::string_view html) {
        if (html.size() > kMaxInput) {
            html = html.substr(0, kMaxInput);
            diag("limit", "HTML input was truncated at 2 MiB.");
        }
        std::vector<std::size_t> stack{0};
        std::size_t pos{};
        while (pos < html.size() && nodes.size() < kMaxNodes) {
            if (html[pos] != '<') {
                auto end = html.find('<', pos);
                if (end == std::string_view::npos) end = html.size();
                add_node(Node{"#text", entities(html.substr(pos, end - pos)), {}, {}, 0}, stack.back());
                pos = end;
                continue;
            }
            if (html.substr(pos, 4) == "<!--") {
                auto end = html.find("-->", pos + 4);
                pos = end == std::string_view::npos ? html.size() : end + 3;
                continue;
            }
            if (pos + 1 >= html.size()) {
                add_node(Node{"#text", "<", {}, {}, 0}, stack.back()); ++pos; continue;
            }
            if (html[pos + 1] == '!' || html[pos + 1] == '?') {
                auto end = html.find('>', pos + 2);
                pos = end == std::string_view::npos ? html.size() : end + 1;
                continue;
            }
            std::size_t cur = pos + 1;
            bool closing = html[cur] == '/';
            if (closing) ++cur;
            auto name_start = cur;
            while (cur < html.size() && name_char(html[cur])) ++cur;
            if (cur == name_start) {
                add_node(Node{"#text", "<", {}, {}, 0}, stack.back()); ++pos; continue;
            }
            auto tag = lower(html.substr(name_start, cur - name_start));
            if (closing) {
                auto end = html.find('>', cur);
                pos = end == std::string_view::npos ? html.size() : end + 1;
                for (std::size_t i = stack.size(); i-- > 1;) {
                    if (nodes[stack[i]].tag == tag) { stack.resize(i); break; }
                }
                continue;
            }
            std::vector<Attr> attrs;
            bool self_closing{};
            while (cur < html.size()) {
                while (cur < html.size() && ascii_space(html[cur])) ++cur;
                if (cur == html.size() || html[cur] == '>') break;
                if (html[cur] == '/' && cur + 1 < html.size() && html[cur + 1] == '>') {
                    self_closing = true; ++cur; break;
                }
                auto at = cur;
                while (cur < html.size() && name_char(html[cur])) ++cur;
                if (at == cur) { ++cur; continue; }
                std::string key = lower(html.substr(at, cur - at));
                while (cur < html.size() && ascii_space(html[cur])) ++cur;
                std::string value;
                if (cur < html.size() && html[cur] == '=') {
                    ++cur;
                    while (cur < html.size() && ascii_space(html[cur])) ++cur;
                    if (cur < html.size() && (html[cur] == '\'' || html[cur] == '"')) {
                        char quote = html[cur++];
                        auto start = cur;
                        while (cur < html.size() && html[cur] != quote) ++cur;
                        value = entities(html.substr(start, cur - start));
                        if (cur < html.size()) ++cur;
                    } else {
                        auto start = cur;
                        while (cur < html.size() && !ascii_space(html[cur]) && html[cur] != '>') ++cur;
                        value = entities(html.substr(start, cur - start));
                    }
                }
                // HTML keeps the first occurrence of a duplicate attribute.
                if (value.size() > 4096) {
                    std::size_t cut = 4096;
                    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0U) == 0x80U) --cut;
                    value.resize(cut);
                    diag("limit", "HTML attribute values beyond 4096 bytes were truncated.");
                }
                if (attrs.size() < 256 && std::none_of(attrs.begin(), attrs.end(), [&](const Attr& a) { return a.name == key; }))
                    attrs.push_back({std::move(key), std::move(value)});
            }
            pos = cur < html.size() ? cur + 1 : html.size();
            // A small implied-close subset keeps common hand-written HTML useful.
            if ((tag == "p" || tag == "li") && nodes[stack.back()].tag == tag) stack.pop_back();
            auto index = add_node(Node{tag, {}, std::move(attrs), {}, 0}, stack.back());
            if (tag == "script" || tag == "style" || tag == "title") {
                auto content_start = pos;
                const std::string closing_prefix = "</" + tag;
                while (pos < html.size()) {
                    auto after = pos + closing_prefix.size();
                    if (starts_ci(html.substr(pos), closing_prefix) && after < html.size() &&
                        (ascii_space(html[after]) || html[after] == '>' || html[after] == '/')) break;
                    ++pos;
                }
                nodes[index].text = tag == "title" ? entities(html.substr(content_start, pos - content_start)) :
                    std::string(html.substr(content_start, pos - content_start));
                if (tag == "title" && frame.title.empty()) frame.title = std::string(trim(nodes[index].text));
                if (tag == "script") diag("unsupported", "The renderer does not execute scripts or event handlers; the host controls explicit local subset execution.");
                if (pos < html.size()) {
                    auto end = html.find('>', pos);
                    pos = end == std::string_view::npos ? html.size() : end + 1;
                }
                continue;
            }
            static constexpr std::array<std::string_view, 14> voids = {
                "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"};
            for (const auto& a : nodes[index].attrs) {
                if (a.name.size() > 2 && a.name.substr(0, 2) == "on")
                    diag("unsupported", "The renderer does not execute scripts or event handlers; the host controls explicit local subset execution.");
            }
            if (tag == "img") diag("unsupported", "Images are represented by alt-text placeholders; image decoding is not implemented.");
            if ((tag == "link" && !nodes[index].attr("href").empty()) || tag == "iframe" || tag == "video" || tag == "audio")
                diag("unsupported", "External stylesheets, frames and media resources are not loaded.");
            if (tag == "input" || tag == "button" || tag == "select" || tag == "textarea")
                diag("unsupported", "Form elements are static placeholders; input and submission are not implemented.");
            if (!self_closing && std::find(voids.begin(), voids.end(), tag) == voids.end()) {
                if (stack.size() < kMaxDepth) stack.push_back(index);
                else diag("limit", "HTML nesting beyond 128 levels was flattened.");
            }
        }
        if (nodes.size() >= kMaxNodes) diag("limit", "HTML node limit reached; remaining input was not parsed.");
    }
    float text_width(std::string_view text, float size, bool bold) {
        if (measure) {
            try {
                float value = measure(text, size, bold);
                if (std::isfinite(value) && value >= 0 && value <= kMaxDimension) return value;
            } catch (...) {}
            diag("metrics", "Invalid host text measurements were replaced with approximate metrics.");
        }
        float width{};
        for (std::size_t i = 0; i < text.size();) {
            auto n = utf8_length(text, i);
            auto c = static_cast<unsigned char>(text[i]);
            float factor = c == ' ' ? 0.32F : (c < 128 && std::string_view("il.,:;!'|").find(static_cast<char>(c)) != std::string_view::npos ? 0.30F : 0.56F);
            // CJK and emoji are approximately full-width in the fallback font.
            if (n >= 3 && c >= 0xE3) factor = 1.0F;
            width += size * factor * (bold ? 1.04F : 1.0F);
            i += n;
        }
        return std::min(width, kMaxDimension);
    }
    std::size_t paint(PaintCommand command) {
        if (frame.display_list.size() >= kMaxCommands) {
            if (!commands_limited) { commands_limited = true; diag("limit", "Display list limit reached; remaining content was not painted."); }
            return std::numeric_limits<std::size_t>::max();
        }
        frame.display_list.push_back(std::move(command));
        return frame.display_list.size() - 1;
    }
};

enum class Unit { Auto, Px, Percent, Em, Rem };
struct Length { float value{}; Unit unit{Unit::Px}; };
std::optional<float> number(std::string_view s) {
    s = trim(s);
    if (s.empty()) return {};
    float result{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
    if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(result)) return {};
    // Out-of-budget author values are ignored rather than moving all useful
    // content tens of thousands of pixels outside the viewport.
    if (std::abs(result) > 10000.0F) return {};
    return result;
}
std::optional<Length> length(std::string_view s) {
    s = trim(s);
    if (s == "auto") return Length{0, Unit::Auto};
    Unit unit = Unit::Px;
    if (s.size() > 3 && s.substr(s.size() - 3) == "rem") { unit = Unit::Rem; s.remove_suffix(3); }
    else if (s.size() > 2 && s.substr(s.size() - 2) == "px") s.remove_suffix(2);
    else if (s.size() > 2 && s.substr(s.size() - 2) == "em") { unit = Unit::Em; s.remove_suffix(2); }
    else if (!s.empty() && s.back() == '%') { unit = Unit::Percent; s.remove_suffix(1); }
    auto value = number(s);
    if (!value) return {};
    return Length{*value, unit};
}
float resolve(Length value, float basis, float font) {
    float result = value.value;
    if (value.unit == Unit::Percent) result *= basis / 100.0F;
    else if (value.unit == Unit::Em) result *= font;
    else if (value.unit == Unit::Rem) result *= 16.0F;
    else if (value.unit == Unit::Auto) result = 0;
    return std::clamp(result, -kMaxDimension, kMaxDimension);
}
std::optional<Color> parse_color(std::string_view s) {
    s = trim(s);
    if (s.size() == 4 && s.front() == '#') {
        unsigned value{};
        auto [ptr, ec] = std::from_chars(s.data() + 1, s.data() + 4, value, 16);
        if (ec == std::errc{} && ptr == s.data() + 4)
            return Color{static_cast<unsigned char>(((value >> 8) & 15U) * 17U),
                         static_cast<unsigned char>(((value >> 4) & 15U) * 17U),
                         static_cast<unsigned char>((value & 15U) * 17U)};
    }
    if (s.size() == 7 && s.front() == '#') {
        unsigned value{};
        auto [ptr, ec] = std::from_chars(s.data() + 1, s.data() + 7, value, 16);
        if (ec == std::errc{} && ptr == s.data() + 7)
            return Color{static_cast<unsigned char>(value >> 16), static_cast<unsigned char>((value >> 8) & 255U), static_cast<unsigned char>(value & 255U)};
    }
    if (starts_ci(s, "rgb(") && s.back() == ')') {
        auto values = s.substr(4, s.size() - 5);
        std::array<unsigned char, 3> rgb{};
        for (std::size_t i = 0; i < 3; ++i) {
            auto comma = values.find(',');
            if (i < 2 && comma == std::string_view::npos) return {};
            auto part = trim(values.substr(0, comma));
            bool percent = !part.empty() && part.back() == '%';
            if (percent) part.remove_suffix(1);
            auto n = number(part);
            if (!n) return {};
            rgb[i] = static_cast<unsigned char>(std::clamp(*n * (percent ? 2.55F : 1.0F), 0.0F, 255.0F));
            if (comma != std::string_view::npos) values.remove_prefix(comma + 1);
            else values = {};
        }
        if (!trim(values).empty()) return {};
        return Color{rgb[0], rgb[1], rgb[2]};
    }
    static constexpr std::array<std::pair<std::string_view, Color>, 20> names{{
        {"black", {0,0,0}}, {"white", {255,255,255}}, {"red", {255,0,0}},
        {"green", {0,128,0}}, {"blue", {0,0,255}}, {"gray", {128,128,128}},
        {"grey", {128,128,128}}, {"silver", {192,192,192}}, {"yellow", {255,255,0}},
        {"orange", {255,165,0}}, {"purple", {128,0,128}}, {"navy", {0,0,128}},
        {"teal", {0,128,128}}, {"aqua", {0,255,255}}, {"fuchsia", {255,0,255}},
        {"maroon", {128,0,0}}, {"olive", {128,128,0}}, {"lime", {0,255,0}},
        {"rebeccapurple", {102,51,153}}, {"transparent", {0,0,0}}
    }};
    auto name = lower(s);
    for (const auto& [key, color] : names) if (key == name) return color;
    return {};
}

struct Declaration { std::string property, value; bool important{}; };
std::vector<Declaration> declarations(std::string_view css, Engine* engine = nullptr) {
    std::vector<Declaration> out;
    while (!css.empty() && out.size() < 256) {
        std::size_t end{};
        char quote{};
        unsigned parens{};
        for (; end < css.size(); ++end) {
            char c = css[end];
            if (quote) { if (c == quote && (end == 0 || css[end - 1] != '\\')) quote = 0; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '(') ++parens;
            else if (c == ')' && parens) --parens;
            else if (c == ';' && !parens) break;
        }
        auto part = trim(css.substr(0, end));
        auto colon = part.find(':');
        if (colon != std::string_view::npos) {
            if (colon > 128 || part.size() - colon - 1 > 4096) {
                if (engine) engine->diag("limit", "Oversized CSS property names or values were ignored.");
                if (end == css.size()) break;
                css.remove_prefix(end + 1);
                continue;
            }
            auto key = lower(trim(part.substr(0, colon)));
            auto value = lower(trim(part.substr(colon + 1)));
            bool important{};
            auto imp = value.rfind("!important");
            if (imp != std::string::npos && trim(std::string_view(value).substr(imp)) == "!important") {
                value = std::string(trim(std::string_view(value).substr(0, imp))); important = true;
            }
            out.push_back({std::move(key), std::move(value), important});
        }
        if (end == css.size()) break;
        css.remove_prefix(end + 1);
    }
    return out;
}

struct Selector {
    std::string tag;
    std::vector<std::string> ids, classes;
};
std::optional<Selector> selector(std::string_view s) {
    s = trim(s);
    if (s.empty() || s.size() > 4096) return {};
    Selector out;
    std::size_t pos{};
    if (s.front() == '*') ++pos;
    else if (s.front() != '.' && s.front() != '#') {
        auto start = pos;
        while (pos < s.size() && name_char(s[pos]) && s[pos] != ':') ++pos;
        if (pos == start) return {};
        out.tag = lower(s.substr(start, pos - start));
    }
    while (pos < s.size()) {
        char prefix = s[pos++];
        if (prefix != '.' && prefix != '#') return {};
        auto start = pos;
        while (pos < s.size() && name_char(s[pos]) && s[pos] != ':') ++pos;
        if (start == pos) return {};
        if (out.classes.size() + out.ids.size() >= 128) return {};
        if (prefix == '.') out.classes.emplace_back(s.substr(start, pos - start));
        else out.ids.emplace_back(s.substr(start, pos - start));
    }
    return out;
}
bool matches(const Selector& s, const Node& n, std::string_view node_id,
             std::string_view node_classes, std::size_t& byte_budget) {
    // Charge conservative byte work before comparing/scanning. Counting only
    // selectors permits a repeated compound class selector to rescan a large
    // class attribute hundreds of times per match.
    auto charge = [&](std::size_t bytes) {
        if (bytes > byte_budget) { byte_budget = 0; return false; }
        byte_budget -= bytes;
        return true;
    };
    if (!charge(1)) return false;
    if (!s.tag.empty()) {
        if (!charge(s.tag.size() + n.tag.size() + 1)) return false;
        if (s.tag != n.tag) return false;
    }
    for (const auto& id : s.ids) {
        if (!charge(id.size() + node_id.size() + 1)) return false;
        if (id != node_id) return false;
    }
    for (const auto& cls : s.classes) {
        // Each node attribute is scanned once per class. The second factor
        // covers token comparisons and whitespace handling within that scan.
        if (!charge(node_classes.size() * 2 + cls.size() + 1)) return false;
        bool found{};
        auto classes = node_classes;
        while (!classes.empty()) {
            classes = trim(classes);
            auto end = classes.find_first_of(" \t\r\n\f");
            auto word = classes.substr(0, end);
            if (word == cls) { found = true; break; }
            if (end == std::string_view::npos) break;
            classes.remove_prefix(end + 1);
        }
        if (!found) return false;
    }
    return true;
}
struct Rule { Selector selector; std::shared_ptr<const std::vector<Declaration>> values; unsigned order{}; };
enum class Display { Inline, Block, None };
struct Style {
    Display display{Display::Inline};
    Color color{28, 30, 36};
    std::optional<Color> background;
    float font{16};
    bool bold{}, pre{};
    std::array<Length, 4> margin{}, padding{}; // top, right, bottom, left
    Length width{0, Unit::Auto}, max_width{0, Unit::Auto}, height{0, Unit::Auto};
};
bool block_tag(std::string_view tag) {
    static constexpr std::array<std::string_view, 28> blocks = {
        "#document", "html", "body", "div", "main", "section", "article", "header", "footer",
        "nav", "aside", "p", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
        "pre", "blockquote", "form", "hr", "table", "tr", "figure"};
    return std::find(blocks.begin(), blocks.end(), tag) != blocks.end();
}
bool hidden_tag(std::string_view tag) {
    return tag == "head" || tag == "style" || tag == "script" || tag == "title" || tag == "meta" ||
        tag == "link" || tag == "base" || tag == "template" || tag == "iframe" || tag == "video" || tag == "audio";
}
void edges(std::array<Length, 4>& target, std::string_view value) {
    std::array<Length, 4> vals{};
    std::size_t count{};
    while (!(value = trim(value)).empty() && count < 4) {
        auto end = value.find_first_of(" \t\r\n");
        auto parsed = length(value.substr(0, end));
        if (!parsed) return;
        vals[count++] = *parsed;
        if (end == std::string_view::npos) { value = {}; break; }
        value.remove_prefix(end + 1);
    }
    if (!trim(value).empty() || !count) return;
    target[0] = vals[0]; target[1] = count > 1 ? vals[1] : vals[0];
    target[2] = count > 2 ? vals[2] : vals[0]; target[3] = count > 3 ? vals[3] : target[1];
}
void apply(Engine& engine, Style& style, const Declaration& d, float inherited_font) {
    const auto& p = d.property; const auto& v = d.value;
    if (p == "color") { if (auto c = parse_color(v)) style.color = *c; }
    else if (p == "background" || p == "background-color") {
        if (v == "transparent" || v == "none") style.background.reset();
        else if (auto c = parse_color(v)) style.background = *c;
        else engine.diag("css", "Complex backgrounds and background images are not implemented.");
    } else if (p == "font-size") {
        if (auto l = length(v)) style.font = std::clamp(resolve(*l, inherited_font, inherited_font), 4.0F, 256.0F);
    } else if (p == "font-weight") {
        if (v == "bold" || v == "bolder") style.bold = true;
        else if (v == "normal" || v == "lighter") style.bold = false;
        else if (auto n = number(v)) style.bold = *n >= 600;
    } else if (p == "display") {
        if (v == "none") style.display = Display::None;
        else if (v == "block") style.display = Display::Block;
        else if (v == "inline" || v == "inline-block") {
            style.display = Display::Inline;
            if (v == "inline-block") engine.diag("css", "inline-block is approximated as inline content.");
        } else engine.diag("css", "Flex, grid and other display modes are not implemented; normal flow is used.");
    } else if (p == "width") { if (auto l = length(v)) style.width = *l; }
    else if (p == "max-width") { if (auto l = length(v)) style.max_width = *l; }
    else if (p == "height") { if (auto l = length(v)) style.height = *l; }
    else if (p == "margin") edges(style.margin, v);
    else if (p == "padding") edges(style.padding, v);
    else if (p == "white-space") {
        if (v == "pre" || v == "pre-wrap") style.pre = true;
        else if (v == "normal") style.pre = false;
        else engine.diag("css", "Only normal, pre and pre-wrap whitespace modes are implemented.");
    } else {
        static constexpr std::array<std::string_view, 4> sides = {"top", "right", "bottom", "left"};
        bool handled{};
        for (std::size_t i = 0; i < 4; ++i) {
            if (p == "margin-" + std::string(sides[i])) { if (auto l = length(v)) style.margin[i] = *l; handled = true; }
            if (p == "padding-" + std::string(sides[i])) { if (auto l = length(v)) style.padding[i] = *l; handled = true; }
        }
        if (!handled) {
            // These declarations are safely inert; explicitly surface their absence.
            engine.diag("css", "Unsupported CSS property: " + p + ".");
        }
    }
}
std::vector<Rule> collect_rules(Engine& engine) {
    std::vector<Rule> out;
    std::vector<bool> inert(engine.nodes.size());
    for (std::size_t i = 0; i < engine.nodes.size(); ++i)
        for (auto child : engine.nodes[i].children)
            inert[child] = inert[i] || engine.nodes[i].tag == "template";
    std::size_t css_bytes_budget = 512U * 1024U;
    for (std::size_t node_index = 0; node_index < engine.nodes.size(); ++node_index) {
        const auto& node = engine.nodes[node_index];
        if (inert[node_index]) continue;
        if (node.tag != "style") continue;
        std::string clean;
        std::string_view css = node.text;
        if (css.size() > css_bytes_budget) {
            css = css.substr(0, css_bytes_budget);
            engine.diag("limit", "Stylesheet text budget of 512 KiB reached; remaining CSS was ignored.");
        }
        css_bytes_budget -= css.size();
        for (std::size_t i = 0; i < css.size();) {
            if (css.substr(i, 2) == "/*") {
                auto end = css.find("*/", i + 2);
                i = end == std::string_view::npos ? css.size() : end + 2;
                clean.push_back(' ');
            } else clean.push_back(css[i++]);
        }
        css = clean;
        while (!css.empty() && out.size() < kMaxRules) {
            auto open = css.find('{');
            if (open == std::string_view::npos) break;
            auto close = css.find('}', open + 1);
            if (close == std::string_view::npos) { engine.diag("css", "Incomplete CSS rule was ignored."); break; }
            auto selectors = trim(css.substr(0, open));
            auto values = std::make_shared<const std::vector<Declaration>>(declarations(css.substr(open + 1, close - open - 1), &engine));
            if (selectors.starts_with('@')) {
                engine.diag("css", "CSS at-rules, including media queries and imports, are not implemented.");
                // Skip nested braces so rules in a false media condition cannot leak.
                unsigned depth = 1;
                std::size_t end = open + 1;
                for (; end < css.size() && depth; ++end) {
                    if (css[end] == '{') ++depth;
                    if (css[end] == '}') --depth;
                }
                css.remove_prefix(end);
                continue;
            }
            while (!selectors.empty() && out.size() < kMaxRules) {
                auto comma = selectors.find(',');
                auto parsed = selector(selectors.substr(0, comma));
                if (parsed) out.push_back({std::move(*parsed), values, static_cast<unsigned>(out.size())});
                else engine.diag("css", "Only simple tag, class, ID and compound selectors are implemented.");
                if (comma == std::string_view::npos) break;
                selectors.remove_prefix(comma + 1);
            }
            css.remove_prefix(close + 1);
        }
    }
    if (out.size() >= kMaxRules) engine.diag("limit", "CSS rule limit reached; remaining rules were ignored.");
    return out;
}

std::vector<Style> compute_styles(Engine& engine, const std::vector<Rule>& rules) {
    std::vector<Style> styles(engine.nodes.size());
    // Tree nodes are allocated parent-first, which keeps this traversal bounded.
    std::vector<std::size_t> parents(engine.nodes.size());
    for (std::size_t i = 0; i < engine.nodes.size(); ++i)
        for (auto child : engine.nodes[i].children) parents[child] = i;
    std::size_t selector_budget = 2000000;
    std::size_t selector_byte_budget = 8U * 1024U * 1024U;
    std::size_t declaration_budget = 131072;
    for (std::size_t i = 0; i < engine.nodes.size(); ++i) {
        auto& s = styles[i]; const auto& n = engine.nodes[i];
        float inherited_font = i ? styles[parents[i]].font : 16.0F;
        if (i) {
            const auto& parent = styles[parents[i]];
            s.color = parent.color; s.font = parent.font; s.bold = parent.bold; s.pre = parent.pre;
        }
        if (block_tag(n.tag)) s.display = Display::Block;
        if (hidden_tag(n.tag) || n.has_attr("hidden")) s.display = Display::None;
        if (n.tag == "body") s.margin.fill({8, Unit::Px});
        if (n.tag == "p" || n.tag == "ul" || n.tag == "ol" || n.tag == "blockquote" || n.tag == "pre" || n.tag == "figure")
            s.margin = {{{16, Unit::Px}, {}, {16, Unit::Px}, {}}};
        if (n.tag == "ul" || n.tag == "ol") s.padding[3] = {24, Unit::Px};
        if (n.tag == "blockquote") s.margin[3] = s.margin[1] = {24, Unit::Px};
        if (n.tag.size() == 2 && n.tag.front() == 'h' && n.tag.back() >= '1' && n.tag.back() <= '6') {
            static constexpr std::array<float, 6> sizes = {32, 24, 20, 18, 16, 14};
            s.font = sizes[static_cast<std::size_t>(n.tag.back() - '1')]; s.bold = true;
            s.margin = {{{s.font * 0.67F, Unit::Px}, {}, {s.font * 0.67F, Unit::Px}, {}}};
        }
        if (n.tag == "strong" || n.tag == "b") s.bold = true;
        if (n.tag == "small") s.font *= 0.85F;
        if (n.tag == "pre") s.pre = true;
        if (n.tag == "a" && n.has_attr("href")) s.color = {37, 99, 235};
        if (n.tag == "hr") { s.height = {1, Unit::Px}; s.background = Color{195, 201, 209}; s.margin[0] = s.margin[2] = {12, Unit::Px}; }
        struct Candidate {
            const Declaration* d;
            std::size_t ids, classes, tags;
            unsigned order, declaration;
            bool inline_origin;
        };
        std::vector<Candidate> candidates;
        if (n.tag != "#text") {
            const auto node_id = n.attr("id");
            const auto node_classes = n.attr("class");
            for (const auto& rule : rules) {
                if (!selector_budget || !selector_byte_budget || !declaration_budget || candidates.size() >= 2048) {
                    engine.diag("limit", "CSS selector or declaration work budget reached; remaining author styles use defaults.");
                    break;
                }
                --selector_budget;
                if (!matches(rule.selector, n, node_id, node_classes, selector_byte_budget)) {
                    if (!selector_byte_budget) {
                        engine.diag("limit", "CSS selector byte work budget of 8 MiB reached; remaining author styles use defaults.");
                        break;
                    }
                    continue;
                }
                for (std::size_t j = 0; j < rule.values->size(); ++j) {
                    if (!declaration_budget || candidates.size() >= 2048) {
                        engine.diag("limit", "CSS selector or declaration work budget reached; remaining author styles use defaults.");
                        break;
                    }
                    --declaration_budget;
                    candidates.push_back({&(*rule.values)[j], rule.selector.ids.size(), rule.selector.classes.size(),
                        rule.selector.tag.empty() ? 0U : 1U, rule.order, static_cast<unsigned>(j), false});
                }
            }
        }
        auto inline_values = declarations(n.attr("style"), &engine);
        for (std::size_t j = 0; j < inline_values.size(); ++j) {
            if (!declaration_budget) {
                engine.diag("limit", "CSS selector or declaration work budget reached; remaining author styles use defaults.");
                break;
            }
            --declaration_budget;
            candidates.push_back({&inline_values[j], 0, 0, 0, static_cast<unsigned>(rules.size()), static_cast<unsigned>(j), true});
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.d->important != b.d->important) return a.d->important < b.d->important;
            if (a.inline_origin != b.inline_origin) return a.inline_origin < b.inline_origin;
            if (a.ids != b.ids) return a.ids < b.ids;
            if (a.classes != b.classes) return a.classes < b.classes;
            if (a.tags != b.tags) return a.tags < b.tags;
            if (a.order != b.order) return a.order < b.order;
            return a.declaration < b.declaration;
        });
        for (const auto& c : candidates) apply(engine, s, *c.d, inherited_font);
        if (n.tag == "#text") s.display = Display::Inline;
    }
    return styles;
}

class Layout {
    Engine& engine;
    const std::vector<Style>& styles;
    struct Run { std::string text; Style style; std::string link; unsigned source{}; bool break_line{}, space{}; };
    struct Flow {
        float x{}, y{}, width{}, cursor{}, line_height{};
        std::vector<std::size_t> line_commands;
        bool pending_space{};
    };
public:
    Layout(Engine& e, const std::vector<Style>& s) : engine(e), styles(s) {}
    void finish_line(Flow& flow, bool force = false, float min_height = 0) {
        if (flow.cursor > 0 || !flow.line_commands.empty() || force) {
            float height = std::max({flow.line_height, min_height, 1.0F});
            // Align each text run within a common line box when fonts differ.
            for (auto index : flow.line_commands) {
                if (index < engine.frame.display_list.size()) {
                    auto& cmd = engine.frame.display_list[index];
                    cmd.bounds.y += std::max(0.0F, (height - cmd.bounds.height) * 0.5F);
                }
            }
            flow.y = std::min(kMaxDimension, flow.y + height);
        }
        flow.cursor = 0; flow.line_height = 0; flow.line_commands.clear(); flow.pending_space = false;
    }
    void collect(std::size_t index, std::vector<Run>& runs, std::string link = {}, std::optional<Color> inline_background = {}) {
        const auto& node = engine.nodes[index]; const auto& style = styles[index];
        if (style.display == Display::None || runs.size() >= kMaxCommands) return;
        if (style.display == Display::Inline && style.background) inline_background = style.background;
        Style run_style = style;
        // Approximate an inline element's painted box on each of its runs.
        // This is paint propagation, not CSS background inheritance.
        if (!run_style.background) run_style.background = inline_background;
        if (node.tag == "a" && node.has_attr("href")) link = node.attr("href");
        if (node.tag == "br") { runs.push_back({{}, run_style, link, node.id, true, false}); return; }
        std::string content;
        if (node.tag == "#text") content = node.text;
        else if (node.tag == "img") content = "[Image: " + std::string(node.attr("alt").empty() ? "no alt text" : node.attr("alt")) + "]";
        else if (node.tag == "input") content = "[Input: " + std::string(node.attr("placeholder")) + "]";
        if (!content.empty()) {
            for (std::size_t p = 0; p < content.size() && runs.size() < kMaxCommands;) {
                if (ascii_space(content[p])) {
                    if (style.pre) {
                        if (content[p] == '\n') runs.push_back({{}, run_style, link, node.id, true, false});
                        else if (content[p] != '\r') runs.push_back({content[p] == '\t' ? "    " : " ", run_style, link, node.id, false, false});
                        ++p;
                    } else {
                        while (p < content.size() && ascii_space(content[p])) ++p;
                        runs.push_back({" ", run_style, link, node.id, false, true});
                    }
                } else {
                    auto start = p;
                    while (p < content.size() && !ascii_space(content[p])) p += utf8_length(content, p);
                    runs.push_back({content.substr(start, p - start), run_style, link, node.id, false, false});
                }
            }
        }
        for (auto child : node.children) collect(child, runs, link, inline_background);
    }
    void text(Flow& flow, const Run& run) {
        if (run.break_line) { finish_line(flow, true, run.style.font * 1.35F); return; }
        if (run.space) { if (flow.cursor > 0) flow.pending_space = true; return; }
        if (run.text.empty()) return;
        float gap = flow.pending_space && flow.cursor > 0 ? engine.text_width(" ", run.style.font, run.style.bold) : 0;
        flow.pending_space = false;
        float width = engine.text_width(run.text, run.style.font, run.style.bold);
        if (flow.cursor > 0 && flow.cursor + gap + width > flow.width) { finish_line(flow); gap = 0; }
        flow.cursor += gap;
        auto paint_chunk = [&](std::string_view value, float chunk_width) {
            float h = run.style.font * 1.35F;
            Rect bounds{flow.x + flow.cursor, flow.y, chunk_width, h};
            if (run.style.background) {
                auto background = engine.paint({PaintKind::FillRect, bounds, *run.style.background, {}, run.style.font, run.style.bold, run.link, run.source});
                if (background != std::numeric_limits<std::size_t>::max()) flow.line_commands.push_back(background);
            }
            auto id = engine.paint({PaintKind::Text, bounds, run.style.color, std::string(value), run.style.font, run.style.bold, run.link, run.source});
            if (id != std::numeric_limits<std::size_t>::max()) flow.line_commands.push_back(id);
            flow.cursor += chunk_width;
            flow.line_height = std::max(flow.line_height, h);
        };
        if (width <= flow.width) { paint_chunk(run.text, width); return; }
        // Break oversized words at UTF-8 character boundaries instead of
        // slicing bytes. Even a narrower-than-one-character viewport progresses.
        std::vector<std::size_t> boundaries{0};
        for (std::size_t p = 0; p < run.text.size();) {
            p += utf8_length(run.text, p);
            boundaries.push_back(p);
        }
        std::size_t start{};
        while (start + 1 < boundaries.size() && !engine.commands_limited) {
            // Exponential probing finds a nearby fit, then binary search avoids
            // measuring every growing prefix. Each probe contains <=4096
            // Unicode characters, so even very wide lines have bounded work.
            auto limit = std::min(start + 4096, boundaries.size() - 1);
            auto measured = [&](std::size_t end) {
                return engine.text_width(std::string_view(run.text).substr(boundaries[start], boundaries[end] - boundaries[start]), run.style.font, run.style.bold);
            };
            std::size_t end = start + 1;
            float chunk_width = measured(end);
            if (flow.cursor > 0 && flow.cursor + chunk_width > flow.width) finish_line(flow);
            if (chunk_width <= flow.width - flow.cursor) {
                std::size_t low = end;
                std::size_t high = limit + 1;
                for (std::size_t step = 2; low < limit;) {
                    auto probe = std::min(start + step, limit);
                    float candidate = measured(probe);
                    if (candidate > flow.width - flow.cursor) { high = probe; break; }
                    low = probe; chunk_width = candidate;
                    if (probe == limit) break;
                    step = std::min<std::size_t>(step * 2, 4096);
                }
                while (low + 1 < high) {
                    auto mid = low + (high - low) / 2;
                    float candidate = measured(mid);
                    if (candidate <= flow.width - flow.cursor) { low = mid; chunk_width = candidate; }
                    else high = mid;
                }
                end = low;
            }
            paint_chunk(std::string_view(run.text).substr(boundaries[start], boundaries[end] - boundaries[start]), chunk_width);
            start = end;
            if (start + 1 < boundaries.size() && end < limit) finish_line(flow);
        }
    }
    void inline_nodes(const std::vector<std::size_t>& indices, Flow& flow) {
        std::vector<Run> runs;
        for (auto index : indices) collect(index, runs);
        for (const auto& run : runs) {
            if (engine.commands_limited || flow.y >= kMaxDimension) break;
            text(flow, run);
        }
    }
    float block(std::size_t index, float parent_x, float parent_y, float available) {
        const auto& node = engine.nodes[index]; const auto& style = styles[index];
        if (style.display == Display::None || engine.commands_limited) return 0;
        std::array<float, 4> margin{}, pad{};
        for (std::size_t i = 0; i < 4; ++i) {
            margin[i] = resolve(style.margin[i], available, style.font);
            pad[i] = std::max(0.0F, resolve(style.padding[i], available, style.font));
        }
        // Bound negative margins so hostile CSS cannot create non-finite geometry.
        float outer_width = std::max(1.0F, available - margin[1] - margin[3]);
        float content_width = style.width.unit == Unit::Auto ? std::max(1.0F, outer_width - pad[1] - pad[3]) :
            std::max(1.0F, resolve(style.width, available, style.font));
        if (style.max_width.unit != Unit::Auto) content_width = std::min(content_width, std::max(1.0F, resolve(style.max_width, available, style.font)));
        content_width = std::clamp(content_width, 1.0F, 32768.0F);
        float box_width = content_width + pad[1] + pad[3];
        bool auto_left = style.margin[3].unit == Unit::Auto;
        bool auto_right = style.margin[1].unit == Unit::Auto;
        float free = std::max(0.0F, available - box_width - (auto_left ? 0 : margin[3]) - (auto_right ? 0 : margin[1]));
        if (auto_left) margin[3] = auto_right ? free * 0.5F : free;
        if (auto_right) margin[1] = auto_left ? free * 0.5F : free;
        float box_x = std::clamp(parent_x + margin[3], -kMaxDimension, kMaxDimension);
        float box_y = std::clamp(parent_y + margin[0], -kMaxDimension, kMaxDimension);
        std::size_t background = std::numeric_limits<std::size_t>::max();
        if (style.background) background = engine.paint({PaintKind::FillRect, {box_x, box_y, box_width, 0}, *style.background, {}, style.font, style.bold, {}, node.id});
        Flow flow{box_x + pad[3], box_y + pad[0], content_width, 0, 0, {}, false};
        std::vector<std::size_t> pending;
        if (node.tag == "li") {
            Run marker{"\xE2\x80\xA2", style, {}, node.id, false, false};
            text(flow, marker); flow.pending_space = true;
        }
        for (auto child : node.children) {
            if (engine.commands_limited || flow.y >= kMaxDimension) break;
            if (styles[child].display == Display::None) continue;
            if (styles[child].display == Display::Block) {
                if (!pending.empty()) { inline_nodes(pending, flow); pending.clear(); }
                finish_line(flow);
                flow.y = std::min(kMaxDimension, flow.y + block(child, flow.x, flow.y, content_width));
            } else pending.push_back(child);
        }
        if (!pending.empty()) inline_nodes(pending, flow);
        finish_line(flow);
        float content_height = std::max(0.0F, flow.y - box_y - pad[0]);
        if (style.height.unit != Unit::Auto) {
            // Fixed-height boxes keep normal-flow children visible (CSS overflow:visible).
            content_height = std::max(0.0F, resolve(style.height, 0, style.font));
        }
        float box_height = std::min(kMaxDimension, content_height + pad[0] + pad[2]);
        if (background < engine.frame.display_list.size()) engine.frame.display_list[background].bounds.height = box_height;
        return std::clamp(margin[0] + box_height + margin[2], -kMaxDimension, kMaxDimension);
    }
};

} // namespace

Frame render(std::string_view html, float viewport_width, MeasureText measure) {
    Engine engine(viewport_width, std::move(measure));
    engine.parse(html);
    auto rules = collect_rules(engine);
    auto styles = compute_styles(engine, rules);
    Layout layout(engine, styles);
    engine.frame.height = std::max(1.0F, layout.block(0, 0, 0, engine.frame.width));
    for (const auto& command : engine.frame.display_list)
        engine.frame.height = std::max(engine.frame.height, std::min(kMaxDimension, command.bounds.y + command.bounds.height));
    if (engine.frame.title.empty()) engine.frame.title = "Untitled";
    engine.diag("engine", "Experimental renderer: a subset of HTML and normal-flow CSS; no resource fetching, images or web platform compatibility guarantee.");
    return std::move(engine.frame);
}

} // namespace causalis
