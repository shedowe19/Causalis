#include "causalis/document.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace causalis {
namespace {

constexpr std::size_t max_document_bytes = 16U * 1024U * 1024U;
constexpr std::size_t max_open_elements = 512;
constexpr std::size_t max_elements = 65'536;
constexpr std::size_t max_attributes_per_element = 256;
constexpr std::size_t max_attributes = 65'536;
constexpr std::size_t max_attribute_name_bytes = 128;
constexpr std::size_t max_attribute_value_bytes = 4096;
constexpr std::size_t no_node = std::numeric_limits<std::size_t>::max();

bool ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}
bool ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool name_char(char c) {
    return ascii_alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
}
char lower_char(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
std::string lower(std::string_view input) {
    std::string result(input);
    for (char& c : result) c = lower_char(c);
    return result;
}
bool equals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_char(a[i]) != lower_char(b[i])) return false;
    }
    return true;
}
bool void_tag(std::string_view tag) {
    constexpr std::array tags{"area", "base", "br", "col", "embed", "hr", "img",
                              "input", "link", "meta", "param", "source", "track", "wbr"};
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}
bool raw_tag(std::string_view tag) {
    constexpr std::array tags{"script", "style", "textarea", "title", "xmp", "iframe",
                              "noembed", "noframes", "plaintext"};
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}
bool omitted_tag(std::string_view tag) {
    constexpr std::array tags{"head", "script", "style", "template", "form", "button",
                              "select", "textarea", "iframe", "object", "embed", "svg",
                              "math", "canvas", "noscript", "noembed", "noframes"};
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}
bool allowed_tag(std::string_view tag) {
    constexpr std::array tags{"article", "main", "section", "aside", "header", "footer",
        "nav", "h1", "h2", "h3", "h4", "h5", "h6", "p", "blockquote", "ul", "ol", "li",
        "pre", "code", "strong", "b", "em", "i", "u", "s", "br", "hr", "a", "table",
        "thead", "tbody", "tfoot", "tr", "th", "td", "figure", "figcaption", "div", "span",
        "dl", "dt", "dd", "details", "summary"};
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}
bool block_tag(std::string_view tag) {
    constexpr std::array tags{"article", "main", "section", "aside", "header", "footer",
        "nav", "h1", "h2", "h3", "h4", "h5", "h6", "p", "blockquote", "ul", "ol", "li",
        "pre", "table", "tr", "figure", "figcaption", "div", "dl", "dt", "dd", "details",
        "summary", "br", "hr"};
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

void append_utf8(std::string& output, std::uint32_t value) {
    if (value == 0 || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        value = 0xFFFDU;
    }
    if (value <= 0x7FU) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

// Only the small named entity set supported by this document adapter is
// decoded; unsupported entities remain literal text. Numeric entities accept
// optional semicolons and reject overflow rather than wrapping code points.
std::string decode_entities(std::string_view source, std::size_t limit = max_document_bytes,
                            bool* overflow = nullptr) {
    std::string result;
    result.reserve(std::min(source.size(), limit));
    if (overflow) *overflow = false;
    for (std::size_t p = 0; p < source.size();) {
        if (result.size() > limit) {
            if (overflow) *overflow = true;
            return {};
        }
        if (source[p] == '\0') { append_utf8(result, 0xFFFDU); ++p; continue; }
        if (source[p] != '&') { result.push_back(source[p++]); continue; }
        const std::size_t start = p;
        std::size_t q = p + 1;
        if (q < source.size() && source[q] == '#') {
            ++q;
            unsigned base = 10;
            if (q < source.size() && (source[q] == 'x' || source[q] == 'X')) { base = 16; ++q; }
            const std::size_t digits = q;
            std::uint32_t value = 0;
            bool overflow = false;
            while (q < source.size()) {
                const char c = lower_char(source[q]);
                unsigned d = 16;
                if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
                if (d >= base) break;
                if (value > (0x10FFFFU - d) / base) overflow = true;
                if (!overflow) value = value * base + d;
                ++q;
            }
            if (q > digits) {
                if (q < source.size() && source[q] == ';') ++q;
                append_utf8(result, overflow ? 0xFFFDU : value);
                p = q;
                continue;
            }
        } else {
            constexpr std::array<std::pair<std::string_view, std::uint32_t>, 6> entities{{
                {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''},
                {"nbsp;", 0xA0U}}};
            bool found = false;
            for (const auto& [name, value] : entities) {
                if (source.substr(q, name.size()) == name) {
                    append_utf8(result, value);
                    p = q + name.size();
                    found = true;
                    break;
                }
            }
            if (found) continue;
        }
        result.push_back(source[start]);
        p = start + 1;
    }
    if (result.size() > limit) {
        if (overflow) *overflow = true;
        return {};
    }
    return result;
}

std::string escaped(std::string_view source) {
    std::string result;
    result.reserve(source.size());
    for (char c : source) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        case '\0': append_utf8(result, 0xFFFDU); break;
        default: result.push_back(c); break;
        }
    }
    return result;
}

struct Attribute {
    std::string name, value;
    std::size_t begin{}, end{};
};
struct Node {
    std::string tag, id, href;
    std::size_t parent{no_node};
    std::size_t begin{}, content_begin{}, content_end{}, end{};
    std::vector<Attribute> attributes;
    bool inert{}, omitted{}, title_excluded{}, raw{}, is_void{}, closed{}, opening_complete{};
};
enum class EventKind { Open, Close, Text };
struct Event {
    EventKind kind;
    std::size_t node{no_node};
    std::size_t offset{}, length{};
};
struct Parsed {
    std::vector<Node> nodes;
    std::vector<Event> events;
};

std::size_t tag_end(std::string_view source, std::size_t p) {
    char quote = 0;
    for (; p < source.size(); ++p) {
        const char c = source[p];
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '>') return p + 1;
    }
    return source.size();
}

std::size_t raw_end(std::string_view source, std::size_t p, std::string_view tag) {
    if (tag == "plaintext") return source.size();
    while (p < source.size()) {
        p = source.find('<', p);
        if (p == std::string_view::npos) return source.size();
        const std::size_t name = p + 2;
        if (p + 1 < source.size() && source[p + 1] == '/' &&
            equals_ascii(source.substr(name, tag.size()), tag)) {
            const std::size_t following = name + tag.size();
            if (following == source.size() || ascii_space(source[following]) ||
                source[following] == '>' || source[following] == '/') return p;
        }
        ++p;
    }
    return source.size();
}

Parsed scan(std::string_view source) {
    Parsed parsed;
    std::size_t attributes_seen = 0;
    std::vector<std::size_t> stack;
    stack.reserve(max_open_elements);
    auto parent = [&]() { return stack.empty() ? no_node : stack.back(); };
    auto text = [&](std::size_t start, std::size_t end) {
        if (start >= end) return;
        if (!parsed.events.empty() && parsed.events.back().kind == EventKind::Text &&
            parsed.events.back().node == parent() &&
            parsed.events.back().offset + parsed.events.back().length == start) {
            parsed.events.back().length += end - start;
        } else parsed.events.push_back({EventKind::Text, parent(), start, end - start});
    };
    auto close_top = [&](std::size_t content_end, std::size_t end) {
        const auto index = stack.back();
        stack.pop_back();
        auto& node = parsed.nodes[index];
        node.content_end = content_end;
        node.end = end;
        node.closed = true;
        parsed.events.push_back({EventKind::Close, index, content_end, 0});
    };
    std::size_t p = 0;
    while (p < source.size()) {
        if (!stack.empty() && parsed.nodes[stack.back()].raw) {
            const std::size_t end = raw_end(source, p, parsed.nodes[stack.back()].tag);
            text(p, end);
            p = end;
            if (p == source.size()) break;
        }
        if (source[p] != '<') {
            const std::size_t end = source.find('<', p);
            text(p, end == std::string_view::npos ? source.size() : end);
            p = end == std::string_view::npos ? source.size() : end;
            continue;
        }
        if (source.substr(p, 4) == "<!--") {
            const std::size_t end = source.find("-->", p + 4);
            p = end == std::string_view::npos ? source.size() : end + 3;
            continue;
        }
        if (source.substr(p, 2) == "<!" || source.substr(p, 2) == "<?") {
            p = tag_end(source, p + 2);
            continue;
        }
        const std::size_t begin = p;
        const bool closing = p + 1 < source.size() && source[p + 1] == '/';
        std::size_t q = p + (closing ? 2U : 1U);
        if (q >= source.size() || !ascii_alpha(source[q])) {
            text(p, p + 1);
            ++p;
            continue;
        }
        const std::size_t name_start = q;
        while (q < source.size() && name_char(source[q])) ++q;
        const std::string tag = lower(source.substr(name_start, q - name_start));
        if (q < source.size() && !ascii_space(source[q]) && source[q] != '>' && source[q] != '/') {
            text(p, p + 1);
            ++p;
            continue;
        }
        if (closing) {
            const std::size_t end = tag_end(source, q);
            auto matching = std::find_if(stack.rbegin(), stack.rend(), [&](std::size_t i) {
                return parsed.nodes[i].tag == tag;
            });
            if (matching != stack.rend()) {
                const std::size_t target = *matching;
                while (!stack.empty()) {
                    const auto top = stack.back();
                    close_top(begin, top == target ? end : begin);
                    if (top == target) break;
                }
            }
            p = end;
            continue;
        }
        if (parsed.nodes.size() == max_elements) break;
        Node node;
        node.tag = tag;
        node.begin = begin;
        node.raw = raw_tag(tag);
        node.is_void = void_tag(tag);
        bool has_id = false, has_href = false, has_hidden = false;
        while (q < source.size()) {
            while (q < source.size() && ascii_space(source[q])) ++q;
            if (q == source.size()) break;
            if (source[q] == '>') { ++q; node.opening_complete = true; break; }
            if (source[q] == '/' && q + 1 < source.size() && source[q + 1] == '>') {
                q += 2; node.opening_complete = true; break;
            }
            const std::size_t attr_start = q;
            while (q < source.size() && !ascii_space(source[q]) && source[q] != '=' &&
                   source[q] != '>' && source[q] != '/') ++q;
            if (q == attr_start) { ++q; continue; }
            if (q - attr_start > max_attribute_name_bytes ||
                node.attributes.size() >= max_attributes_per_element ||
                attributes_seen >= max_attributes) return {};
            ++attributes_seen;
            const std::string attr = lower(source.substr(attr_start, q - attr_start));
            while (q < source.size() && ascii_space(source[q])) ++q;
            std::string value;
            if (q < source.size() && source[q] == '=') {
                ++q;
                while (q < source.size() && ascii_space(source[q])) ++q;
                const bool quoted = q < source.size() && (source[q] == '\'' || source[q] == '"');
                const char quote = quoted ? source[q++] : '\0';
                const std::size_t value_start = q;
                if (quoted) { while (q < source.size() && source[q] != quote) ++q; }
                else { while (q < source.size() && !ascii_space(source[q]) && source[q] != '>') ++q; }
                bool overflow = false;
                value = decode_entities(source.substr(value_start, q - value_start),
                                        max_attribute_value_bytes, &overflow);
                if (overflow) return {};
                if (quoted && q < source.size()) ++q;
            }
            node.attributes.push_back({attr, value, attr_start, q});
            if (attr == "id" && !has_id) { node.id = std::move(value); has_id = true; }
            else if (attr == "href" && !has_href) { node.href = std::move(value); has_href = true; }
            else if (attr == "hidden") has_hidden = true;
        }
        // The limited scanner implements common paragraph/list optional ends.
        // All stack searches are bounded, including adversarial malformed HTML.
        if (!stack.empty() && ((tag == "p" || block_tag(tag)) && parsed.nodes[stack.back()].tag == "p")) {
            close_top(begin, begin);
        }
        if (!stack.empty() && tag == "li" && parsed.nodes[stack.back()].tag == "li") close_top(begin, begin);
        node.parent = parent();
        if (node.parent != no_node) {
            node.inert = parsed.nodes[node.parent].inert;
            node.omitted = parsed.nodes[node.parent].omitted;
            node.title_excluded = parsed.nodes[node.parent].title_excluded;
        }
        node.inert = node.inert || tag == "template" || tag == "noscript" || tag == "svg" || tag == "math";
        node.omitted = node.omitted || omitted_tag(tag) || has_hidden;
        node.title_excluded = node.title_excluded || (omitted_tag(tag) && tag != "head") || has_hidden;
        node.content_begin = q;
        node.content_end = q;
        node.end = q;
        const std::size_t index = parsed.nodes.size();
        parsed.nodes.push_back(std::move(node));
        parsed.events.push_back({EventKind::Open, index, begin, q - begin});
        p = q;
        if (parsed.nodes[index].is_void) {
            parsed.nodes[index].closed = true;
            parsed.events.push_back({EventKind::Close, index, p, 0});
        } else if (stack.size() < max_open_elements) {
            stack.push_back(index);
        } else {
            // Fail closed when nesting exceeds the scanner budget. Preserve
            // source but omit its undecoded tail from inspection/reading.
            parsed.nodes[index].omitted = true;
            parsed.nodes[index].inert = true;
            parsed.nodes[index].content_end = source.size();
            parsed.nodes[index].end = source.size();
            parsed.nodes[index].closed = true;
            parsed.events.push_back({EventKind::Close, index, source.size(), 0});
            p = source.size();
        }
    }
    while (!stack.empty()) close_top(source.size(), source.size());
    return parsed;
}

std::string title_of(const Parsed& parsed, std::string_view source) {
    for (const auto& node : parsed.nodes) {
        if (node.tag == "title" && !node.inert && !node.title_excluded) {
            const auto text = decode_entities(source.substr(node.content_begin, node.content_end - node.content_begin));
            std::string title;
            bool pending = false;
            for (char c : text) {
                if (ascii_space(c)) pending = !title.empty();
                else {
                    if (pending) title.push_back(' ');
                    title.push_back(c);
                    pending = false;
                }
            }
            return title;
        }
    }
    return {};
}

bool safe_href(std::string_view value) {
    if (value.empty()) return false;
    // Reject embedded whitespace/control bytes before interpreting a scheme.
    for (unsigned char c : value) if (c <= 0x20U || c == 0x7FU || c == '\\') return false;
    const auto colon = value.find(':');
    const auto path = value.find_first_of("/?#");
    if (colon == std::string_view::npos || (path != std::string_view::npos && path < colon)) return true;
    return equals_ascii(value.substr(0, colon), "http") || equals_ascii(value.substr(0, colon), "https");
}

std::string_view trimmed(std::string_view value) {
    while (!value.empty() && ascii_space(value.front())) value.remove_prefix(1);
    while (!value.empty() && ascii_space(value.back())) value.remove_suffix(1);
    return value;
}
const Attribute* attribute_of(const Node& node, std::string_view name) {
    for (const auto& attribute : node.attributes) if (attribute.name == name) return &attribute;
    return nullptr;
}
bool supported_script(const Node& node) {
    if (node.tag != "script" || node.inert || attribute_of(node, "src")) return false;
    const Attribute* type = attribute_of(node, "type");
    if (!type) return true;
    const auto value = trimmed(type->value);
    return value.empty() || equals_ascii(value, "text/javascript") || equals_ascii(value, "application/javascript");
}
bool mutable_node(const Node& node, std::string_view source) {
    return !node.inert && !node.raw && node.opening_complete && node.content_begin > node.begin &&
        node.content_begin <= source.size() && source[node.content_begin - 1] == '>';
}
bool safe_attribute(std::string_view name) {
    constexpr std::array names{"id", "class", "title", "lang", "dir", "role", "hidden", "aria-label", "aria-hidden"};
    return std::find(names.begin(), names.end(), name) != names.end();
}
bool safe_style_property(std::string_view name) {
    constexpr std::array names{"color", "background", "background-color", "font-size", "font-weight", "display",
        "margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "padding", "padding-top",
        "padding-right", "padding-bottom", "padding-left", "width", "max-width"};
    return std::find(names.begin(), names.end(), name) != names.end();
}
bool safe_rgb_function(std::string_view value) {
    const bool rgba = value.starts_with("rgba(");
    if ((!rgba && !value.starts_with("rgb(")) || value.back() != ')') return false;
    value.remove_prefix(rgba ? 5U : 4U);
    value.remove_suffix(1);
    std::size_t count = 0;
    while (true) {
        const auto comma = value.find(',');
        auto channel = trimmed(value.substr(0, comma));
        bool percentage = false;
        if (!channel.empty() && channel.back() == '%') { channel.remove_suffix(1); percentage = true; }
        if (!channel.empty() && channel.front() == '+') channel.remove_prefix(1);
        if (channel.empty()) return false;
        // No nested functions, additional tokens, or CSS resource syntax.
        for (char c : channel) if (!(c >= '0' && c <= '9') && c != '.' && c != '-') return false;
        double number = 0;
        const auto parsed = std::from_chars(channel.data(), channel.data() + channel.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != channel.data() + channel.size() || !std::isfinite(number)) return false;
        const double maximum = percentage ? 100.0 : (count == 3 ? 1.0 : 255.0);
        if (number < 0 || number > maximum) return false;
        ++count;
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return count == (rgba ? 4U : 3U);
}
bool safe_style_value(std::string_view property, std::string_view value) {
    value = trimmed(value);
    if (value.empty() || value.size() > 4096) return false;
    for (char c : value) {
        if (!ascii_alpha(c) && !(c >= '0' && c <= '9') && c != '#' && c != '.' && c != '%' &&
            c != '+' && c != '-' && c != ' ' && c != ',' && c != '(' && c != ')') return false;
    }
    const auto normalized = lower(value);
    if (property == "display") {
        return normalized == "none" || normalized == "block" || normalized == "inline" || normalized == "inline-block";
    }
    if (normalized.find_first_of("()") != std::string::npos) {
        const bool color = property == "color" || property == "background" || property == "background-color";
        return color && safe_rgb_function(normalized);
    }
    return true;
}

bool replace_attribute(std::string& source, const Node& node, std::string_view name, std::string_view value) {
    const std::string replacement = std::string(name) + "=\"" + escaped(value) + '"';
    std::vector<const Attribute*> matching;
    std::size_t removed = 0;
    for (const auto& attribute : node.attributes) {
        if (attribute.name == name) {
            matching.push_back(&attribute);
            removed += attribute.end - attribute.begin;
        }
    }
    const std::size_t inserted = replacement.size() + (matching.empty() ? 1U : 0U);
    if (inserted > max_document_bytes - (source.size() - removed)) return false;
    if (!matching.empty()) {
        for (std::size_t i = matching.size(); i > 0; --i) {
            const auto& attribute = *matching[i - 1];
            source.replace(attribute.begin, attribute.end - attribute.begin, i == 1 ? replacement : std::string{});
        }
    } else {
        std::size_t point = node.content_begin - 1;
        if (point > node.begin && source[point - 1] == '/') --point;
        source.insert(point, " " + replacement);
    }
    return true;
}

std::string plain_of(const Parsed& parsed, std::string_view source) {
    std::string result;
    bool pending_space = false;
    auto line_break = [&]() {
        pending_space = false;
        if (!result.empty() && result.back() != '\n') result.push_back('\n');
    };
    for (const auto& event : parsed.events) {
        const Node* node = event.node == no_node ? nullptr : &parsed.nodes[event.node];
        if (node && node->omitted) continue;
        if (event.kind != EventKind::Text) {
            if (node && block_tag(node->tag)) line_break();
            continue;
        }
        const auto text = decode_entities(source.substr(event.offset, event.length));
        for (std::size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (static_cast<unsigned char>(c) == 0xC2U && i + 1 < text.size() &&
                static_cast<unsigned char>(text[i + 1]) == 0xA0U) { c = ' '; ++i; }
            if (ascii_space(c)) pending_space = !result.empty() && result.back() != '\n';
            else {
                if (pending_space) result.push_back(' ');
                result.push_back(c);
                pending_space = false;
            }
        }
    }
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

} // namespace

Document::Document(std::string html) : html_(std::move(html)) {
    if (html_.size() > max_document_bytes) throw std::length_error("Document exceeds the 16 MiB scanner limit");
}
const std::string& Document::html() const noexcept { return html_; }
std::string Document::title() const { return title_of(scan(html_), html_); }

std::vector<std::string> Document::scripts() const {
    const auto parsed = scan(html_);
    std::vector<std::string> result;
    for (const auto& node : parsed.nodes) {
        if (supported_script(node)) {
            result.emplace_back(html_.substr(node.content_begin, node.content_end - node.content_begin));
        }
    }
    return result;
}

bool Document::set_text(std::string_view selector, std::string_view text) {
    if (selector.size() < 2 || selector.front() != '#') return false;
    const auto id = selector.substr(1);
    const auto parsed = scan(html_);
    for (const auto& node : parsed.nodes) {
        if (node.id != id || node.inert) continue;
        if (!mutable_node(node, html_) || node.is_void || !node.closed) return false;
        if (text.size() > max_document_bytes) throw std::length_error("Text mutation exceeds the 16 MiB scanner limit");
        const auto replacement = escaped(text);
        const auto removed = node.content_end - node.content_begin;
        if (replacement.size() > max_document_bytes - (html_.size() - removed)) {
            throw std::length_error("Text mutation exceeds the 16 MiB scanner limit");
        }
        html_.replace(node.content_begin, removed, replacement);
        return true;
    }
    return false;
}

bool Document::set_attribute(std::string_view selector, std::string_view name, std::string_view value) {
    if (selector.size() < 2 || selector.size() > 4097 || selector.front() != '#' ||
        name.size() > 64 || value.size() > 4096) return false;
    const auto normalized_name = lower(name);
    if (!safe_attribute(normalized_name)) return false;
    const auto parsed = scan(html_);
    for (const auto& node : parsed.nodes) {
        if (node.id != selector.substr(1) || node.inert) continue;
        if (!mutable_node(node, html_)) return false;
        return replace_attribute(html_, node, normalized_name, value);
    }
    return false;
}

bool Document::set_style(std::string_view selector, std::string_view property, std::string_view value) {
    if (selector.size() < 2 || selector.size() > 4097 || selector.front() != '#' ||
        property.size() > 64 || value.size() > 4096) return false;
    const auto normalized_property = lower(property);
    value = trimmed(value);
    if (!safe_style_property(normalized_property) || !safe_style_value(normalized_property, value)) return false;
    const auto parsed = scan(html_);
    for (const auto& node : parsed.nodes) {
        if (node.id != selector.substr(1) || node.inert) continue;
        if (!mutable_node(node, html_)) return false;
        std::string style;
        if (const auto* previous = attribute_of(node, "style")) {
            std::string_view declarations = previous->value;
            while (!declarations.empty()) {
                const auto end = declarations.find(';');
                const auto declaration = declarations.substr(0, end);
                const auto colon = declaration.find(':');
                if (colon != std::string_view::npos) {
                    const auto old_name = lower(trimmed(declaration.substr(0, colon)));
                    const auto old_value = trimmed(declaration.substr(colon + 1));
                    if (old_name != normalized_property && safe_style_property(old_name) && safe_style_value(old_name, old_value)) {
                        style += old_name + ':' + std::string(old_value) + ';';
                    }
                }
                if (end == std::string_view::npos) break;
                declarations.remove_prefix(end + 1);
            }
        }
        style += normalized_property + ':' + std::string(value) + ';';
        if (style.size() > max_attribute_value_bytes) return false;
        return replace_attribute(html_, node, "style", style);
    }
    return false;
}

std::string Document::reading_html() const {
    const auto parsed = scan(html_);
    const std::string title = title_of(parsed, html_);
    std::string result = "<!doctype html><html><head><meta charset=\"utf-8\"><title>";
    result += escaped(title.empty() ? "Reading view" : title);
    result += "</title><style>body{background:#f4f0e6;color:#232323;padding:24px;}"
              "main{max-width:840px;}a{color:#235b9c;}pre{white-space:pre-wrap;}"
              "h1,h2,h3{color:#162b44;}</style></head><body><main>";
    for (const auto& event : parsed.events) {
        const Node* node = event.node == no_node ? nullptr : &parsed.nodes[event.node];
        if (node && node->omitted) continue;
        if (event.kind == EventKind::Text) {
            result += escaped(decode_entities(std::string_view(html_).substr(event.offset, event.length)));
        } else if (node && allowed_tag(node->tag)) {
            if (event.kind == EventKind::Open) {
                result += '<';
                result += node->tag;
                if (node->tag == "a" && safe_href(node->href)) {
                    result += " href=\"";
                    result += escaped(node->href);
                    result += '"';
                }
                result += '>';
            } else if (!node->is_void) {
                result += "</";
                result += node->tag;
                result += '>';
            }
        }
    }
    result += "</main></body></html>";
    return result;
}

std::string Document::plain_text() const { return plain_of(scan(html_), html_); }

DocumentDifference Document::difference(std::string_view previous) const noexcept {
    DocumentDifference result;
    std::size_t prefix = 0;
    const std::size_t common_size = std::min(previous.size(), html_.size());
    while (prefix < common_size && previous[prefix] == html_[prefix]) ++prefix;
    if (prefix == previous.size() && prefix == html_.size()) return result;
    std::size_t suffix = 0;
    while (suffix < common_size - prefix &&
           previous[previous.size() - 1 - suffix] == html_[html_.size() - 1 - suffix]) ++suffix;
    result.changed = true;
    result.first_changed_byte = prefix;
    result.removed_bytes = previous.size() - prefix - suffix;
    result.inserted_bytes = html_.size() - prefix - suffix;
    return result;
}

} // namespace causalis
