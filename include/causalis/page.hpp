#pragma once
#include "causalis/engine.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace causalis {
enum class DocumentSource { Local, Remote };
struct PageOptions {
    DocumentSource source{DocumentSource::Local};
    bool run_scripts{};
    bool reading_view{};
    std::size_t instruction_budget{100000};
};
struct PageResult {
    Frame frame;
    std::string document_html;
    std::vector<std::string> console;
    std::vector<std::string> script_diagnostics;
};
// Explicit opt-in local execution only. This experimental pipeline does not
// execute fetched scripts or provide network, filesystem or vault APIs to JS.
// Pages are limited to 2 MiB. Applying a script transaction is bounded to 256
// operations and 16 MiB cumulative source scan work; exhaustion discards every
// document change. Unmatched/inert #id targets are reported and ignored.
PageResult render_page(std::string html, float viewport_width,
                       PageOptions options = {}, MeasureText measure = {});
}
