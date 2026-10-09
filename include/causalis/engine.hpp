#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace causalis {

struct Color { unsigned char r{}, g{}, b{}; };
struct Rect { float x{}, y{}, width{}, height{}; };
enum class PaintKind { FillRect, Text };

// Coordinates are CSS pixels. Text bounds identify the top-left of its line
// box, not a baseline. `link` is a raw href; the embedding host decides which
// URL schemes it is willing to open. No navigation happens in this library.
struct PaintCommand {
    PaintKind kind{PaintKind::FillRect};
    Rect bounds;
    Color color;
    std::string text;
    float font_size{16.0F};
    bool bold{};
    std::string link;
    // Stable within this render: useful for an inspectable engine trace.
    unsigned source_id{};
};

struct Diagnostic { std::string category, message; };
struct Frame {
    float width{}, height{};
    std::vector<PaintCommand> display_list;
    std::vector<Diagnostic> diagnostics;
    std::string title;
};

using MeasureText = std::function<float(std::string_view, float, bool)>;

// A deliberately small, original HTML/CSS renderer. No script execution,
// networking, image decoding or external browser engine is used. Supplying
// the host's text metrics makes wrapping match its native font renderer.
Frame render(std::string_view html, float viewport_width, MeasureText measure = {});

} // namespace causalis
