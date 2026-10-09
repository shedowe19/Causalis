#include "causalis/script.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace causalis::script {
namespace {

constexpr std::size_t source_limit = 1'048'576;
constexpr std::size_t string_limit = 65'536;
constexpr std::size_t token_limit = 100'000;
constexpr std::size_t node_limit = 40'000;
constexpr std::size_t syntax_depth_limit = 128;
constexpr std::size_t output_limit = 1'048'576;
constexpr std::size_t output_entry_limit = 2'048;
constexpr std::size_t mutation_limit = 256;
constexpr std::size_t binding_limit = 16'384;
constexpr std::size_t function_limit = 256;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

bool identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
bool identifier_part(char c) { return identifier_start(c) || (c >= '0' && c <= '9'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }

struct Token {
    enum class Kind { Identifier, Number, String, Punctuation, End } kind{};
    std::string text;
    std::size_t line{}, offset{};
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}
    std::vector<Token> run() {
        std::vector<Token> tokens;
        while (true) {
            skip_trivia();
            if (position_ == source_.size()) break;
            if (tokens.size() >= token_limit) fail("Script token limit exceeded");
            const auto start = position_;
            const auto line = line_;
            const char c = peek();
            if (identifier_start(c)) {
                advance();
                while (identifier_part(peek())) advance();
                tokens.push_back({Token::Kind::Identifier, std::string(source_.substr(start, position_ - start)), line, start});
            } else if (digit(c) || (c == '.' && digit(peek(1)))) {
                while (digit(peek())) advance();
                if (peek() == '.') {
                    advance();
                    while (digit(peek())) advance();
                }
                if (peek() == 'e' || peek() == 'E') {
                    advance();
                    if (peek() == '+' || peek() == '-') advance();
                    if (!digit(peek())) error("Invalid numeric exponent");
                    while (digit(peek())) advance();
                }
                tokens.push_back({Token::Kind::Number, std::string(source_.substr(start, position_ - start)), line, start});
            } else if (c == '\'' || c == '"') {
                tokens.push_back({Token::Kind::String, read_string(), line, start});
            } else {
                static constexpr std::string_view three[] = {"===", "!=="};
                static constexpr std::string_view two[] = {"==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "++", "--"};
                std::string punctuation;
                for (const auto op : three) {
                    if (source_.substr(position_, op.size()) == op) { punctuation = op; break; }
                }
                if (punctuation.empty()) {
                    for (const auto op : two) {
                        if (source_.substr(position_, op.size()) == op) { punctuation = op; break; }
                    }
                }
                if (punctuation.empty()) {
                    if (std::string_view("{}();,.[ ]?:+-*/%!<>=").find(c) == std::string_view::npos) {
                        error(c == '`' ? "Template literals are not supported" : "Unsupported script character");
                    }
                    punctuation.push_back(c);
                }
                position_ += punctuation.size();
                tokens.push_back({Token::Kind::Punctuation, std::move(punctuation), line, start});
            }
        }
        tokens.push_back({Token::Kind::End, "", line_, position_});
        return tokens;
    }

private:
    std::string_view source_;
    std::size_t position_{}, line_{1};
    char peek(std::size_t distance = 0) const {
        return distance < source_.size() - position_ ? source_[position_ + distance] : '\0';
    }
    void advance() { if (peek() == '\n') ++line_; ++position_; }
    [[noreturn]] void error(const std::string& message) const {
        fail(message + " at line " + std::to_string(line_) + ", byte " + std::to_string(position_));
    }
    void skip_trivia() {
        while (position_ < source_.size()) {
            if (space(peek())) { advance(); continue; }
            if (peek() == '/' && peek(1) == '/') {
                while (position_ < source_.size() && peek() != '\n') advance();
                continue;
            }
            if (peek() == '/' && peek(1) == '*') {
                advance(); advance();
                while (position_ < source_.size() && !(peek() == '*' && peek(1) == '/')) advance();
                if (position_ == source_.size()) error("Unterminated comment");
                advance(); advance();
                continue;
            }
            break;
        }
    }
    unsigned hex(unsigned count) {
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i) {
            const char c = peek();
            unsigned nibble = 0;
            if (c >= '0' && c <= '9') nibble = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = static_cast<unsigned>(c - 'A' + 10);
            else error("Invalid hexadecimal string escape");
            value = value * 16 + nibble;
            advance();
        }
        return value;
    }
    static void append_utf8(std::string& out, unsigned value) {
        if (value < 0x80) out.push_back(static_cast<char>(value));
        else if (value < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (value >> 6)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else if (value < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (value >> 12)));
            out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (value >> 18)));
            out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }
    std::string read_string() {
        const char quote = peek();
        advance();
        std::string out;
        while (position_ < source_.size() && peek() != quote) {
            const char c = peek();
            if (c == '\n' || c == '\r' || c == '\0') error("Invalid character in string literal");
            advance();
            if (c != '\\') out.push_back(c);
            else {
                if (position_ == source_.size()) error("Unterminated string escape");
                const char escaped = peek(); advance();
                switch (escaped) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'v': out.push_back('\v'); break;
                case '0': if (digit(peek())) error("Octal escapes are not supported"); out.push_back('\0'); break;
                case '\\': case '\'': case '"': case '/': out.push_back(escaped); break;
                case 'x': append_utf8(out, hex(2)); break;
                case 'u': {
                    unsigned code = hex(4);
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (peek() != '\\' || peek(1) != 'u') error("Unpaired Unicode surrogate");
                        advance(); advance();
                        const unsigned low = hex(4);
                        if (low < 0xdc00 || low > 0xdfff) error("Invalid Unicode surrogate pair");
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff) error("Unpaired Unicode surrogate");
                    append_utf8(out, code);
                    break;
                }
                default: error("Unsupported string escape");
                }
            }
            if (out.size() > string_limit) error("Script string limit exceeded");
        }
        if (position_ == source_.size()) error("Unterminated string literal");
        advance();
        return out;
    }
};

struct Undefined {};
struct Null {};
struct Element { std::string selector; };
struct Style { std::string selector; };
struct FunctionRef { std::size_t index{}; };
enum class Host { Console, Document, Math };
struct Native { std::string name, selector; };
using Value = std::variant<Undefined, Null, bool, double, std::string, Element, Style, FunctionRef, Host, Native>;

struct Expr {
    enum class Kind { Literal, Identifier, Unary, Binary, Assignment, Member, Call, Conditional, Update } kind{};
    std::string op;
    Value value;
    std::unique_ptr<Expr> left, right, third;
    std::vector<std::unique_ptr<Expr>> arguments;
    bool postfix{};
    std::size_t line{};
    std::size_t tree_depth{1};
};

struct Declaration { std::string name; std::unique_ptr<Expr> initializer; };
struct Stmt {
    enum class Kind { Empty, Expression, Block, Variables, If, While, For, Function, Return, Break, Continue } kind{};
    std::string name, declaration_kind;
    std::unique_ptr<Expr> expression, increment;
    std::unique_ptr<Stmt> first, second, initializer;
    std::vector<std::unique_ptr<Stmt>> statements;
    std::vector<Declaration> declarations;
    std::vector<std::string> parameters;
    std::size_t line{};
};

class DepthGuard {
public:
    DepthGuard(std::size_t& depth, std::size_t limit, const char* message) : depth_(depth) {
        if (depth >= limit) fail(message);
        ++depth_;
    }
    ~DepthGuard() { --depth_; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
private:
    std::size_t& depth_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    std::vector<std::unique_ptr<Stmt>> run() {
        std::vector<std::unique_ptr<Stmt>> result;
        while (peek().kind != Token::Kind::End) result.push_back(statement());
        return result;
    }

private:
    std::vector<Token> tokens_;
    std::size_t position_{}, depth_{}, nodes_{}, function_depth_{}, loop_depth_{};
    const Token& peek() const { return tokens_[position_]; }
    const Token& previous() const { return tokens_[position_ - 1]; }
    bool punctuation(std::string_view text) const {
        return peek().kind == Token::Kind::Punctuation && peek().text == text;
    }
    bool match(std::string_view text) {
        if (peek().text != text || peek().kind == Token::Kind::String || peek().kind == Token::Kind::End) return false;
        ++position_; return true;
    }
    void expect(std::string_view text) { if (!match(text)) error("Expected '" + std::string(text) + "'"); }
    [[noreturn]] void error(const std::string& message) const {
        fail(message + " at line " + std::to_string(peek().line) + ", byte " + std::to_string(peek().offset));
    }
    static bool reserved(std::string_view text) {
        static constexpr std::string_view names[] = {"let", "const", "var", "if", "else", "while", "for", "function", "return", "break", "continue", "true", "false", "null", "undefined", "new", "class", "this", "switch", "case", "throw", "try", "catch", "async", "await", "import", "export", "delete", "typeof", "void", "in", "instanceof", "do", "with", "yield"};
        return std::find(std::begin(names), std::end(names), text) != std::end(names);
    }
    std::string identifier() {
        if (peek().kind != Token::Kind::Identifier || reserved(peek().text)) error("Expected a variable name");
        return tokens_[position_++].text;
    }
    void count_node() { if (++nodes_ > node_limit) error("Script syntax-node limit exceeded"); }
    std::unique_ptr<Expr> expr_node(Expr::Kind kind) {
        count_node(); auto node = std::make_unique<Expr>(); node->kind = kind; node->line = peek().line; return node;
    }
    std::unique_ptr<Stmt> stmt_node(Stmt::Kind kind) {
        count_node(); auto node = std::make_unique<Stmt>(); node->kind = kind; node->line = peek().line; return node;
    }
    std::unique_ptr<Expr> checked(std::unique_ptr<Expr> node) {
        const auto include = [&node](const std::unique_ptr<Expr>& child) {
            if (child) node->tree_depth = std::max(node->tree_depth, child->tree_depth + 1);
        };
        include(node->left); include(node->right); include(node->third);
        for (const auto& argument : node->arguments) include(argument);
        if (node->tree_depth > syntax_depth_limit) error("Script expression-tree depth limit exceeded");
        return node;
    }
    void end_statement() {
        if (match(";") || peek().kind == Token::Kind::End || punctuation("}")) return;
        if (position_ > 0 && previous().line < peek().line) return;
        error("Expected statement separator");
    }
    std::unique_ptr<Stmt> variables(std::string kind) {
        auto node = stmt_node(Stmt::Kind::Variables); node->declaration_kind = std::move(kind);
        do {
            Declaration declaration{identifier(), {}};
            if (match("=")) declaration.initializer = assignment();
            else if (node->declaration_kind == "const") error("const requires an initializer");
            node->declarations.push_back(std::move(declaration));
            if (node->declarations.size() > 256) error("Declaration-list limit exceeded");
        } while (match(","));
        return node;
    }
    std::unique_ptr<Stmt> statement() {
        DepthGuard depth(depth_, syntax_depth_limit, "Script syntax nesting limit exceeded");
        if (match(";")) return stmt_node(Stmt::Kind::Empty);
        if (match("{")) {
            auto node = stmt_node(Stmt::Kind::Block);
            while (!punctuation("}")) {
                if (peek().kind == Token::Kind::End) error("Unterminated block");
                node->statements.push_back(statement());
            }
            expect("}"); return node;
        }
        if (match("let") || match("const") || match("var")) {
            const auto kind = previous().text;
            auto node = variables(kind); end_statement(); return node;
        }
        if (match("if")) {
            auto node = stmt_node(Stmt::Kind::If);
            expect("("); node->expression = assignment(); expect(")"); node->first = statement();
            if (match("else")) node->second = statement();
            return node;
        }
        if (match("while")) {
            auto node = stmt_node(Stmt::Kind::While);
            expect("("); node->expression = assignment(); expect(")");
            ++loop_depth_; node->first = statement(); --loop_depth_; return node;
        }
        if (match("for")) {
            auto node = stmt_node(Stmt::Kind::For); expect("(");
            if (!match(";")) {
                if (match("let") || match("const") || match("var")) node->initializer = variables(previous().text);
                else { node->initializer = stmt_node(Stmt::Kind::Expression); node->initializer->expression = assignment(); }
                expect(";");
            }
            if (!match(";")) { node->expression = assignment(); expect(";"); }
            if (!match(")")) { node->increment = assignment(); expect(")"); }
            ++loop_depth_; node->first = statement(); --loop_depth_; return node;
        }
        if (match("function")) {
            auto node = stmt_node(Stmt::Kind::Function); node->name = identifier(); expect("(");
            if (!match(")")) {
                do {
                    auto name = identifier();
                    if (std::find(node->parameters.begin(), node->parameters.end(), name) != node->parameters.end()) error("Duplicate function parameter");
                    node->parameters.push_back(std::move(name));
                    if (node->parameters.size() > 32) error("Function parameter limit exceeded");
                } while (match(","));
                expect(")");
            }
            if (!punctuation("{")) error("Function requires a block body");
            const auto old_loop_depth = loop_depth_; loop_depth_ = 0; ++function_depth_;
            node->first = statement(); --function_depth_; loop_depth_ = old_loop_depth;
            return node;
        }
        if (match("return")) {
            if (function_depth_ == 0) error("return outside a function");
            const auto line = previous().line;
            auto node = stmt_node(Stmt::Kind::Return);
            if (peek().line == line && peek().kind != Token::Kind::End && !punctuation(";") && !punctuation("}")) node->expression = assignment();
            end_statement(); return node;
        }
        if (match("break") || match("continue")) {
            const bool is_break = previous().text == "break";
            if (loop_depth_ == 0) error("Loop control outside a loop");
            auto node = stmt_node(is_break ? Stmt::Kind::Break : Stmt::Kind::Continue); end_statement(); return node;
        }
        auto node = stmt_node(Stmt::Kind::Expression); node->expression = assignment(); end_statement(); return node;
    }
    static bool assignable(const Expr& expression) {
        return expression.kind == Expr::Kind::Identifier || expression.kind == Expr::Kind::Member;
    }
    std::unique_ptr<Expr> assignment() {
        DepthGuard depth(depth_, syntax_depth_limit, "Script syntax nesting limit exceeded");
        auto left = conditional();
        if (match("=") || match("+=") || match("-=") || match("*=") || match("/=") || match("%=")) {
            if (!assignable(*left)) error("Invalid assignment target");
            auto node = expr_node(Expr::Kind::Assignment); node->op = previous().text; node->left = std::move(left); node->right = assignment(); return checked(std::move(node));
        }
        return left;
    }
    std::unique_ptr<Expr> conditional() {
        auto left = binary(1);
        if (match("?")) {
            auto node = expr_node(Expr::Kind::Conditional); node->left = std::move(left); node->right = assignment(); expect(":"); node->third = assignment(); return checked(std::move(node));
        }
        return left;
    }
    static int precedence(std::string_view op) {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "==" || op == "!=" || op == "===" || op == "!==") return 3;
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
        if (op == "+" || op == "-") return 5;
        if (op == "*" || op == "/" || op == "%") return 6;
        return 0;
    }
    std::unique_ptr<Expr> binary(int minimum) {
        DepthGuard depth(depth_, syntax_depth_limit, "Script syntax nesting limit exceeded");
        auto left = unary();
        while (peek().kind == Token::Kind::Punctuation && precedence(peek().text) >= minimum) {
            const auto op = tokens_[position_++].text;
            auto node = expr_node(Expr::Kind::Binary); node->op = op; node->left = std::move(left); node->right = binary(precedence(op) + 1); left = checked(std::move(node));
        }
        return left;
    }
    std::unique_ptr<Expr> unary() {
        DepthGuard depth(depth_, syntax_depth_limit, "Script syntax nesting limit exceeded");
        if (match("!") || match("+") || match("-")) {
            auto node = expr_node(Expr::Kind::Unary); node->op = previous().text; node->left = unary(); return checked(std::move(node));
        }
        if (match("++") || match("--")) {
            auto node = expr_node(Expr::Kind::Update); node->op = previous().text; node->left = unary();
            if (!assignable(*node->left)) error("Invalid increment target");
            return checked(std::move(node));
        }
        return postfix();
    }
    std::unique_ptr<Expr> postfix() {
        auto left = primary();
        std::size_t chain = 0;
        while (true) {
            if (++chain > syntax_depth_limit) error("Script member-chain limit exceeded");
            if (match(".")) {
                if (peek().kind != Token::Kind::Identifier) error("Expected member name");
                auto node = expr_node(Expr::Kind::Member); node->left = std::move(left); node->right = expr_node(Expr::Kind::Literal); node->right->value = tokens_[position_++].text; left = checked(std::move(node));
            } else if (match("[")) {
                auto node = expr_node(Expr::Kind::Member); node->left = std::move(left); node->right = assignment(); expect("]"); left = checked(std::move(node));
            } else if (match("(")) {
                auto node = expr_node(Expr::Kind::Call); node->left = std::move(left);
                if (!match(")")) {
                    do {
                        node->arguments.push_back(assignment());
                        if (node->arguments.size() > 32) error("Call argument limit exceeded");
                    } while (match(","));
                    expect(")");
                }
                left = checked(std::move(node));
            } else if ((punctuation("++") || punctuation("--")) && previous().line == peek().line) {
                if (!assignable(*left)) error("Invalid increment target");
                auto node = expr_node(Expr::Kind::Update); node->op = tokens_[position_++].text; node->postfix = true; node->left = std::move(left); left = checked(std::move(node));
                break;
            } else break;
        }
        return left;
    }
    std::unique_ptr<Expr> primary() {
        if (match("(")) { auto expression = assignment(); expect(")"); return expression; }
        if (peek().kind == Token::Kind::String) {
            auto node = expr_node(Expr::Kind::Literal); node->value = tokens_[position_++].text; return node;
        }
        if (peek().kind == Token::Kind::Number) {
            auto node = expr_node(Expr::Kind::Literal); const auto token = tokens_[position_++];
            double number = 0;
            const auto parsed = std::from_chars(token.text.data(), token.text.data() + token.text.size(), number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != token.text.data() + token.text.size() || !std::isfinite(number)) error("Numeric literal is not finite or valid");
            node->value = number; return node;
        }
        if (match("true") || match("false") || match("null") || match("undefined")) {
            auto node = expr_node(Expr::Kind::Literal); const auto token = previous().text;
            if (token == "true" || token == "false") node->value = token == "true";
            else if (token == "null") node->value = Null{};
            else node->value = Undefined{};
            return node;
        }
        if (peek().kind == Token::Kind::Identifier && !reserved(peek().text)) {
            auto node = expr_node(Expr::Kind::Identifier); node->op = tokens_[position_++].text; return node;
        }
        error("Unsupported or missing expression");
    }
};

struct Binding { Value value; bool constant{}, var{}, initialized{true}; };
struct Scope {
    std::unordered_map<std::string, Binding> bindings;
    std::shared_ptr<Scope> parent;
    bool function_scope{};
};
struct Function { const Stmt* declaration{}; std::shared_ptr<Scope> closure; };
struct Flow {
    enum class Kind { Normal, Return, Break, Continue } kind{};
    Value value;
};
struct Reference { std::shared_ptr<Scope> scope; std::string name, selector, property; };

class Interpreter {
public:
    explicit Interpreter(std::size_t budget) : budget_(std::min<std::size_t>(budget, 1'000'000)) {
        global_ = std::make_shared<Scope>(); global_->function_scope = true;
        define(global_, "console", Host::Console, true);
        define(global_, "document", Host::Document, true);
        define(global_, "Math", Host::Math, true);
        define(global_, "String", Native{"String", {}}, true);
        define(global_, "Number", Native{"Number", {}}, true);
        define(global_, "Boolean", Native{"Boolean", {}}, true);
    }
    Result run(const std::vector<std::unique_ptr<Stmt>>& program) {
        for (const auto& statement : program) {
            if (exec(*statement, global_).kind != Flow::Kind::Normal) fail("Unexpected control flow at script root");
        }
        result_.success = true; return std::move(result_);
    }

private:
    std::size_t budget_, steps_{}, depth_{}, call_depth_{}, bindings_{}, output_bytes_{};
    Result result_;
    std::shared_ptr<Scope> global_;
    std::vector<Function> functions_;
    void tick(std::size_t work = 1) {
        if (work > budget_ - steps_) fail("Script instruction budget exceeded");
        steps_ += work;
    }
    void charge(const Value& value) {
        if (const auto string = std::get_if<std::string>(&value)) tick(string->size() / 64 + 1);
    }
    static Value number(double value) {
        if (!std::isfinite(value)) fail("Script numeric result is not finite");
        return value;
    }
    static bool truthy(const Value& value) {
        if (std::holds_alternative<Undefined>(value) || std::holds_alternative<Null>(value)) return false;
        if (const auto boolean = std::get_if<bool>(&value)) return *boolean;
        if (const auto numeric = std::get_if<double>(&value)) return *numeric != 0;
        if (const auto string = std::get_if<std::string>(&value)) return !string->empty();
        return true;
    }
    static std::string stringify(const Value& value) {
        if (std::holds_alternative<Undefined>(value)) return "undefined";
        if (std::holds_alternative<Null>(value)) return "null";
        if (const auto boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
        if (const auto numeric = std::get_if<double>(&value)) {
            if (*numeric == 0) return "0";
            std::ostringstream stream; stream.imbue(std::locale::classic()); stream << std::setprecision(15) << *numeric; return stream.str();
        }
        if (const auto string = std::get_if<std::string>(&value)) return *string;
        if (std::holds_alternative<Element>(value)) return "[Element]";
        if (std::holds_alternative<Style>(value)) return "[Style]";
        if (std::holds_alternative<FunctionRef>(value) || std::holds_alternative<Native>(value)) return "[Function]";
        return "[Host]";
    }
    static std::size_t utf16_length(std::string_view string) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < string.size();) {
            const auto first = static_cast<unsigned char>(string[i]);
            unsigned length = 1, code = first;
            if (first >= 0xc2 && first <= 0xdf) { length = 2; code = first & 0x1f; }
            else if (first >= 0xe0 && first <= 0xef) { length = 3; code = first & 0x0f; }
            else if (first >= 0xf0 && first <= 0xf4) { length = 4; code = first & 0x07; }
            else if (first >= 0x80) fail("String is not valid UTF-8");
            if (length > string.size() - i) fail("String is not valid UTF-8");
            for (unsigned j = 1; j < length; ++j) {
                const auto next = static_cast<unsigned char>(string[i + j]);
                if ((next & 0xc0) != 0x80) fail("String is not valid UTF-8");
                code = (code << 6) | (next & 0x3f);
            }
            if ((length == 2 && code < 0x80) || (length == 3 && code < 0x800) ||
                (length == 4 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) fail("String is not valid UTF-8");
            count += code >= 0x10000 ? 2 : 1;
            i += length;
        }
        return count;
    }
    static double numeric(const Value& value) {
        if (const auto number = std::get_if<double>(&value)) return *number;
        if (const auto boolean = std::get_if<bool>(&value)) return *boolean ? 1 : 0;
        if (std::holds_alternative<Null>(value)) return 0;
        if (const auto string = std::get_if<std::string>(&value)) {
            std::string_view trimmed = *string;
            while (!trimmed.empty() && space(trimmed.front())) trimmed.remove_prefix(1);
            while (!trimmed.empty() && space(trimmed.back())) trimmed.remove_suffix(1);
            if (trimmed.empty()) return 0;
            if (trimmed.front() == '+') trimmed.remove_prefix(1);
            double result = 0;
            const auto parsed = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), result, std::chars_format::general);
            if (parsed.ec == std::errc{} && parsed.ptr == trimmed.data() + trimmed.size() && std::isfinite(result)) return result;
        }
        fail("Value cannot be converted to a finite number");
    }
    static bool strict_equal(const Value& left, const Value& right) {
        if (left.index() != right.index()) return false;
        if (std::holds_alternative<Null>(left) || std::holds_alternative<Undefined>(left)) return true;
        if (const auto value = std::get_if<bool>(&left)) return *value == std::get<bool>(right);
        if (const auto value = std::get_if<double>(&left)) return *value == std::get<double>(right);
        if (const auto value = std::get_if<std::string>(&left)) return *value == std::get<std::string>(right);
        if (const auto value = std::get_if<FunctionRef>(&left)) return value->index == std::get<FunctionRef>(right).index;
        if (const auto value = std::get_if<Element>(&left)) return value->selector == std::get<Element>(right).selector;
        if (const auto value = std::get_if<Style>(&left)) return value->selector == std::get<Style>(right).selector;
        if (const auto value = std::get_if<Host>(&left)) return *value == std::get<Host>(right);
        if (const auto value = std::get_if<Native>(&left)) { const auto& other = std::get<Native>(right); return value->name == other.name && value->selector == other.selector; }
        return false;
    }
    static bool loose_equal(const Value& left, const Value& right) {
        if (left.index() == right.index()) return strict_equal(left, right);
        const auto nullish = [](const Value& value) { return std::holds_alternative<Null>(value) || std::holds_alternative<Undefined>(value); };
        if (nullish(left) || nullish(right)) return nullish(left) && nullish(right);
        const auto scalar = [](const Value& value) { return std::holds_alternative<double>(value) || std::holds_alternative<bool>(value) || std::holds_alternative<std::string>(value); };
        if (scalar(left) && scalar(right)) {
            try { return numeric(left) == numeric(right); } catch (const std::runtime_error&) { return false; }
        }
        return false;
    }
    static Value calculate(std::string_view op, const Value& left, const Value& right) {
        if (op == "+" && (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right))) {
            auto first = stringify(left); auto second = stringify(right);
            if (first.size() > string_limit - second.size()) fail("Script string limit exceeded");
            first += second; return first;
        }
        if (op == "===" || op == "!==") return strict_equal(left, right) != (op == "!==");
        if (op == "==" || op == "!=") return loose_equal(left, right) != (op == "!=");
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (const auto a = std::get_if<std::string>(&left); a && std::holds_alternative<std::string>(right)) {
                const auto& b = std::get<std::string>(right);
                if (op == "<") return *a < b;
                if (op == ">") return *a > b;
                if (op == "<=") return *a <= b;
                return *a >= b;
            }
            const auto a = numeric(left), b = numeric(right);
            if (op == "<") return a < b;
            if (op == ">") return a > b;
            if (op == "<=") return a <= b;
            return a >= b;
        }
        const auto a = numeric(left), b = numeric(right);
        if (op == "+") return number(a + b);
        if (op == "-") return number(a - b);
        if (op == "*") return number(a * b);
        if ((op == "/" || op == "%") && b == 0) fail("Division by zero in script");
        if (op == "/") return number(a / b);
        if (op == "%") return number(std::fmod(a, b));
        fail("Unsupported binary operation");
    }
    std::shared_ptr<Scope> child(const std::shared_ptr<Scope>& parent, bool function_scope = false) {
        auto scope = std::make_shared<Scope>(); scope->parent = parent; scope->function_scope = function_scope; return scope;
    }
    void define(const std::shared_ptr<Scope>& scope, const std::string& name, Value value, bool constant, bool var = false, bool initialized = true) {
        const auto existing = scope->bindings.find(name);
        if (existing != scope->bindings.end()) {
            if (var && existing->second.var && !existing->second.constant) { existing->second.value = std::move(value); return; }
            fail("Duplicate binding: " + name);
        }
        if (++bindings_ > binding_limit) fail("Script binding limit exceeded");
        scope->bindings.emplace(name, Binding{std::move(value), constant, var, initialized});
    }
    Reference variable_reference(const std::shared_ptr<Scope>& scope, const std::string& name) const {
        for (auto current = scope; current; current = current->parent) {
            if (current->bindings.find(name) != current->bindings.end()) return {current, name, {}, {}};
        }
        fail("Undefined variable: " + name);
    }
    static std::string css_name(std::string_view value) {
        if (value.empty() || value.size() > 128) fail("Invalid style property");
        std::string result;
        for (const char c : value) {
            if (c >= 'A' && c <= 'Z') { result.push_back('-'); result.push_back(static_cast<char>(c - 'A' + 'a')); }
            else if ((c >= 'a' && c <= 'z') || c == '-' || digit(c)) result.push_back(c);
            else fail("Invalid style property");
        }
        static constexpr std::string_view names[] = {"color", "background", "background-color", "font-size", "font-weight", "display",
            "margin", "margin-top", "margin-right", "margin-bottom", "margin-left", "padding", "padding-top",
            "padding-right", "padding-bottom", "padding-left", "width", "max-width"};
        if (std::find(std::begin(names), std::end(names), result) == std::end(names)) fail("Unsupported style property: " + result);
        return result;
    }
    static std::string style_value(std::string_view property, std::string_view value) {
        while (!value.empty() && space(value.front())) value.remove_prefix(1);
        while (!value.empty() && space(value.back())) value.remove_suffix(1);
        if (value.empty() || value.size() > 4'096) fail("Empty or oversized style value");
        std::string normalized;
        normalized.reserve(value.size());
        for (const char c : value) {
            const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!alpha && !digit(c) && c != '#' && c != '.' && c != '%' && c != '+' && c != '-' && c != ' ' && c != ',' && c != '(' && c != ')') fail("Unsafe style value");
            normalized.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        }
        if (property == "display" && normalized != "none" && normalized != "block" && normalized != "inline" && normalized != "inline-block") fail("Unsupported display value");
        if (normalized.find_first_of("()") != std::string::npos) {
            const bool color = property == "color" || property == "background" || property == "background-color";
            if (!color || (!normalized.starts_with("rgb(") && !normalized.starts_with("rgba(")) || normalized.back() != ')') fail("Unsupported or unsafe CSS function");
        }
        return std::string(value);
    }
    static bool allowed_attribute(std::string_view name) {
        static constexpr std::string_view names[] = {"class", "title", "lang", "dir", "role", "hidden", "aria-label", "aria-hidden"};
        return std::find(std::begin(names), std::end(names), name) != std::end(names);
    }
    static void validate_selector(std::string_view selector) {
        if (selector.empty() || selector.size() > 1'024 || selector.find('\0') != std::string_view::npos) fail("Invalid or oversized DOM selector");
        if (selector.front() != '#') fail("Only one #id selector is supported");
        selector.remove_prefix(1);
        if (selector.empty() || !std::all_of(selector.begin(), selector.end(), [](char c) { return identifier_part(c) || c == '-'; })) fail("Only one #id selector is supported");
    }
    Value property(const Value& object, const std::string& key) {
        tick();
        if (const auto host = std::get_if<Host>(&object)) {
            if (*host == Host::Console && key == "log") return Native{"console.log", {}};
            if (*host == Host::Document && (key == "querySelector" || key == "getElementById")) return Native{"document." + key, {}};
            if (*host == Host::Math) {
                if (key == "PI") return 3.14159265358979323846;
                if (key == "E") return 2.71828182845904523536;
                if (key == "floor" || key == "ceil" || key == "round" || key == "abs" || key == "min" || key == "max" || key == "sqrt" || key == "pow") return Native{"Math." + key, {}};
            }
            fail("Unsupported host property: " + key);
        }
        if (const auto element = std::get_if<Element>(&object)) {
            if (key == "style") return Style{element->selector};
            if (key == "setAttribute") return Native{"element.setAttribute", element->selector};
            if (key == "textContent") return read({{}, {}, element->selector, "textContent"});
            if (key == "className") return read({{}, {}, element->selector, "attribute.class"});
            fail("Unsupported element property: " + key);
        }
        if (const auto style = std::get_if<Style>(&object)) {
            if (key == "setProperty") return Native{"style.setProperty", style->selector};
            return read({{}, {}, style->selector, "style." + css_name(key)});
        }
        if (const auto string = std::get_if<std::string>(&object); string && key == "length") return static_cast<double>(utf16_length(*string));
        fail("Property access is not supported for this value");
    }
    Reference reference(const Expr& expression, const std::shared_ptr<Scope>& scope) {
        tick();
        if (expression.kind == Expr::Kind::Identifier) return variable_reference(scope, expression.op);
        if (expression.kind != Expr::Kind::Member) fail("Invalid assignment reference");
        const auto object = eval(*expression.left, scope);
        const auto key = stringify(eval(*expression.right, scope));
        if (const auto element = std::get_if<Element>(&object)) {
            if (key == "textContent") return {{}, {}, element->selector, "textContent"};
            if (key == "className") return {{}, {}, element->selector, "attribute.class"};
            fail("Unsupported or unsafe element assignment: " + key);
        }
        if (const auto style = std::get_if<Style>(&object)) return {{}, {}, style->selector, "style." + css_name(key)};
        fail("Only variables and supported DOM properties are assignable");
    }
    Value read(const Reference& reference) {
        if (reference.scope) {
            const auto& binding = reference.scope->bindings.at(reference.name);
            if (!binding.initialized) fail("Cannot read a binding before initialization: " + reference.name);
            const auto& value = binding.value;
            charge(value);
            return value;
        }
        for (auto item = result_.mutations.rbegin(); item != result_.mutations.rend(); ++item) {
            tick();
            if (item->selector == reference.selector && item->property == reference.property) {
                tick(item->value.size() / 64 + 1);
                return item->value;
            }
        }
        fail("Initial DOM reads are unsupported: " + reference.property);
    }
    void reserve_output(std::size_t bytes) {
        if (bytes > output_limit - output_bytes_) fail("Script output-size limit exceeded");
        output_bytes_ += bytes;
    }
    void write(const Reference& reference, Value value) {
        if (reference.scope) {
            auto& binding = reference.scope->bindings.at(reference.name);
            if (!binding.initialized) fail("Cannot write a binding before initialization: " + reference.name);
            if (binding.constant) fail("Cannot assign a const binding: " + reference.name);
            binding.value = std::move(value); return;
        }
        if (result_.mutations.size() >= mutation_limit) fail("Script DOM-mutation limit exceeded");
        auto text = stringify(value);
        if (reference.property.starts_with("attribute.") && text.size() > 4'096) fail("Script attribute-value limit exceeded");
        if (reference.property.starts_with("style.")) text = style_value(std::string_view(reference.property).substr(6), text);
        reserve_output(reference.selector.size() + reference.property.size() + text.size());
        result_.mutations.push_back({reference.selector, reference.property, std::move(text)});
    }
    static void arguments_count(const std::vector<Value>& args, std::size_t count, std::string_view name) {
        if (args.size() != count) fail(std::string(name) + " requires " + std::to_string(count) + " argument(s)");
    }
    Value native_call(const Native& native, const std::vector<Value>& args) {
        tick();
        if (native.name == "console.log") {
            if (result_.console.size() >= output_entry_limit) fail("Script console-entry limit exceeded");
            std::string line;
            bool first = true;
            for (const auto& arg : args) {
                const auto text = stringify(arg);
                if (text.size() + (first ? 0 : 1) > string_limit - line.size()) fail("Script console-line limit exceeded");
                if (!first) line.push_back(' ');
                line += text;
                first = false;
            }
            reserve_output(line.size()); result_.console.push_back(std::move(line)); return Undefined{};
        }
        if (native.name == "String" || native.name == "Number" || native.name == "Boolean") {
            if (args.size() > 1) fail(native.name + " accepts at most one argument");
            if (native.name == "String") return args.empty() ? std::string{} : stringify(args[0]);
            if (native.name == "Boolean") return !args.empty() && truthy(args[0]);
            return args.empty() ? Value{0.0} : number(numeric(args[0]));
        }
        if (native.name == "document.querySelector" || native.name == "document.getElementById") {
            arguments_count(args, 1, native.name); auto selector = stringify(args[0]);
            if (native.name == "document.getElementById") selector.insert(selector.begin(), '#');
            validate_selector(selector); return Element{std::move(selector)};
        }
        if (native.name == "element.setAttribute") {
            arguments_count(args, 2, native.name); const auto key = stringify(args[0]);
            if (!allowed_attribute(key)) fail("Unsupported or unsafe attribute: " + key);
            write({{}, {}, native.selector, "attribute." + key}, args[1]); return Undefined{};
        }
        if (native.name == "style.setProperty") {
            arguments_count(args, 2, native.name); write({{}, {}, native.selector, "style." + css_name(stringify(args[0]))}, args[1]); return Undefined{};
        }
        if (native.name.substr(0, 5) == "Math.") {
            const auto operation = native.name.substr(5);
            if (operation == "min" || operation == "max") {
                if (args.empty()) fail(native.name + " requires at least one argument");
                auto result = numeric(args[0]);
                for (std::size_t i = 1; i < args.size(); ++i) result = operation == "min" ? std::min(result, numeric(args[i])) : std::max(result, numeric(args[i]));
                return number(result);
            }
            arguments_count(args, operation == "pow" ? 2 : 1, native.name);
            const auto value = numeric(args[0]);
            if (operation == "floor") return number(std::floor(value));
            if (operation == "ceil") return number(std::ceil(value));
            if (operation == "round") return number(std::floor(value + 0.5));
            if (operation == "abs") return number(std::abs(value));
            if (operation == "sqrt") return number(std::sqrt(value));
            if (operation == "pow") return number(std::pow(value, numeric(args[1])));
        }
        fail("Unsupported native function");
    }
    Value call(const Value& callee, const std::vector<Value>& args) {
        if (const auto native = std::get_if<Native>(&callee)) return native_call(*native, args);
        const auto reference = std::get_if<FunctionRef>(&callee);
        if (!reference) fail("Value is not callable");
        DepthGuard depth(call_depth_, 32, "Script function-call depth limit exceeded");
        // Copy: nested declarations may reallocate the function registry.
        const auto function = functions_.at(reference->index);
        auto scope = child(function.closure, true);
        for (std::size_t i = 0; i < function.declaration->parameters.size(); ++i) {
            define(scope, function.declaration->parameters[i], i < args.size() ? args[i] : Value{Undefined{}}, false);
        }
        const auto result = exec(*function.declaration->first, scope);
        if (result.kind == Flow::Kind::Break || result.kind == Flow::Kind::Continue) fail("Loop control escaped a function");
        return result.kind == Flow::Kind::Return ? result.value : Value{Undefined{}};
    }
    Value eval(const Expr& expression, const std::shared_ptr<Scope>& scope) {
        tick(); DepthGuard depth(depth_, syntax_depth_limit, "Script evaluation nesting limit exceeded");
        switch (expression.kind) {
        case Expr::Kind::Literal: charge(expression.value); return expression.value;
        case Expr::Kind::Identifier: return read(variable_reference(scope, expression.op));
        case Expr::Kind::Unary: {
            const auto value = eval(*expression.left, scope);
            if (expression.op == "!") return !truthy(value);
            return number(expression.op == "-" ? -numeric(value) : numeric(value));
        }
        case Expr::Kind::Binary: {
            const auto left = eval(*expression.left, scope);
            if (expression.op == "&&") return truthy(left) ? eval(*expression.right, scope) : left;
            if (expression.op == "||") return truthy(left) ? left : eval(*expression.right, scope);
            return calculate(expression.op, left, eval(*expression.right, scope));
        }
        case Expr::Kind::Conditional: return eval(truthy(eval(*expression.left, scope)) ? *expression.right : *expression.third, scope);
        case Expr::Kind::Assignment: {
            const auto target = reference(*expression.left, scope);
            // Compound assignments read the target before evaluating the RHS.
            const auto old = expression.op == "=" ? Value{Undefined{}} : read(target);
            const auto right = eval(*expression.right, scope);
            const auto value = expression.op == "=" ? right : calculate(expression.op.substr(0, 1), old, right);
            write(target, value); return value;
        }
        case Expr::Kind::Update: {
            const auto target = reference(*expression.left, scope);
            const auto old = numeric(read(target));
            const auto updated = number(old + (expression.op == "++" ? 1 : -1));
            write(target, updated); return expression.postfix ? Value{old} : updated;
        }
        case Expr::Kind::Member: {
            const auto object = eval(*expression.left, scope);
            return property(object, stringify(eval(*expression.right, scope)));
        }
        case Expr::Kind::Call: {
            const auto callee = eval(*expression.left, scope);
            std::vector<Value> args; args.reserve(expression.arguments.size());
            for (const auto& argument : expression.arguments) args.push_back(eval(*argument, scope));
            return call(callee, args);
        }
        }
        fail("Invalid script expression");
    }
    Flow exec(const Stmt& statement, const std::shared_ptr<Scope>& scope) {
        tick(); DepthGuard depth(depth_, syntax_depth_limit, "Script evaluation nesting limit exceeded");
        switch (statement.kind) {
        case Stmt::Kind::Empty: return {};
        case Stmt::Kind::Expression: eval(*statement.expression, scope); return {};
        case Stmt::Kind::Block: {
            const auto nested = child(scope);
            for (const auto& item : statement.statements) {
                auto flow = exec(*item, nested); if (flow.kind != Flow::Kind::Normal) return flow;
            }
            return {};
        }
        case Stmt::Kind::Variables: {
            auto target = scope;
            const bool var = statement.declaration_kind == "var";
            if (var) while (target->parent && !target->function_scope) target = target->parent;
            for (const auto& item : statement.declarations) {
                // var x; does not erase an existing var binding.
                if (var && !item.initializer && target->bindings.find(item.name) != target->bindings.end()) {
                    if (!target->bindings.at(item.name).var) fail("Duplicate binding: " + item.name);
                    continue;
                }
                if (var) define(target, item.name, item.initializer ? eval(*item.initializer, scope) : Value{Undefined{}}, false, true);
                else {
                    define(target, item.name, Undefined{}, statement.declaration_kind == "const", false, false);
                    const auto value = item.initializer ? eval(*item.initializer, scope) : Value{Undefined{}};
                    auto& binding = target->bindings.at(item.name);
                    binding.value = value; binding.initialized = true;
                }
            }
            return {};
        }
        case Stmt::Kind::If:
            if (truthy(eval(*statement.expression, scope))) return exec(*statement.first, scope);
            return statement.second ? exec(*statement.second, scope) : Flow{};
        case Stmt::Kind::While:
            while (truthy(eval(*statement.expression, scope))) {
                auto flow = exec(*statement.first, scope);
                if (flow.kind == Flow::Kind::Return) return flow;
                if (flow.kind == Flow::Kind::Break) break;
            }
            return {};
        case Stmt::Kind::For: {
            auto nested = child(scope);
            if (statement.initializer) exec(*statement.initializer, nested);
            while (true) {
                tick();
                if (statement.expression && !truthy(eval(*statement.expression, nested))) break;
                auto flow = exec(*statement.first, nested);
                if (flow.kind == Flow::Kind::Return) return flow;
                if (flow.kind == Flow::Kind::Break) break;
                // Preserve the value captured by closures from this iteration.
                if (statement.initializer && statement.initializer->kind == Stmt::Kind::Variables && statement.initializer->declaration_kind != "var") {
                    auto next = child(scope);
                    for (const auto& declaration : statement.initializer->declarations) {
                        const auto& binding = nested->bindings.at(declaration.name);
                        charge(binding.value);
                        define(next, declaration.name, binding.value, binding.constant);
                    }
                    nested = std::move(next);
                }
                if (statement.increment) eval(*statement.increment, nested);
            }
            return {};
        }
        case Stmt::Kind::Function:
            if (functions_.size() >= function_limit) fail("Script function-definition limit exceeded");
            define(scope, statement.name, FunctionRef{functions_.size()}, false);
            functions_.push_back({&statement, scope}); return {};
        case Stmt::Kind::Return: return {Flow::Kind::Return, statement.expression ? eval(*statement.expression, scope) : Value{Undefined{}}};
        case Stmt::Kind::Break: return {Flow::Kind::Break, Undefined{}};
        case Stmt::Kind::Continue: return {Flow::Kind::Continue, Undefined{}};
        }
        fail("Invalid script statement");
    }
};

} // namespace

Result execute(std::string_view source, std::size_t instruction_budget) {
    try {
        if (source.size() > source_limit) fail("Script source-size limit exceeded");
        Lexer lexer(source);
        Parser parser(lexer.run());
        const auto program = parser.run();
        Interpreter interpreter(instruction_budget);
        return interpreter.run(program);
    } catch (const std::exception& error) {
        Result result;
        result.diagnostics.push_back(error.what());
        return result;
    }
}

} // namespace causalis::script
