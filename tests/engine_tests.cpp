#include "causalis/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using causalis::Frame;
using causalis::PaintCommand;
using causalis::PaintKind;

void require(bool condition, std::string_view explanation) {
    if (!condition) {
        throw std::runtime_error(std::string(explanation));
    }
}

std::string normalized(std::string_view input) {
    std::string result;
    bool pending_space = false;
    for (unsigned char c : input) {
        if (std::isspace(c)) {
            pending_space = !result.empty();
        } else {
            if (pending_space) result.push_back(' ');
            result.push_back(static_cast<char>(c));
            pending_space = false;
        }
    }
    return result;
}

std::string visible_text(const Frame& frame, std::string_view link = {}) {
    std::string result;
    for (const auto& command : frame.display_list) {
        if (command.kind != PaintKind::Text) continue;
        if (!link.empty() && command.link != link) continue;
        if (!result.empty()) result.push_back(' ');
        result += command.text;
    }
    return normalized(result);
}

std::vector<const PaintCommand*> text_commands(const Frame& frame) {
    std::vector<const PaintCommand*> commands;
    for (const auto& command : frame.display_list) {
        if (command.kind == PaintKind::Text) commands.push_back(&command);
    }
    return commands;
}

bool rgb(const PaintCommand& command, unsigned char r, unsigned char g,
         unsigned char b) {
    return command.color.r == r && command.color.g == g && command.color.b == b;
}

void expect_text_color(const Frame& frame, std::string_view word,
                       unsigned char r, unsigned char g, unsigned char b) {
    bool found = false;
    for (const auto& command : frame.display_list) {
        if (command.kind != PaintKind::Text || command.text.find(word) == std::string::npos) {
            continue;
        }
        found = true;
        require(rgb(command, r, g, b), "visible text has an unexpected cascaded color");
    }
    require(found, "expected visible text was not painted");
}

void expect_valid_geometry(const Frame& frame) {
    constexpr float limit = 10'000'000.0F;
    require(std::isfinite(frame.width) && frame.width > 0 && frame.width <= limit,
            "frame width must be finite, positive and bounded");
    require(std::isfinite(frame.height) && frame.height >= 0 && frame.height <= limit,
            "frame height must be finite, nonnegative and bounded");
    for (const auto& command : frame.display_list) {
        const auto& b = command.bounds;
        require(std::isfinite(b.x) && std::isfinite(b.y) &&
                std::isfinite(b.width) && std::isfinite(b.height),
                "paint geometry must never contain NaN or infinity");
        require(std::abs(b.x) <= limit && std::abs(b.y) <= limit &&
                b.width >= 0 && b.width <= limit && b.height >= 0 && b.height <= limit,
                "paint geometry must have nonnegative, bounded dimensions");
        if (command.kind == PaintKind::Text) {
            require(std::isfinite(command.font_size) && command.font_size > 0 &&
                    command.font_size <= limit,
                    "painted text must have a finite, positive, bounded font size");
        }
    }
}

float measured(std::string_view value, float size, bool) {
    return static_cast<float>(value.size()) * size * 0.5F;
}

float vertical_span(const Frame& frame) {
    const auto commands = text_commands(frame);
    require(!commands.empty(), "wrapping fixture must paint text");
    float first = commands.front()->bounds.y;
    float last = first;
    for (const auto* command : commands) {
        first = std::min(first, command->bounds.y);
        last = std::max(last, command->bounds.y);
    }
    return last - first;
}

struct Test {
    const char* name;
    std::function<void()> run;
};

} // namespace

int main() {
    const std::vector<Test> tests = {
        {"empty input produces valid geometry", [] {
            const auto frame = causalis::render("", 640);
            expect_valid_geometry(frame);
            require(visible_text(frame).empty(), "empty input must not invent visible text");
        }},
        {"head metadata and executable source remain invisible", [] {
            const auto frame = causalis::render(
                "<!doctype html><html><head><title>Private title</title>"
                "<style>p { color: #123456; }</style><script>head_secret()</script>"
                "</head><body><p>Visible body</p><script>body_secret()</script></body></html>",
                640);
            const auto text = visible_text(frame);
            require(text.find("Visible") != std::string::npos, "body content must be visible");
            require(text.find("Private") == std::string::npos &&
                    text.find("secret") == std::string::npos &&
                    text.find("color") == std::string::npos,
                    "head/style/script source must not leak into page paint");
            require(!frame.diagnostics.empty(), "inactive scripts should produce a diagnostic");
        }},
        {"block text is laid out below preceding block", [] {
            const auto frame = causalis::render("<p>First</p><p>Second</p>", 640, measured);
            const auto commands = text_commands(frame);
            require(commands.size() >= 2, "both paragraphs must paint text");
            float first_y = -1;
            float second_y = -1;
            for (const auto* command : commands) {
                if (command->text.find("First") != std::string::npos) first_y = command->bounds.y;
                if (command->text.find("Second") != std::string::npos) second_y = command->bounds.y;
            }
            require(first_y >= 0 && second_y > first_y, "separate blocks must occupy separate lines");
        }},
        {"parent text color is inherited by inline children", [] {
            const auto frame = causalis::render(
                "<div style='color: #123456'><span>Inherited</span></div>", 640);
            expect_text_color(frame, "Inherited", 0x12, 0x34, 0x56);
        }},
        {"selector specificity wins over later generic rule", [] {
            const auto frame = causalis::render(
                "<style>#selected { color: #13579b; } .note { color: #112233; }"
                "p { color: #abcdef; }</style><p id='selected' class='note'>Specific</p>", 640);
            expect_text_color(frame, "Specific", 0x13, 0x57, 0x9b);
        }},
        {"equal specificity uses source order", [] {
            const auto frame = causalis::render(
                "<style>.note { color: #112233; }.note { color: #456789; }</style>"
                "<p class='note'>Later</p>", 640);
            expect_text_color(frame, "Later", 0x45, 0x67, 0x89);
        }},
        {"inline declaration overrides stylesheet", [] {
            const auto frame = causalis::render(
                "<style>#selected { color: #112233; }</style>"
                "<p id='selected' style='color: #aabbcc'>Inline</p>", 640);
            expect_text_color(frame, "Inline", 0xaa, 0xbb, 0xcc);
        }},
        {"compound selector requires all its parts", [] {
            const auto frame = causalis::render(
                "<style>p {color:#112233;} p.note#selected {color:#789abc;}</style>"
                "<p class='note' id='selected'>Matching</p>"
                "<p class='other' id='unselected'>Unmatched</p>", 640);
            expect_text_color(frame, "Matching", 0x78, 0x9a, 0xbc);
            expect_text_color(frame, "Unmatched", 0x11, 0x22, 0x33);
        }},
        {"entities decode as text without turning into markup", [] {
            const auto frame = causalis::render("<p>A&amp;B &lt;tag&gt; &#67; &#x44;</p>", 640);
            const auto text = visible_text(frame);
            require(text.find("A&B") != std::string::npos, "ampersand entity must decode");
            require(text.find("<tag>") != std::string::npos, "escaped markup must remain visible text");
            require(text.find("&#") == std::string::npos, "numeric entities must decode");
            require(text.find("C") != std::string::npos && text.find("D") != std::string::npos,
                    "decoded numeric entity characters must remain visible");
        }},
        {"narrow viewport wraps words onto more lines", [] {
            const std::string html = "<p>alpha bravo charlie delta echo foxtrot golf hotel</p>";
            const auto wide = causalis::render(html, 1000, measured);
            const auto narrow = causalis::render(html, 100, measured);
            require(visible_text(wide) == visible_text(narrow), "word wrapping must preserve text order");
            require(vertical_span(narrow) > vertical_span(wide), "narrow view must wrap onto more lines");
            require(narrow.height > wide.height, "wrapped content must increase document height");
            expect_valid_geometry(narrow);
        }},
        {"anchor labels retain their navigation target", [] {
            const std::string target = "https://example.invalid/project?q=one&mode=two";
            const auto frame = causalis::render(
                "<p>Before <a href='https://example.invalid/project?q=one&amp;mode=two'>"
                "Open <strong>project</strong></a> after</p>", 640);
            const auto linked = visible_text(frame, target);
            require(linked.find("Open") != std::string::npos &&
                    linked.find("project") != std::string::npos,
                    "all anchor label text, including nested inline text, must retain the target");
            for (const auto& command : frame.display_list) {
                if (command.kind == PaintKind::Text &&
                    (command.text.find("Before") != std::string::npos ||
                     command.text.find("after") != std::string::npos)) {
                    require(command.link.empty(), "anchor targets must not leak into adjacent text");
                }
            }
        }},
        {"image resource is reported without fetching it", [] {
            const auto frame = causalis::render(
                "<p>Safe</p><img src='https://example.invalid/private.png'>", 640);
            require(visible_text(frame).find("Safe") != std::string::npos,
                    "unsupported resources must not prevent surrounding text rendering");
            require(!frame.diagnostics.empty(), "unsupported image/network resource must be diagnosed");
            expect_valid_geometry(frame);
        }},
        {"malformed markup remains renderable", [] {
            const auto frame = causalis::render(
                "<div><p>Before <b>nested</div> After</p><span broken='unterminated", 640);
            expect_valid_geometry(frame);
            require(visible_text(frame).find("Before") != std::string::npos,
                    "malformed trailing markup must not discard preceding valid content");
        }},
        {"deeply nested untrusted markup is bounded", [] {
            std::string html;
            for (int depth = 0; depth < 3000; ++depth) html += "<div>";
            html += "Depth marker";
            for (int depth = 0; depth < 3000; ++depth) html += "</div>";
            const auto frame = causalis::render(html, 640);
            expect_valid_geometry(frame);
        }},
        {"invalid viewport dimensions are sanitized", [] {
            for (float width : {0.0F, -100.0F, std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(), 1.0e30F}) {
                expect_valid_geometry(causalis::render("<p>Visible</p>", width, measured));
            }
        }},
        {"hostile measurement callbacks cannot corrupt geometry", [] {
            for (float bad : {-100.0F, std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(), 1.0e30F}) {
                const auto frame = causalis::render(
                    "<p>alpha bravo charlie delta</p>", 150,
                    [bad](std::string_view, float, bool) { return bad; });
                expect_valid_geometry(frame);
                require(visible_text(frame).find("alpha") != std::string::npos,
                        "measurement failure must not discard page text");
            }
        }},
        {"extreme CSS dimensions cannot corrupt geometry", [] {
            const auto frame = causalis::render(
                "<div style='margin:999999999999px;padding:999999999999px;'>"
                "<p style='font-size:999999999999px;'>Extreme</p></div>", 640, measured);
            expect_valid_geometry(frame);
            require(visible_text(frame).find("Extreme") != std::string::npos,
                    "extreme author styles must preserve visible content");
        }},
        {"script close tag requires a valid tag delimiter", [] {
            const auto frame = causalis::render(
                "<script>opaque</scriptx><p>LEAK</p></script><p>Visible</p>", 640);
            const auto text = visible_text(frame);
            require(text.find("LEAK") == std::string::npos,
                    "a script-like tag name must not terminate opaque script content");
            require(text.find("Visible") != std::string::npos,
                    "content after the actual script end tag must remain visible");
            expect_valid_geometry(frame);
        }},
        {"style inside an inactive template cannot alter the document", [] {
            const auto frame = causalis::render(
                "<template><style>body{display:none}</style></template><body>Visible</body>", 640);
            require(visible_text(frame).find("Visible") != std::string::npos,
                    "stylesheet declarations in inactive template content must remain inert");
            expect_valid_geometry(frame);
        }},
        {"ID specificity outranks any repeated class specificity", [] {
            const auto frame = causalis::render(
                "<style>#selected{color:#123456}"
                ".a.a.a.a.a.a.a.a.a.a.a{color:#abcdef}</style>"
                "<p id=selected class=a>Specific</p>", 640);
            expect_text_color(frame, "Specific", 0x12, 0x34, 0x56);
        }},
        {"conflicting IDs in a compound selector cannot match", [] {
            const auto frame = causalis::render(
                "<style>p{color:#123456}#a#b{color:#abcdef}</style>"
                "<p id=b>Conflict</p>", 640);
            expect_text_color(frame, "Conflict", 0x12, 0x34, 0x56);
        }},
        {"duplicate selectors and declarations respect the style work budget", [] {
            std::string html = "<style>";
            for (int index = 0; index < 256; ++index) {
                if (index != 0) html.push_back(',');
                html.push_back('*');
            }
            html.push_back('{');
            for (int index = 0; index < 64; ++index) html += "color:#123456;";
            html += "}</style>";
            for (int index = 0; index < 256; ++index) html += "<p>Budget</p>";
            const auto frame = causalis::render(html, 640);
            expect_valid_geometry(frame);
            require(visible_text(frame).find("Budget") != std::string::npos,
                    "style budget exhaustion must preserve useful page content");
            const auto limit = std::find_if(frame.diagnostics.begin(), frame.diagnostics.end(),
                [](const auto& diagnostic) {
                    return diagnostic.category.find("limit") != std::string::npos ||
                           diagnostic.message.find("budget") != std::string::npos ||
                           diagnostic.message.find("limit") != std::string::npos;
                });
            require(limit != frame.diagnostics.end(),
                    "bounded style workload must report when its work budget is exhausted");
        }}
    };

    std::size_t failed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "FAIL " << test.name << ": unexpected exception\n";
        }
    }
    std::cout << tests.size() - failed << '/' << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
