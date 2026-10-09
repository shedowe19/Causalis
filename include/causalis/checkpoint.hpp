#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace causalis {
class Document;
struct Checkpoint {
    std::string origin;
    std::string title;
    std::string passive_html;
};
struct CheckpointChange {
    enum class Kind { Added, Removed, Notice };
    Kind kind;
    std::string text;
};
Checkpoint make_checkpoint(const Document& document, std::string_view origin);
std::string encode_checkpoint(const Checkpoint& checkpoint);
Checkpoint decode_checkpoint(std::string_view bytes);
// Bounded line comparison, not arbitrary JavaScript-state rollback.
std::vector<CheckpointChange> compare_checkpoints(const Checkpoint& before, const Checkpoint& after);
}
