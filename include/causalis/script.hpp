#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace causalis::script {

// An original, deliberately limited scripting language with JavaScript syntax.
// This is not an ECMAScript implementation. It does not expose the operating
// system, networking, browser storage, credentials, or a foreign JS runtime.
// Supported DOM selectors are one #id. The embedding
// renderer resolves selectors and applies the resulting transaction.
struct Mutation {
    std::string selector;
    // textContent, style.<hyphenated CSS property>, or attribute.<name>.
    // Attributes are limited to class/title/lang/dir/role/hidden/aria-label/
    // aria-hidden. Element IDs and navigation/resource attributes cannot change.
    std::string property;
    std::string value;
};

struct Result {
    std::vector<std::string> console;
    std::vector<Mutation> mutations;
    std::vector<std::string> diagnostics;
    bool success{};
};

// Runs synchronously with independent source, token, syntax-depth, call-depth,
// string, binding, and output limits. The instruction budget is capped at
// 1,000,000. All console output and mutations are discarded on any error.
// DOM reads are limited to properties already written by this execution;
// there is no access to the initial document or to live browser objects.
// Variables and named functions are declared in execution order (no hoisting).
// Numbers must remain finite: division by zero is an explicit runtime error.
// An execution emits at most 256 DOM mutations. querySelector/getElementById
// return selector handles; existence is checked by the embedding renderer.
// Styles are limited to the renderer's colors, fonts, display, spacing, width,
// and max-width properties; style and attribute values are capped at 4 KiB.
Result execute(std::string_view source, std::size_t instruction_budget = 100'000);

} // namespace causalis::script
