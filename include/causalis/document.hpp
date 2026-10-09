#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace causalis {

struct DocumentDifference {
    bool changed{};
    std::size_t first_changed_byte{};
    std::size_t removed_bytes{};
    std::size_t inserted_bytes{};
};

// An inspectable source-document adapter for the original renderer. This is a
// bounded HTML scanner, not a complete HTML5 DOM or a scripting environment.
// Source is preserved exactly until a successful plain-text mutation. Parsing
// is limited to 512 open elements, 65,536 elements/attributes and 16 MiB input.
// Attributes allow 256 per element, 128-byte names and 4096-byte decoded values;
// exceeding an attribute budget makes inspection fail closed for the document.
// HTML5
// adoption-agency/foster-parenting rules and foreign namespaces are unsupported.
class Document {
public:
    explicit Document(std::string html);

    const std::string& html() const noexcept;
    std::string title() const;

    // Supported classic inline JavaScript bodies for inspection only: absent/
    // empty type, text/javascript and application/javascript. Templates,
    // modules, non-script data blocks and external src scripts are excluded.
    // External sources are never fetched and scripts are never run here.
    std::vector<std::string> scripts() const;

    // Supports a literal #id selector, including UTF-8 IDs, without CSS escape
    // interpretation. Mutates the first matching non-inert, non-raw, non-void
    // element. Text is escaped, so it cannot introduce markup or attributes.
    bool set_text(std::string_view selector, std::string_view text);

    // Source-safe mutations for the host's explicitly limited script runtime.
    // Attributes allow id/class/title/lang/dir/role/hidden/aria-label/aria-hidden
    // only. Style accepts supported visual properties and rejects CSS resource
    // syntax and injection delimiters. Values are capped at 4096 bytes.
    bool set_attribute(std::string_view selector, std::string_view name, std::string_view value);
    bool set_style(std::string_view selector, std::string_view property, std::string_view value);

    // A generated, escaped static reading document. Executable/resource tags,
    // templates, forms, hidden subtrees and the original head are omitted.
    // Links allow http(s), relative paths and fragment identifiers only.
    std::string reading_html() const;
    std::string plain_text() const;

    // Linear-time source comparison suitable for a host-owned checkpoint.
    // A difference is one contiguous byte range, not a semantic DOM diff.
    DocumentDifference difference(std::string_view previous) const noexcept;

private:
    std::string html_;
};

} // namespace causalis
