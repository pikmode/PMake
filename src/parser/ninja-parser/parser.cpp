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
    // PERF: early return если нет $. CMake-generated build.ninja
    // почти не содержит $, значит 99% вызовов возвращаются сразу.
    // Раньше всегда делали 3 прохода replace_inplace.
    std::string unescapePath(std::string_view path) {
        if (path.find('$') == std::string_view::npos) {
            return std::string(path);
        }
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

    // PERF: handler принимает std::string&& — move вместо copy.
    void parseIndentedBlock(int baseIndent,
                            const std::function<void(std::string&&, std::string&&)>& handler) {
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

            handler(std::move(key), std::move(value));

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

// ============================================================
// parseName — PERF: fast path для одиночного IDENT/STRING без $.
//
// 99% name в CMake-generated build.ninja — это одиночный IDENT
// (obj/main.o, src/main.cpp) без $ и без continuation.
// Fast path: сохраняем lexer state, advance на 1 токен, проверяем
// разделитель, и если да — возвращаем просто string_view.
//
// Slow path: continuation (несколько токенов без whitespace) или
// VAR_REF (${var}) — оригинальная логика.
// ============================================================
std::string ParserImpl::parseName() {
    // ---- PERF: fast path ----
    if (cur_.type == TOK_IDENT || cur_.type == TOK_STRING) {
        std::string_view s = str(cur_.strId);
        if (s.find('$') == std::string_view::npos) {
            auto ls = lexer_.saveState();
            Token saved = cur_;
            int savedHadWs = lexer_.hadWhitespace();

            advance();
            bool isSeparator =
                cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF ||
                cur_.type == TOK_COLON   || cur_.type == TOK_PIPE ||
                cur_.type == TOK_PIPE2   || cur_.type == TOK_INDENT ||
                cur_.type == TOK_EQUALS;

            // Continuation (без whitespace, но есть следующий name) — НЕ fast path.
            bool isContinuation = !isSeparator && !savedHadWs && lexer_.hadWhitespace() == false &&
                                  isNameToken();

            if (isSeparator) {
                return std::string(s);
            }
            // Если был whitespace — это два разных name, fast path OK.
            if (savedHadWs && !isSeparator) {
                return std::string(s);
            }
            (void)isContinuation;

            // Не разделитель и без whitespace — continuation (склейка), откат.
            lexer_.restoreState(ls);
            cur_ = saved;
        }
    }

    // ---- slow path — оригинал ----
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
    // PERF: типично 1-5 outputs/inputs. Reserve 8, чтобы не реаллоцировать.
    std::vector<std::string> names;
    names.reserve(8);
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

    // PERF: handler принимает std::string&& → std::move в поля rule.
    parseIndentedBlock(baseIndent, [&](std::string&& key, std::string&& value) {
        if (key == "command")               rule.command = std::move(value);
        else if (key == "depfile")          rule.depfile = std::move(value);
        else if (key == "deps")             rule.deps = std::move(value);
        else if (key == "generator")        rule.generator = (value == "1" || value == "true");
        else if (key == "restat")           rule.restat = (value == "1" || value == "true");
        else if (key == "rspfile")          rule.rspfile = std::move(value);
        else if (key == "rspfile_content")  rule.rspfile_content = std::move(value);
        else if (key == "pool")             rule.pool = (value == "console" || value == "1" || value == "true");
        else if (key == "description")      rule.description = std::move(value);
        else {
            LOG_WARN(std::format("Unknown rule variable '{}' in rule '{}'", key, rule.name));
        }
    });

    state_.addRule(std::move(rule));
}

void ParserImpl::parseBuild() {
    std::vector<std::string> outputs = parseNameList();

    // FIX: implicit outputs (после первого |) игнорируются.
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
    inputs.reserve(8);
    implicit_after.reserve(4);
    order_only_after.reserve(4);

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

    // PERF: Scope создаём только если есть { ... } блок.
    // Для 99% build без per-build переменных — shared_ptr не нужен.
    std::shared_ptr<core::Scope> scope;
    if (cur_.type == TOK_INDENT && cur_.indent > 0) {
        scope = std::make_shared<core::Scope>();
        int baseIndent = cur_.indent;
        // PERF: key/value передаём по value, но key копируется в Scope::set
        // (в unordered_map). value можно move, но set принимает string_view.
        // Оставляем как есть — экономия от move в Scope::set минимальна.
        parseIndentedBlock(baseIndent, [&](std::string&& key, std::string&& value) {
            scope->set(key, value);
        });
    }

    core::Build build;
    build.outputs = std::move(outputs);
    build.inputs = std::move(inputs);
    build.implicit = std::move(implicit_after);
    build.order_only = std::move(order_only_after);
    build.rule_name = std::move(ruleName);
    build.scope = std::move(scope);
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

    parseIndentedBlock(baseIndent, [&](std::string&& key, std::string&& value) {
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