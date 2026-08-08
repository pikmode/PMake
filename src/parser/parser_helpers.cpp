#include "parser/parser_impl.h"
#include "parser/parser_utils.h"
#include <QDebug>
#include <QFileInfo>
#include <stdexcept>

namespace pmake::parser {

// Инициализация статических счётчиков
int ParserImpl::operationCounter = 0;
int ParserImpl::includeDepth = 0;
int ParserImpl::functionCallDepth = 0;

// ---- Конструктор/деструктор ----
ParserImpl::ParserImpl(const QByteArray& data, const QString& filename,
                       const QString& config, const QString& platform)
    : lexer_(data, filename), filename_(filename),
      config_(config), platform_(platform) {
    state_.setGlobal("config", config);
    state_.setGlobal("platform", platform);
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
    astStack_.push(root_);
    operationCounter = 0;
    includeDepth = 0;
    functionCallDepth = 0;
    LOG_VERBOSE("Parser initialized");
}

ParserImpl::~ParserImpl() {
    delete root_;
    LOG_VERBOSE("Parser destroyed");
}

// ---- Вспомогательные методы ----
void ParserImpl::advance() {
    try {
        cur_ = lexer_.nextToken();
        currentColumn_ = cur_.column;
    } catch (const std::exception& e) {
        // При ошибке лексера генерируем EOF и помечаем состояние ошибки
        cur_ = {TOK_EOF, -1, lexer_.line(), lexer_.column(), 0};
        currentColumn_ = cur_.column;
        hasError_ = true;
        errors_.append(QString("Lexer error: %1").arg(e.what()));
        LOG_ERROR(errors_.last());
    } catch (...) {
        cur_ = {TOK_EOF, -1, lexer_.line(), lexer_.column(), 0};
        currentColumn_ = cur_.column;
        hasError_ = true;
        errors_.append("Unknown lexer error");
        LOG_ERROR(errors_.last());
    }
}

bool ParserImpl::match(TokenType t) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    return false;
}

bool ParserImpl::expect(TokenType t, const QString& msg) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    error(msg);
    return false;
}

void ParserImpl::error(const QString& msg, bool recover) {
    static QString lastError;
    static int lastLine = -1;
    if (lastError == msg && lastLine == cur_.line) {
        return;
    }
    lastError = msg;
    lastLine = cur_.line;

    QString err = formatError(filename_, cur_.line, currentColumn_, msg, lexer_.str(cur_.strId));
    errors_.append(err);
    LOG_ERROR(err);
    hasError_ = true;
    if (!recover) {
        throw std::runtime_error(err.toStdString());
    }
}

QString ParserImpl::str(int id) const {
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
            if (cur_.indent > baseIndent) {
                return true;
            }
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
    return astStack_.isEmpty() ? root_ : astStack_.top();
}

void ParserImpl::pushBlock(AstNode* node) {
    astStack_.push(node);
}

void ParserImpl::popBlock() {
    if (!astStack_.isEmpty()) astStack_.pop();
}

// ---- Создание узлов AST ----
AstNode* ParserImpl::makeStringLiteral(const QString& value) {
    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many AST nodes created", false);
    }
    LOG_VERBOSE(QString("makeStringLiteral: value='%1', opCounter=%2")
                .arg(value).arg(ParserImpl::operationCounter));
    auto node = new AstNode();
    node->type = AstNode::STRING_LITERAL;
    node->value = value;
    node->line = cur_.line;
    node->column = cur_.column;
    return node;
}

AstNode* ParserImpl::makeNumberLiteral(double value) {
    auto node = new AstNode();
    node->type = AstNode::NUMBER_LITERAL;
    node->value = value;
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
    node->value = value;
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

AstNode* ParserImpl::makeVariableRef(const QString& name) {
    auto node = new AstNode();
    node->type = AstNode::VARIABLE_REF;
    node->name = name;
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

AstNode* ParserImpl::makeConcat(const QVector<AstNode*>& parts) {
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

AstNode* ParserImpl::makeList(const QVector<AstNode*>& elements) {
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
bool ParserImpl::checkDuplicate(const QString& name, const QSet<QString>& set, const QString& type) {
    if (set.contains(name)) {
        error(QString("%1 '%2' already defined.").arg(type).arg(name), true);
        return true;
    }
    return false;
}

void ParserImpl::addDefinition(const QString& name, QSet<QString>& set) {
    set.insert(name);
}

void ParserImpl::checkIncludeCycle(const QString& path) {
    QString canonical = normalizePath(path);
    if (processingStack_.contains(canonical)) {
        error(QString("Include/import cycle detected: %1").arg(path), true);
    }
}

void ParserImpl::checkType(AstNode* node, const QString& expectedType, const QString& context) {
    Q_UNUSED(node); Q_UNUSED(expectedType); Q_UNUSED(context);
}

void ParserImpl::checkFunctionArgs(const QString& name, const QVector<QVariant>& args,
                                   int minCount, int maxCount) {
    if (args.size() < minCount) {
        error(QString("Function '%1' expects at least %2 arguments, got %3")
              .arg(name).arg(minCount).arg(args.size()), true);
    }
    if (maxCount >= 0 && args.size() > maxCount) {
        error(QString("Function '%1' expects at most %2 arguments, got %3")
              .arg(name).arg(maxCount).arg(args.size()), true);
    }
}

// ---- parseIdent ----
QString ParserImpl::parseIdent() {
    if (cur_.type != TOK_IDENT) {
        error("Expected identifier", true);
        return QString();
    }
    QString s = str(cur_.strId);
    advance();
    return s;
}

// ---- Нормализация пути ----
QString ParserImpl::normalizePath(const QString& path) {
    if (path.isEmpty()) return path;
    QFileInfo fi(path);
    if (fi.exists()) {
        return fi.canonicalFilePath();
    }
    return QFileInfo(path).absoluteFilePath();
}

// ---- Вспомогательная функция для подстановки переменных на этапе парсинга ----
QString ParserImpl::astNodeToStringWithState(AstNode* node, const core::State* state) {
    if (!node) return QString();
    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return node->value.toString();
        case AstNode::VARIABLE_REF: {
            auto opt = state->getGlobal(node->name);
            if (opt.has_value()) return *opt;
            return node->name;
        }
        case AstNode::CONCAT: {
            QString result;
            for (auto child : node->children) {
                result += astNodeToStringWithState(child, state);
            }
            return result;
        }
        case AstNode::NUMBER_LITERAL:
            return QString::number(node->value.toDouble());
        case AstNode::BOOL_LITERAL:
            return node->value.toBool() ? "true" : "false";
        default:
            return QString();
    }
}

} // namespace pmake::parser