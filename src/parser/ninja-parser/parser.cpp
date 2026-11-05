// parser/ninja-parser/parser.cpp
#include "parser/ninja-parser/parser.h"
#include "parser/ninja-parser/lexer.h"
#include "util/filesystem.h"
#include "util/logging.h"

#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pmake::ninja {

namespace {
    std::string unescapePath(std::string_view path) {
        std::string result(path);
        auto replace_inplace = [&](std::string_view from, std::string_view to) {
            std::size_t pos = 0;
            while ((pos = result.find(from.data(), pos, from.size())) != std::string::npos) {
                result.replace(pos, from.size(), to.data(), to.size());
                pos += to.size();
            }
        };
        replace_inplace("$:", ":");
        replace_inplace("$ ", " ");
        replace_inplace("$$", "$");
        return result;
    }
}

class ParserImpl {
public:
    ParserImpl(std::string_view data, std::string_view filename)
        : lexer_(data, filename), filename_(filename) {}

    std::unique_ptr<core::State> parse();

private:
    Lexer lexer_;
    std::string filename_;
    core::State state_;
    util::StringPool& pool_ = util::StringPool::instance();

    Token cur_;
    bool isIncluded_ = false;
    std::unordered_set<std::string> processingStack_;

    void advance() { cur_ = lexer_.nextToken(); }

    bool match(TokenType t) {
        if (cur_.type == t) {
            advance();
            return true;
        }
        return false;
    }

    bool expect(TokenType t, std::string_view msg) {
        if (cur_.type == t) {
            advance();
            return true;
        }
        error(msg);
        return false;
    }

    [[noreturn]] void error(std::string_view msg) {
        LOG_ERROR(std::format("Ninja Parser error at {} line {}: {}",
                              filename_, cur_.line, msg));
        throw std::runtime_error("Parse error");
    }

    std::string_view str(int id) const {
        if (id < 0 || id >= static_cast<int>(pool_.size())) {
            LOG_ERROR(std::format("Invalid string id: {} (pool size: {})",
                                  id, pool_.size()));
            return {};
        }
        return pool_.get(id);
    }

    void parseTopLevel();
    std::string parseName();
    std::vector<std::string> parseNameList();
    void parseVariable();
    void parseRule();
    void parseBuild();
    void parseDefault();
    void parseInclude(bool subninja);
    void parsePool(std::string_view poolName);

    bool isNameToken() const {
        return cur_.type == TOK_IDENT || cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF;
    }

    std::string readValueWithContinuation() {
        return lexer_.readValue(true);
    }

    void parseIndentedBlock(int baseIndent,
                            const std::function<void(const std::string&, const std::string&)>& handler) {
        while (true) {
            if (cur_.type != TOK_INDENT || cur_.indent != baseIndent)
                break;
            advance();
            if (cur_.type != TOK_IDENT) {
                error("expected variable name in indented block");
            }
            int keyId = cur_.strId;
            std::string key{str(keyId)};
            advance();
            if (cur_.type != TOK_EQUALS) {
                error("expected '='");
            }
            std::string value = readValueWithContinuation();
            advance();

            handler(key, value);

            if (cur_.type == TOK_NEWLINE) advance();
            else break;
            if (cur_.type == TOK_INDENT && cur_.indent == baseIndent)
                continue;
            else
                break;
        }
        while (cur_.type == TOK_NEWLINE) advance();
        if (cur_.type == TOK_INDENT && cur_.indent == 0) advance();
    }
};

// ----------------------------------------------------------------------------
// Реализация методов парсинга
// ----------------------------------------------------------------------------

std::string ParserImpl::parseName() {
    std::string result;
    bool first = true;
    while (true) {
        if (cur_.type == TOK_IDENT || cur_.type == TOK_STRING) {
            if (!first && lexer_.hadWhitespace()) break;
            result += str(cur_.strId);
            advance();
            first = false;
        } else if (cur_.type == TOK_VAR_REF) {
            if (!first && lexer_.hadWhitespace()) break;
            std::string_view varName = str(cur_.strId);
            std::optional<std::string> val = state_.getGlobal(varName);
            if (val.has_value()) {
                result += *val;
            } else {
                result += '$';
                result += varName;
            }
            advance();
            first = false;
        } else {
            break;
        }
    }
    return unescapePath(result);
}

std::vector<std::string> ParserImpl::parseNameList() {
    std::vector<std::string> names;
    while (isNameToken()) {
        names.push_back(parseName());
    }
    return names;
}

void ParserImpl::parseVariable() {
    int nameId = cur_.strId;
    advance();
    if (cur_.type != TOK_EQUALS) error("expected '='");
    std::string value = readValueWithContinuation();
    state_.setGlobal(str(nameId), value);
    advance();
}

void ParserImpl::parseRule() {
    if (cur_.type != TOK_IDENT) error("expected rule name");
    int ruleNameId = cur_.strId;
    advance();

    core::Rule rule;
    rule.name = std::string(str(ruleNameId));

    if (cur_.type != TOK_NEWLINE) error("expected newline after rule name");
    advance();

    if (cur_.type != TOK_INDENT || cur_.indent <= 0) error("expected indented block for rule");
    int baseIndent = cur_.indent;

    parseIndentedBlock(baseIndent, [&](const std::string& key, const std::string& value) {
        if (key == "command")      rule.command = value;
        else if (key == "depfile") rule.depfile = value;
        else if (key == "deps")    rule.deps = value;
        else if (key == "generator") rule.generator = (value == "1" || value == "true");
        else if (key == "restat")  rule.restat = (value == "1" || value == "true");
        else if (key == "rspfile") rule.rspfile = value;
        else if (key == "rspfile_content") rule.rspfile_content = value;
        else if (key == "pool")    rule.pool = (value == "console" || value == "1" || value == "true");
        else if (key == "description") rule.description = value;
        else {
            LOG_WARN(std::format("Unknown rule variable '{}' in rule '{}'", key, rule.name));
        }
    });

    state_.addRule(std::move(rule));
}

void ParserImpl::parseBuild() {
    std::vector<std::string> outputs = parseNameList();

    // FIX: implicit outputs (после первого |) игнорируются.
    // У нас в Build нет поля implicit_outputs, поэтому просто парсим и отбрасываем.
    // Для CMake это безопасно: там implicit дублирует explicit с абсолютным префиксом.
    if (cur_.type == TOK_PIPE) {
        advance();
        while (cur_.type != TOK_PIPE2 && cur_.type != TOK_COLON &&
               cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
            if (isNameToken())
                parseName(); // discard
            else
                break;
        }
    }

    // Order-only outputs перед ':' (в CMake не встречается, тоже отбрасываем)
    if (cur_.type == TOK_PIPE2) {
        advance();
        while (cur_.type != TOK_COLON && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
            if (isNameToken())
                parseName(); // discard
            else
                break;
        }
    }

    if (cur_.type != TOK_COLON)
        error("expected ':' after outputs or dependencies");
    advance();

    std::string ruleName = parseName();

    std::vector<std::string> inputs;
    std::vector<std::string> implicit_after;
    std::vector<std::string> order_only_after;

    while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_PIPE) {
            advance();
            while (cur_.type != TOK_PIPE2 && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
                if (isNameToken())
                    implicit_after.push_back(parseName());
                else
                    break;
            }
        } else if (cur_.type == TOK_PIPE2) {
            advance();
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
                if (isNameToken())
                    order_only_after.push_back(parseName());
                else
                    break;
            }
        } else if (isNameToken()) {
            inputs.push_back(parseName());
        } else {
            break;
        }
    }
    if (cur_.type == TOK_NEWLINE)
        advance();

    // implicit = только implicit_after (implicit INPUTS).
    std::vector<std::string> implicit = std::move(implicit_after);
    std::vector<std::string> order_only = std::move(order_only_after);

    auto scope = std::make_shared<core::Scope>();
    if (cur_.type == TOK_INDENT && cur_.indent > 0) {
        int baseIndent = cur_.indent;
        parseIndentedBlock(baseIndent, [&](const std::string& key, const std::string& value) {
            scope->set(key, value);
        });
    }

    core::Build build;
    build.outputs = std::move(outputs);
    build.inputs = std::move(inputs);
    build.implicit = std::move(implicit);
    build.order_only = std::move(order_only);
    build.rule_name = std::move(ruleName);
    build.scope = scope;
    state_.addBuild(std::move(build));
}

void ParserImpl::parseDefault() {
    std::vector<std::string> targets = parseNameList();
    state_.defaultTargets() = std::move(targets);
    if (cur_.type == TOK_NEWLINE) advance();
}

void ParserImpl::parseInclude(bool subninja) {
    std::string includeFile = parseName();
    if (includeFile.empty()) {
        LOG_WARN("Include with empty filename, skipping");
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (cur_.type == TOK_NEWLINE) advance();

    includeFile = state_.expandVars(includeFile);
    includeFile = unescapePath(includeFile);

    if (includeFile.empty()) {
        LOG_WARN("Include with empty filename after expansion, skipping");
        return;
    }

    std::filesystem::path currentPath(filename_);
    std::error_code ec;
    std::filesystem::path absPathObj = std::filesystem::absolute(currentPath, ec).parent_path() / includeFile;
    std::string absPath = absPathObj.string();

    if (processingStack_.contains(absPath)) {
        LOG_ERROR(std::format("Cyclic include detected: {}", absPath));
        throw std::runtime_error("Cyclic include");
    }
    processingStack_.insert(absPath);

    state_.includedFiles().push_back(absPath);

    std::string data;
    if (!util::readFile(absPath, data)) {
        LOG_ERROR(std::format("Cannot open include file: {}", absPath));
        processingStack_.erase(absPath);
        return;
    }

    ParserImpl subparser(data, absPath);
    subparser.processingStack_ = processingStack_;

    if (!subninja) {
        for (const auto& [k, v] : state_.globals()) {
            subparser.state_.setGlobal(k, v);
        }
    }

    subparser.isIncluded_ = true;
    auto subState = subparser.parse();
    if (subState) {
        for (const auto& [name, rule] : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& [name, pool] : subState->pools()) state_.addPool(pool);
        for (const auto& inc : subState->includedFiles()) {
            state_.includedFiles().push_back(inc);
        }
        for (const auto& target : subState->defaultTargets()) {
            const auto& dt = state_.defaultTargets();
            if (std::find(dt.begin(), dt.end(), target) == dt.end())
                state_.defaultTargets().push_back(target);
        }

        if (!subninja) {
            for (const auto& [k, v] : subState->globals()) {
                state_.setGlobal(k, v);
            }
        }
    } else {
        LOG_WARN("Sub-state parsing failed");
    }

    processingStack_.erase(absPath);
}

void ParserImpl::parsePool(std::string_view poolName) {
    core::Pool pool;
    pool.name = std::string(poolName);
    pool.depth = 1;

    if (cur_.type != TOK_NEWLINE) error("expected newline after pool name");
    advance();

    if (cur_.type != TOK_INDENT || cur_.indent <= 0) error("expected indented block for pool");
    int baseIndent = cur_.indent;

    parseIndentedBlock(baseIndent, [&](const std::string& key, const std::string& value) {
        if (key == "depth") {
            try {
                int d = std::stoi(value);
                if (d > 0) pool.depth = d;
                else LOG_WARN(std::format("Invalid depth value '{}' for pool '{}'", value, poolName));
            } catch (...) {
                LOG_WARN(std::format("Invalid depth value '{}' for pool '{}'", value, poolName));
            }
        } else {
            LOG_WARN(std::format("Unknown pool variable '{}' in pool '{}'", key, poolName));
        }
    });

    state_.addPool(std::move(pool));
}

void ParserImpl::parseTopLevel() {
    while (cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            std::string_view word = str(cur_.strId);

            if (word == "rule") {
                advance();
                parseRule();
                continue;
            }
            else if (word == "build") {
                advance();
                parseBuild();
                continue;
            }
            else if (word == "default") {
                advance();
                parseDefault();
                continue;
            }
            else if (word == "include") {
                advance();
                parseInclude(false);
                continue;
            }
            else if (word == "subninja") {
                advance();
                parseInclude(true);
                continue;
            }
            else if (word == "pool") {
                advance();
                if (cur_.type == TOK_IDENT) {
                    std::string poolName{str(cur_.strId)};
                    advance();
                    parsePool(poolName);
                } else if (cur_.type == TOK_EQUALS) {
                    std::string value = readValueWithContinuation();
                    state_.setGlobal(word, value);
                    advance();
                } else {
                    error("expected pool name or '=' after pool");
                }
                continue;
            }
            else {
                int id = cur_.strId;
                std::string varName{str(id)};
                advance();
                if (cur_.type != TOK_EQUALS) {
                    error(std::format("expected '=' after variable '{}'", varName));
                }
                std::string value = readValueWithContinuation();
                state_.setGlobal(varName, value);
                advance();
                continue;
            }
        }
        else if (cur_.type == TOK_NEWLINE) {
            advance();
        }
        else if (cur_.type == TOK_INDENT) {
            advance();
        }
        else {
            error(std::format("Unexpected token in top level: {}", static_cast<int>(cur_.type)));
        }
    }
}

std::unique_ptr<core::State> ParserImpl::parse() {
    advance();
    try {
        parseTopLevel();
    } catch (const std::exception& e) {
        LOG_ERROR(std::format("Parsing failed: {}", e.what()));
        return nullptr;
    } catch (...) {
        LOG_ERROR("Unknown exception during parsing");
        return nullptr;
    }

    // Добавляем встроенные правила
    bool needsPhony = false;
    for (const auto& build : state_.builds()) {
        if (build.rule_name == "phony") {
            needsPhony = true;
            break;
        }
    }
    if (needsPhony) {
        if (!state_.getRule("phony")) {
            core::Rule phonyRule;
            phonyRule.name = "phony";
            phonyRule.command = "";
            state_.addRule(std::move(phonyRule));
        }
    }

    if (!state_.getRule("rcc")) {
        core::Rule rccRule;
        rccRule.name = "rcc";
        rccRule.command = "rcc $in -o $out";
        rccRule.generator = true;
        state_.addRule(std::move(rccRule));
    }
    if (!state_.getRule("moc")) {
        core::Rule mocRule;
        mocRule.name = "moc";
        mocRule.command = "moc $in -o $out";
        mocRule.generator = true;
        state_.addRule(std::move(mocRule));
    }
    if (!state_.getRule("uic")) {
        core::Rule uicRule;
        uicRule.name = "uic";
        uicRule.command = "uic $in -o $out";
        uicRule.generator = true;
        state_.addRule(std::move(uicRule));
    }

    return std::make_unique<core::State>(std::move(state_));
}

// ============================================================================
// Внешний интерфейс
// ============================================================================
std::unique_ptr<core::State> Parser::parse(std::string_view filename) {
    std::string data;
    if (!util::readFile(filename, data)) {
        LOG_ERROR(std::format("Cannot read file: {}", filename));
        return nullptr;
    }
    ParserImpl impl(data, filename);
    return impl.parse();
}

} // namespace pmake::ninja