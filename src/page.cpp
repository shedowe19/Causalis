#include "causalis/page.hpp"
#include "causalis/document.hpp"
#include "causalis/script.hpp"
#include <stdexcept>
#include <utility>

namespace causalis {
PageResult render_page(std::string html, float width, PageOptions options, MeasureText measure) {
    constexpr std::size_t document_limit = 2 * 1024 * 1024;
    constexpr std::size_t mutation_work_limit = 16 * 1024 * 1024;
    constexpr std::size_t mutation_limit = 256;
    if (html.size() > document_limit) throw std::runtime_error("Page exceeds the 2 MiB document limit.");
    Document document(std::move(html));
    PageResult out;
    if (options.run_scripts && options.source == DocumentSource::Remote) {
        out.script_diagnostics.push_back("Remote document scripts are disabled in this experimental runtime.");
    } else if (options.run_scripts) {
        std::string source;
        for (const auto& block : document.scripts()) {
            source += block;
            source += '\n';
        }
        if (!source.empty()) {
            auto result = script::execute(source, options.instruction_budget);
            out.console = std::move(result.console);
            out.script_diagnostics = std::move(result.diagnostics);
            if (result.success) {
                Document transaction(document.html());
                std::size_t work = 0, count = 0, ignored = 0;
                bool committed = true;
                for (const auto& mutation : result.mutations) {
                    if (++count > mutation_limit || transaction.html().size() > mutation_work_limit - work) {
                        out.script_diagnostics.push_back("Document mutation budget exceeded; all document changes were discarded.");
                        committed = false; break;
                    }
                    work += transaction.html().size();
                    bool applied = false;
                    if (mutation.property == "textContent")
                        applied = transaction.set_text(mutation.selector, mutation.value);
                    else if (mutation.property.starts_with("style."))
                        applied = transaction.set_style(mutation.selector, mutation.property.substr(6), mutation.value);
                    else if (mutation.property.starts_with("attribute."))
                        applied = transaction.set_attribute(mutation.selector, mutation.property.substr(10), mutation.value);
                    if (!applied) ++ignored;
                    if (transaction.html().size() > document_limit) {
                        out.script_diagnostics.push_back("Document size limit exceeded; all document changes were discarded.");
                        committed = false; break;
                    }
                }
                if (ignored) out.script_diagnostics.push_back("Unsupported or unmatched document mutations were ignored: " + std::to_string(ignored));
                if (committed) document = std::move(transaction);
            }
        }
    }
    out.document_html = document.html();
    out.frame = render(options.reading_view ? document.reading_html() : document.html(), width, std::move(measure));
    return out;
}
}
