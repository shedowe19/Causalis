#include "causalis/checkpoint.hpp"
#include "causalis/document.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <unordered_map>

namespace causalis {
namespace {
constexpr std::string_view signature = "CAUSALIS-CHECKPOINT-1\n";
constexpr std::size_t max_html = 2 * 1024 * 1024;
std::string_view utf8_prefix(std::string_view text, std::size_t maximum) {
    std::size_t end = std::min(text.size(), maximum);
    if (end < text.size())
        while (end && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return text.substr(0,end);
}
void check(const Checkpoint& value) {
    if (value.origin.size() > 4096 || value.title.size() > 4096 || value.passive_html.size() > max_html)
        throw std::runtime_error("Checkpoint exceeds supported limits.");
}
void append(std::string& target, std::string_view value) {
    target += std::to_string(value.size()); target += '\n'; target += value;
}
std::string take(std::string_view& input, std::size_t maximum) {
    const auto end = input.find('\n');
    if (end == std::string_view::npos || end == 0 || end > 10)
        throw std::runtime_error("Malformed checkpoint length.");
    std::size_t length{};
    const auto digits = input.substr(0,end);
    auto result = std::from_chars(digits.data(), digits.data()+digits.size(), length);
    if (result.ec != std::errc{} || result.ptr != digits.data()+digits.size() || length > maximum)
        throw std::runtime_error("Unsupported checkpoint length.");
    input.remove_prefix(end+1);
    if (length > input.size()) throw std::runtime_error("Truncated checkpoint.");
    std::string value(input.substr(0,length)); input.remove_prefix(length); return value;
}
std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> result;
    while (!text.empty() && result.size() < 2048) {
        const auto end = text.find('\n');
        auto line = text.substr(0,end);
        if (!line.empty()) result.emplace_back(utf8_prefix(line,4096));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end+1);
    }
    return result;
}
}
Checkpoint make_checkpoint(const Document& document, std::string_view origin) {
    if (origin.size() > 4096) throw std::runtime_error("Checkpoint origin exceeds supported limits.");
    Checkpoint result{std::string(origin), {}, document.reading_html()};
    Document passive(result.passive_html);
    result.title = std::string(utf8_prefix(passive.title(),4096));
    check(result); return result;
}
std::string encode_checkpoint(const Checkpoint& value) {
    check(value);
    std::string out(signature);
    append(out,value.origin); append(out,value.title); append(out,value.passive_html); return out;
}
Checkpoint decode_checkpoint(std::string_view input) {
    if (!input.starts_with(signature)) throw std::runtime_error("Unknown checkpoint format.");
    input.remove_prefix(signature.size());
    Checkpoint value{take(input,4096),take(input,4096),take(input,max_html)};
    if (!input.empty()) throw std::runtime_error("Unexpected checkpoint trailing data.");
    // A checkpoint can be supplied by another program. Regenerate its passive
    // document rather than trusting the stored source to be script-free.
    value.passive_html = Document(value.passive_html).reading_html();
    check(value); return value;
}
std::vector<CheckpointChange> compare_checkpoints(const Checkpoint& before,const Checkpoint& after) {
    check(before); check(after);
    const auto old_text = Document(before.passive_html).plain_text();
    const auto new_text = Document(after.passive_html).plain_text();
    const auto old_lines = lines(old_text);
    const auto new_lines = lines(new_text);
    std::unordered_map<std::string,std::size_t> old_count,new_count;
    for (const auto& line : old_lines) ++old_count[line];
    for (const auto& line : new_lines) ++new_count[line];
    std::vector<CheckpointChange> changes;
    for (const auto& line : old_lines) {
        if (new_count[line]) --new_count[line];
        else changes.push_back({CheckpointChange::Kind::Removed,line});
    }
    for (const auto& line : new_lines) {
        if (old_count[line]) --old_count[line];
        else changes.push_back({CheckpointChange::Kind::Added,line});
    }
    if (changes.empty() && old_text != new_text)
        changes.push_back({CheckpointChange::Kind::Notice,"Text changed in its order or outside the bounded line comparison."});
    return changes;
}
}
