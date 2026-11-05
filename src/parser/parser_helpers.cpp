#include "parser/parser_impl.h"
#include "parser/parser_utils.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace pmake::parser {

// Инициализация статических счётчиков
int ParserImpl::operationCounter = 0;
int ParserImpl::includeDepth = 0;
int ParserImpl::functionCallDepth = 0;

// ---- Конструктор/деструктор ----
ParserImpl::ParserImpl(std::string_view data, std::string_view filename,
                       std::string_view config, std::string_view platform)
    : lexer_(data, filename), filename_(filename),
      config_(config), platform_(platform) {
    state_.setGlobal("config", config_);
    state_.setGlobal("platform", platform_);
    state_.setGlobal("os",
#ifdef _WIN32
        "windows"
#elif __linux__
        "linux"
#elif __APPLE__
        "darwin"
#else
        "unknown"
#endif
    );
    root_ = new AstNode();
    root_->type = AstNode::BLOCK;
    astStack_.push_back(root_);
    operationCounter = 0;
    includeDepth = 0;
    functionCallDepth = 0;
}

ParserImpl::~ParserImpl() {
    delete root_;
}

// ---- Вспомогательные методы ----
void ParserImpl::advance() {
    try {
        cur_ = lexer_.nextToken();
        currentColumn_ = cur_.column;
    } catch (const std::exception& e) {
        cur_ = {TOK_EOF, -1, lexer_.line(), lexer_.column(), 0};
        currentColumn_ = cur_.column;
        hasError_ = true;
        errors_.push_back(std::format("Lexer error: {}", e.what()));
        std::println(std::cerr, "{}", errors_.back());
    } catch (...) {
        cur_ = {TOK_EOF, -1, lexer_.line(), lexer_.column(), 0};
        currentColumn_ = cur_.column;
        hasError_ = true;
        errors_.push_back("Unknown lexer error");
        std::println(std::cerr, "{}", errors_.back());
    }
}

bool ParserImpl::match(TokenType t) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    return false;
}

bool ParserImpl::expect(TokenType t, std::string_view msg) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    error(msg);
    return false;
}

void ParserImpl::error(std::string_view msg, bool recover) {
    static std::string lastError;
    static int lastLine = -1;
    if (lastError == msg && lastLine == cur_.line) {
        return;
    }
    lastError = std::string(msg);
    lastLine = cur_.line;

    std::string err = formatError(filename_, cur_.line, currentColumn_, msg, lexer_.str(cur_.strId));
    errors_.push_back(err);
    std::println(std::cerr, "{}", err);
    hasError_ = true;
    if (!recover) {
        throw std::runtime_error(err);
    }
}

std::string_view ParserImpl::str(int id) const {
    return lexer_.str(id);
}

void ParserImpl::skipNewlines() {
    while (cur_.type == TOK_NEWLINE) advance();
}

void ParserImpl::skipNewlinesAndIndents() {
    while (cur_.type == TOK_NEWLINE || cur_.type == TOK_INDENT) advance();
}

bool ParserImpl::skipToBlockStart(int baseIndent) {
    int loopGuard = 0;
    while (true) {
        if (++loopGuard > 10000) {
            error("Infinite loop in skipToBlockStart", true);
            return false;
        }
        if (cur_.type == TOK_NEWLINE) {
            advance();
            continue;
        }
        if (cur_.type == TOK_INDENT) {
            if (cur_.indent > baseIndent) return true;
            return false;
        }
        if (cur_.type == TOK_LBRACE) {
            return true;
        }
        if (cur_.type == TOK_EOF) {
            error("Unexpected EOF while looking for block", true);
            return false;
        }
        break;
    }
    return false;
}

AstNode* ParserImpl::currentBlock() {
    return astStack_.empty() ? root_ : astStack_.back();
}

void ParserImpl::pushBlock(AstNode* node) {
    astStack_.push_back(node);
}

void ParserImpl::popBlock() {
    if (!astStack_.empty()) astStack_.pop_back();
}

// ---- Создание узлов AST ----
AstNode* ParserImpl::makeStringLiteral(std::string_view value) {
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    auto node = new AstNode();
    node->type = AstNode::STRING_LITERAL;
    node->value = Value(std::string(value));
    node->line = cur_.line;
    node->column = cur_.column;
    return node;
}

AstNode* ParserImpl::makeNumberLiteral(double value) {
    auto node = new AstNode();
    node->type = AstNode::NUMBER_LITERAL;
    node->value = Value(value);
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeBoolLiteral(bool value) {
    auto node = new AstNode();
    node->type = AstNode::BOOL_LITERAL;
    node->value = Value(value);
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeNullLiteral() {
    auto node = new AstNode();
    node->type = AstNode::NULL_LITERAL;
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeVariableRef(std::string_view name) {
    auto node = new AstNode();
    node->type = AstNode::VARIABLE_REF;
    node->name = std::string(name);
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeBinaryOp(int op, AstNode* left, AstNode* right) {
    auto node = new AstNode();
    node->type = AstNode::BINARY_OP;
    node->op = op;
    node->left = left;
    node->right = right;
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeUnaryOp(int op, AstNode* operand) {
    auto node = new AstNode();
    node->type = AstNode::UNARY_OP;
    node->op = op;
    node->left = operand;
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeConcat(const std::vector<AstNode*>& parts) {
    auto node = new AstNode();
    node->type = AstNode::CONCAT;
    node->children = parts;
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

AstNode* ParserImpl::makeList(const std::vector<AstNode*>& elements) {
    if (elements.size() > 10000) {
        error("List size exceeds 10000", true);
        return makeStringLiteral("");
    }
    auto node = new AstNode();
    node->type = AstNode::LIST;
    node->children = elements;
    node->line = cur_.line;
    node->column = cur_.column;
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    return node;
}

// ---- Проверки ----
bool ParserImpl::checkDuplicate(std::string_view name,
                                const std::unordered_set<std::string>& set,
                                std::string_view type) {
    if (set.contains(std::string(name))) {
        error(std::format("{} '{}' already defined.", type, name), true);
        return true;
    }
    return false;
}

void ParserImpl::addDefinition(std::string_view name, std::unordered_set<std::string>& set) {
    set.insert(std::string(name));
}

// ВАЖНО: recover=false — прерывает рекурсию include через throw.
void ParserImpl::checkIncludeCycle(std::string_view path) {
    std::string canonical = normalizePath(path);
    if (processingStack_.contains(canonical)) {
        error(std::format("Include/import cycle detected: {}", path), false);
    }
}

void ParserImpl::checkType(AstNode* node, std::string_view expectedType, std::string_view context) {
    (void)node; (void)expectedType; (void)context;
}

void ParserImpl::checkFunctionArgs(std::string_view name, const std::vector<Value>& args,
                                   int minCount, int maxCount) {
    if (static_cast<int>(args.size()) < minCount) {
        error(std::format("Function '{}' expects at least {} arguments, got {}",
                          name, minCount, args.size()), true);
    }
    if (maxCount >= 0 && static_cast<int>(args.size()) > maxCount) {
        error(std::format("Function '{}' expects at most {} arguments, got {}",
                          name, maxCount, args.size()), true);
    }
}

// ---- parseIdent — ИЗМЕНЕНО: возвращает std::string ----
std::string ParserImpl::parseIdent() {
    if (cur_.type != TOK_IDENT) {
        error("Expected identifier", true);
        return {};
    }
    std::string s{str(cur_.strId)};
    advance();
    return s;
}

// ---- Нормализация пути ----
std::string ParserImpl::normalizePath(std::string_view path) {
    if (path.empty()) return {};
    std::filesystem::path p{std::string(path)};
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        auto canon = std::filesystem::canonical(p, ec);
        if (!ec) return canon.string();
    }
    return std::filesystem::absolute(p, ec).lexically_normal().string();
}

// ---- Вспомогательная функция ----
std::string ParserImpl::astNodeToStringWithState(AstNode* node, const core::State* state) {
    if (!node) return {};
    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return node->value.to_string();
        case AstNode::VARIABLE_REF: {
            auto opt = state->getGlobal(node->name);
            if (opt.has_value()) return *opt;
            return node->name;
        }
        case AstNode::CONCAT: {
            std::string result;
            for (auto child : node->children) {
                result += astNodeToStringWithState(child, state);
            }
            return result;
        }
        case AstNode::NUMBER_LITERAL:
            return std::format("{}", node->value.as_number());
        case AstNode::BOOL_LITERAL:
            return node->value.to_bool() ? "true" : "false";
        default:
            return {};
    }
}

} // namespace pmake::parser