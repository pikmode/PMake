// parser/parser.cpp
#include "parser/parser.h"
#include "parser/lexer.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include <QFileInfo>
#include <QStack>
#include <QVector>
#include <QSet>
#include <QRegularExpression>

namespace pmake::parser {

namespace {
    QString unescapePath(const QString& path) {
        QString result = path;
        result.replace(QStringLiteral("$:"), QStringLiteral(":"));
        result.replace(QStringLiteral("$ "), QStringLiteral(" "));
        result.replace(QStringLiteral("$$"), QStringLiteral("$"));
        return result;
    }

    bool isNumber(const QString& s) {
        bool ok;
        s.toInt(&ok);
        return ok;
    }
}

class ParserImpl {
public:
    ParserImpl(const QByteArray& data, const QString& filename)
        : lexer_(data, filename), filename_(filename) {}

    std::unique_ptr<core::State> parse();

private:
    Lexer lexer_;
    QString filename_;
    core::State state_;
    util::StringPool& pool_ = util::StringPool::instance();

    Token cur_;
    bool isIncluded_ = false;
    QSet<QString> processingStack_;

    void advance() { cur_ = lexer_.nextToken(); }

    bool match(TokenType t) {
        if (cur_.type == t) {
            advance();
            return true;
        }
        return false;
    }

    bool expect(TokenType t, const QString& msg) {
        if (cur_.type == t) {
            advance();
            return true;
        }
        error(msg);
        return false;
    }

    void error(const QString& msg) {
        LOG_ERROR(QString("Parser error at %1 line %2: %3").arg(filename_).arg(cur_.line).arg(msg));
        throw std::runtime_error("Parse error");
    }

    QString str(int id) const {
        if (id < 0 || id >= pool_.size()) {
            LOG_ERROR(QString("Invalid string id: %1 (pool size: %2)").arg(id).arg(pool_.size()));
            return QString();
        }
        return QString::fromUtf8(pool_.get(id));
    }

    void parseTopLevel();
    QString parseName();
    QVector<QString> parseNameList();
    void parseVariable();
    void parseRule();
    void parseBuild();
    void parseDefault();
    void parseInclude(bool subninja);
    void parsePool(const QString& poolName);

    bool isNameToken() const {
        return cur_.type == TOK_IDENT || cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF;
    }

    QString readValueWithContinuation() {
        return lexer_.readValue(true);
    }

    void parseIndentedBlock(int baseIndent, const std::function<void(const QString&, const QString&)>& handler) {
        while (true) {
            if (cur_.type != TOK_INDENT || cur_.indent != baseIndent)
                break;
            advance();
            if (cur_.type != TOK_IDENT) {
                error("expected variable name in indented block");
            }
            int keyId = cur_.strId;
            QString key = str(keyId);
            advance();
            if (cur_.type != TOK_EQUALS) {
                error("expected '='");
            }
            QString value = readValueWithContinuation();
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

    void logInfo(const QString& msg) {
        LOG_INFO(msg);
    }
};

// ----------------------------------------------------------------------------
// Реализация методов парсинга
// ----------------------------------------------------------------------------

QString ParserImpl::parseName() {
    QString result;
    bool first = true;
    while (true) {
        if (cur_.type == TOK_IDENT || cur_.type == TOK_STRING) {
            if (!first && lexer_.hadWhitespace()) break;
            result += str(cur_.strId);
            advance();
            first = false;
        } else if (cur_.type == TOK_VAR_REF) {
            if (!first && lexer_.hadWhitespace()) break;
            QString varName = str(cur_.strId);
            std::optional<QString> val = state_.getGlobal(varName);
            if (val.has_value()) {
                result += *val;
            } else {
                result += "$" + varName;
            }
            advance();
            first = false;
        } else {
            break;
        }
    }
    return unescapePath(result);
}

QVector<QString> ParserImpl::parseNameList() {
    QVector<QString> names;
    while (isNameToken()) {
        names.append(parseName());
    }
    return names;
}

void ParserImpl::parseVariable() {
    int nameId = cur_.strId;
    advance();
    if (cur_.type != TOK_EQUALS) error("expected '='");
    QString value = readValueWithContinuation();
    state_.setGlobal(str(nameId), value);
    logInfo(QString("Variable set: %1 = %2").arg(str(nameId)).arg(value));
    advance();
}

void ParserImpl::parseRule() {
    if (cur_.type != TOK_IDENT) error("expected rule name");
    int ruleNameId = cur_.strId;
    advance();

    core::Rule rule;
    rule.name = str(ruleNameId);

    if (cur_.type != TOK_NEWLINE) error("expected newline after rule name");
    advance();

    if (cur_.type != TOK_INDENT || cur_.indent <= 0) error("expected indented block for rule");
    int baseIndent = cur_.indent;

    parseIndentedBlock(baseIndent, [&](const QString& key, const QString& value) {
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
            LOG_WARN(QString("Unknown rule variable '%1' in rule '%2'").arg(key).arg(rule.name));
        }
    });

    state_.addRule(std::move(rule));
    logInfo(QString("Rule added: %1").arg(rule.name));
}

void ParserImpl::parseBuild() {
    logInfo("parseBuild called");
    
    QVector<QString> outputs = parseNameList();
    
    QVector<QString> implicit_before;
    QVector<QString> order_only_before;
    
    if (cur_.type == TOK_PIPE) {
        advance();
        while (cur_.type != TOK_PIPE2 && cur_.type != TOK_COLON &&
               cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
            if (isNameToken())
                implicit_before.append(parseName());
            else
                break;
        }
    }
    if (cur_.type == TOK_PIPE2) {
        advance();
        while (cur_.type != TOK_COLON && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
            if (isNameToken())
                order_only_before.append(parseName());
            else
                break;
        }
    }
    
    if (cur_.type != TOK_COLON)
        error("expected ':' after outputs or dependencies");
    advance();
    
    QString ruleName = parseName();
    
    QVector<QString> inputs;
    QVector<QString> implicit_after;
    QVector<QString> order_only_after;
    
    while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_PIPE) {
            advance();
            while (cur_.type != TOK_PIPE2 && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
                if (isNameToken())
                    implicit_after.append(parseName());
                else
                    break;
            }
        } else if (cur_.type == TOK_PIPE2) {
            advance();
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
                if (isNameToken())
                    order_only_after.append(parseName());
                else
                    break;
            }
        } else if (isNameToken()) {
            inputs.append(parseName());
        } else {
            break;
        }
    }
    if (cur_.type == TOK_NEWLINE)
        advance();

    QVector<QString> implicit = implicit_before + implicit_after;
    QVector<QString> order_only = order_only_before + order_only_after;
    
    auto scope = QSharedPointer<core::Scope>::create();
    if (cur_.type == TOK_INDENT && cur_.indent > 0) {
        int baseIndent = cur_.indent;
        parseIndentedBlock(baseIndent, [&](const QString& key, const QString& value) {
            scope->set(key, value);
            logInfo(QString("  build local: %1 = %2").arg(key).arg(value));
        });
    }
    
    core::Build build;
    build.outputs = outputs;
    build.inputs = inputs;
    build.implicit = implicit;
    build.order_only = order_only;
    build.rule_name = ruleName;
    build.scope = scope;
    state_.addBuild(std::move(build));
    logInfo(QString("Build added: outputs=%1, rule=%2")
            .arg(build.outputs.join(',')).arg(build.rule_name));
}

void ParserImpl::parseDefault() {
    QVector<QString> targets = parseNameList();
    state_.defaultTargets() = targets;
    if (cur_.type == TOK_NEWLINE) advance();
    logInfo(QString("Default targets: %1").arg(state_.defaultTargets().join(',')));
}

// ============================================================
// ИСПРАВЛЕННЫЙ parseInclude – добавлен возврат глобальных переменных
// ============================================================
void ParserImpl::parseInclude(bool subninja) {
    logInfo(QString("parseInclude called (subninja=%1)").arg(subninja));
    QString includeFile = parseName();
    if (includeFile.isEmpty()) {
        LOG_WARN("Include with empty filename, skipping");
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (cur_.type == TOK_NEWLINE) advance();

    logInfo(QString("Include raw: '%1'").arg(includeFile));
    includeFile = state_.expandVars(includeFile);
    includeFile = unescapePath(includeFile);
    logInfo(QString("Include expanded: '%1'").arg(includeFile));

    if (includeFile.isEmpty()) {
        LOG_WARN("Include with empty filename after expansion, skipping");
        return;
    }

    QFileInfo current(filename_);
    QString absPath = current.absolutePath() + '/' + includeFile;
    logInfo(QString("Include absolute path: '%1'").arg(absPath));

    // Проверка на цикл
    if (processingStack_.contains(absPath)) {
        LOG_ERROR(QString("Cyclic include detected: %1").arg(absPath));
        throw std::runtime_error("Cyclic include");
    }
    processingStack_.insert(absPath);

    state_.includedFiles().append(absPath);

    QByteArray data;
    if (!util::readFile(absPath, data)) {
        LOG_ERROR(QString("Cannot open include file: %1").arg(absPath));
        processingStack_.remove(absPath);
        return;
    }
    logInfo(QString("Include file loaded, size=%1 bytes").arg(data.size()));

    ParserImpl subparser(data, absPath);
    subparser.processingStack_ = processingStack_;

    // Для include (не subninja) копируем глобальные переменные из родителя в дочерний
    if (!subninja) {
        for (auto it = state_.globals().begin(); it != state_.globals().end(); ++it) {
            subparser.state_.setGlobal(it.key(), it.value());
        }
        logInfo("Copied global variables to included file");
    }

    subparser.isIncluded_ = true;
    auto subState = subparser.parse();
    if (subState) {
        logInfo(QString("Sub-state parsed: %1 rules, %2 builds, %3 pools")
                .arg(subState->rules().size())
                .arg(subState->builds().size())
                .arg(subState->pools().size()));
        // Копируем правила, сборки, пулы
        for (auto& rule : subState->rules()) state_.addRule(std::move(rule));
        for (auto& build : subState->builds()) state_.addBuild(std::move(build));
        for (const auto& pool : subState->pools()) state_.addPool(pool);
        for (const QString& inc : subState->includedFiles()) {
            state_.includedFiles().append(inc);
        }
        for (const QString& target : subState->defaultTargets()) {
            if (!state_.defaultTargets().contains(target))
                state_.defaultTargets().append(target);
        }

        // НОВОЕ: для include копируем глобальные переменные обратно в родительский state_
        if (!subninja) {
            for (auto it = subState->globals().begin(); it != subState->globals().end(); ++it) {
                state_.setGlobal(it.key(), it.value());
            }
            logInfo("Copied global variables back from included file");
        }
    } else {
        LOG_WARN("Sub-state parsing failed");
    }

    processingStack_.remove(absPath);
}

void ParserImpl::parsePool(const QString& poolName) {
    core::Pool pool;
    pool.name = poolName;
    pool.depth = 1;

    if (cur_.type != TOK_NEWLINE) error("expected newline after pool name");
    advance();

    if (cur_.type != TOK_INDENT || cur_.indent <= 0) error("expected indented block for pool");
    int baseIndent = cur_.indent;

    parseIndentedBlock(baseIndent, [&](const QString& key, const QString& value) {
        if (key == "depth") {
            bool ok;
            int d = value.toInt(&ok);
            if (ok && d > 0) pool.depth = d;
            else LOG_WARN(QString("Invalid depth value '%1' for pool '%2'").arg(value).arg(poolName));
        } else {
            LOG_WARN(QString("Unknown pool variable '%1' in pool '%2'").arg(key).arg(poolName));
        }
    });

    state_.addPool(std::move(pool));
    logInfo(QString("Pool added: %1 (depth %2)").arg(pool.name).arg(pool.depth));
}

// ----------------------------------------------------------------------------
// Парсинг верхнего уровня
// ----------------------------------------------------------------------------
void ParserImpl::parseTopLevel() {
    while (cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            QString word = str(cur_.strId);

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
                    QString poolName = str(cur_.strId);
                    advance();
                    parsePool(poolName);
                } else if (cur_.type == TOK_EQUALS) {
                    QString value = readValueWithContinuation();
                    state_.setGlobal(word, value);
                    logInfo(QString("Top-level var: %1 = %2").arg(word).arg(value));
                    advance();
                } else {
                    error("expected pool name or '=' after pool");
                }
                continue;
            }
            else {
                int id = cur_.strId;
                QString varName = str(id);
                advance();
                if (cur_.type != TOK_EQUALS) {
                    error(QString("expected '=' after variable '%1'").arg(varName));
                }
                QString value = readValueWithContinuation();
                state_.setGlobal(varName, value);
                logInfo(QString("Top-level var: %1 = %2").arg(varName).arg(value));
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
            error(QString("Unexpected token in top level: %1").arg(cur_.type));
        }
    }

    logInfo(QString("Parsing finished: %1 rules, %2 builds, %3 pools, %4 default targets")
             .arg(state_.rules().size())
             .arg(state_.builds().size())
             .arg(state_.pools().size())
             .arg(state_.defaultTargets().size()));
}

std::unique_ptr<core::State> ParserImpl::parse() {
    advance();
    try {
        parseTopLevel();
    } catch (const std::exception& e) {
        LOG_ERROR(QString("Parsing failed: %1").arg(e.what()));
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
        bool hasPhony = false;
        for (const auto& rule : state_.rules()) {
            if (rule.name == "phony") {
                hasPhony = true;
                break;
            }
        }
        if (!hasPhony) {
            core::Rule phonyRule;
            phonyRule.name = "phony";
            phonyRule.command = "";
            state_.addRule(std::move(phonyRule));
            logInfo("Built-in rule 'phony' added (empty command)");
        }
    }

    if (!state_.getRule("rcc")) {
        core::Rule rccRule;
        rccRule.name = "rcc";
        rccRule.command = "rcc $in -o $out";
        rccRule.generator = true;
        state_.addRule(std::move(rccRule));
        logInfo("Built-in rule 'rcc' added");
    }
    if (!state_.getRule("moc")) {
        core::Rule mocRule;
        mocRule.name = "moc";
        mocRule.command = "moc $in -o $out";
        mocRule.generator = true;
        state_.addRule(std::move(mocRule));
        logInfo("Built-in rule 'moc' added");
    }
    if (!state_.getRule("uic")) {
        core::Rule uicRule;
        uicRule.name = "uic";
        uicRule.command = "uic $in -o $out";
        uicRule.generator = true;
        state_.addRule(std::move(uicRule));
        logInfo("Built-in rule 'uic' added");
    }

    return std::make_unique<core::State>(std::move(state_));
}

// ============================================================================
// Внешний интерфейс
// ============================================================================
std::unique_ptr<core::State> Parser::parse(const QString& filename) {
    QByteArray data;
    if (!util::readFile(filename, data)) {
        LOG_ERROR(QString("Cannot read file: %1").arg(filename));
        return nullptr;
    }
    ParserImpl impl(data, filename);
    return impl.parse();
}

} // namespace pmake::parser