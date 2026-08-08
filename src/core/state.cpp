#include "core/state.h"
#include "util/logging.h"
#include "core/builtins.h"   // для вызова встроенных функций
#include <QRegularExpression>
#include <QDebug>

namespace pmake::core {

// ------------------------------------------------------------------
// Scope
// ------------------------------------------------------------------
Scope::Scope(Scope* parent) : parent_(parent) {}
void Scope::set(const QString& name, const QString& value) { vars_[name] = value; }
std::optional<QString> Scope::get(const QString& name) const {
    auto it = vars_.find(name);
    if (it != vars_.end()) return it.value();
    if (parent_) return parent_->get(name);
    return std::nullopt;
}

// ------------------------------------------------------------------
// State
// ------------------------------------------------------------------
State::State()
    : varRegex_(R"(\$\{([^}]+)\}|\$([a-zA-Z_][a-zA-Z0-9_]*)|{([^}]+)})") {}

State::~State() {
    // Удаляем AST тела функций
    for (auto& func : functions_) {
        delete func.body;
    }
}

// --- Глобальные переменные ---
void State::setGlobal(const QString& name, const QString& value) {
    global_scope_.set(name, value);
}
std::optional<QString> State::getGlobal(const QString& name) const {
    return global_scope_.get(name);
}
const QHash<QString, QString>& State::globals() const {
    return global_scope_.locals();
}

// --- Правила ---
void State::addRule(const Rule& rule) { rules_[rule.name] = rule; }
const Rule* State::getRule(const QString& name) const {
    auto it = rules_.find(name);
    return (it != rules_.end()) ? &it.value() : nullptr;
}
const QHash<QString, Rule>& State::rules() const { return rules_; }

// --- Сборки ---
void State::addBuild(const Build& build) { builds_.append(build); }
const QVector<Build>& State::builds() const { return builds_; }

// --- Пулы ---
void State::addPool(const Pool& pool) { pools_[pool.name] = pool; }
const Pool* State::getPool(const QString& name) const {
    auto it = pools_.find(name);
    return (it != pools_.end()) ? &it.value() : nullptr;
}
const QHash<QString, Pool>& State::pools() const { return pools_; }

// --- Состояния файлов ---
void State::setFileState(const QString& path, const FileState& state) {
    fileStates_[path] = state;
}
std::optional<FileState> State::getFileState(const QString& path) const {
    auto it = fileStates_.find(path);
    if (it != fileStates_.end()) return it.value();
    return std::nullopt;
}
const QHash<QString, FileState>& State::fileStates() const { return fileStates_; }

QVector<QString>& State::defaultTargets() { return defaultTargets_; }
const QVector<QString>& State::defaultTargets() const { return defaultTargets_; }

QVector<QString>& State::includedFiles() { return includedFiles_; }
const QVector<QString>& State::includedFiles() const { return includedFiles_; }

// --- Конфигурации ---
void State::addConfig(const ConfigBlock& block) { configs_[block.name] = block; }
bool State::hasConfig(const QString& name) const { return configs_.contains(name); }
const ConfigBlock* State::getConfig(const QString& name) const {
    auto it = configs_.find(name);
    return (it != configs_.end()) ? &it.value() : nullptr;
}
const QHash<QString, ConfigBlock>& State::configs() const { return configs_; }

// --- Платформы ---
void State::addPlatform(const PlatformBlock& block) { platforms_[block.name] = block; }
bool State::hasPlatform(const QString& name) const { return platforms_.contains(name); }
const PlatformBlock* State::getPlatform(const QString& name) const {
    auto it = platforms_.find(name);
    return (it != platforms_.end()) ? &it.value() : nullptr;
}
const QHash<QString, PlatformBlock>& State::platforms() const { return platforms_; }

// ------------------------------------------------------------------
// Пользовательские функции
// ------------------------------------------------------------------
void State::addFunction(const FunctionDef& func) {
    functions_[func.name] = func;
}
const FunctionDef* State::getFunction(const QString& name) const {
    auto it = functions_.find(name);
    return (it != functions_.end()) ? &it.value() : nullptr;
}
const QHash<QString, FunctionDef>& State::functions() const {
    return functions_;
}

QVariant State::callUserFunction(const QString& name,
                                 const QVector<QVariant>& args,
                                 Scope* callerScope) {
    const FunctionDef* func = getFunction(name);
    if (!func) {
        LOG_ERROR(QString("Function '%1' not defined").arg(name));
        return QVariant();
    }

    // Создаём локальный Scope с родителем callerScope
    Scope localScope(callerScope);

    // Привязываем аргументы к параметрам
    for (int i = 0; i < func->params.size(); ++i) {
        QString paramName = func->params[i];
        QString paramValue = (i < args.size()) ? args[i].toString() : QString();
        localScope.set(paramName, paramValue);
    }

    // Выполняем тело функции (блок)
    if (func->body) {
        for (AstNode* child : func->body->children) {
            QVariant result = executeAstNode(child, &localScope);
            // Если встретили return – возвращаем значение
            if (child->type == AstNode::RETURN) {
                return result;
            }
        }
    }

    return QVariant(); // если нет return
}

QVariant State::executeAstNode(AstNode* node, Scope* localScope) {
    if (!node) return QVariant();

    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return node->value;
        case AstNode::NUMBER_LITERAL:
            return node->value;
        case AstNode::BOOL_LITERAL:
            return node->value;
        case AstNode::VARIABLE_REF: {
            auto opt = localScope->get(node->name);
            if (opt.has_value()) return QVariant(*opt);
            auto optGlobal = getGlobal(node->name);
            if (optGlobal.has_value()) return QVariant(*optGlobal);
            return QVariant();
        }
        case AstNode::CONCAT: {
            QString result;
            for (auto child : node->children) {
                result += executeAstNode(child, localScope).toString();
            }
            return QVariant(result);
        }
        case AstNode::BINARY_OP: {
            QVariant left = executeAstNode(node->left, localScope);
            QVariant right = executeAstNode(node->right, localScope);
            bool leftOk, rightOk;
            int leftNum = left.toInt(&leftOk);
            int rightNum = right.toInt(&rightOk);
            if (leftOk && rightOk) {
                switch (node->op) {
                    case 1: return QVariant(leftNum == rightNum);  // ==
                    case 2: return QVariant(leftNum != rightNum);  // !=
                    case 3: return QVariant(leftNum < rightNum);   // <
                    case 4: return QVariant(leftNum > rightNum);   // >
                    case 5: return QVariant(leftNum <= rightNum);  // <=
                    case 6: return QVariant(leftNum >= rightNum);  // >=
                    default: break;
                }
            } else {
                QString leftStr = left.toString();
                QString rightStr = right.toString();
                switch (node->op) {
                    case 1: return QVariant(leftStr == rightStr);
                    case 2: return QVariant(leftStr != rightStr);
                    case 3: return QVariant(leftStr < rightStr);
                    case 4: return QVariant(leftStr > rightStr);
                    case 5: return QVariant(leftStr <= rightStr);
                    case 6: return QVariant(leftStr >= rightStr);
                    case 7: return QVariant(left.toBool() && right.toBool()); // &&
                    case 8: return QVariant(left.toBool() || right.toBool()); // ||
                    default: break;
                }
            }
            return QVariant();
        }
        case AstNode::UNARY_OP: {
            QVariant operand = executeAstNode(node->left, localScope);
            return QVariant(!operand.toBool());
        }
        case AstNode::FUNCTION_CALL: {
            QVector<QVariant> args;
            for (auto arg : node->children) {
                args.append(executeAstNode(arg, localScope));
            }
            // Сначала проверяем пользовательскую функцию
            if (getFunction(node->name)) {
                return callUserFunction(node->name, args, localScope);
            } else {
                // Встроенная функция
                return Builtins::call(node->name, args, this, localScope);
            }
        }
        case AstNode::ASSIGNMENT: {
            QString value = executeAstNode(node->children[0], localScope).toString();
            localScope->set(node->name, value);
            return QVariant();
        }
        case AstNode::RETURN: {
            if (!node->children.isEmpty()) {
                return executeAstNode(node->children[0], localScope);
            }
            return QVariant();
        }
        case AstNode::BLOCK: {
            for (auto child : node->children) {
                QVariant result = executeAstNode(child, localScope);
                if (child->type == AstNode::RETURN) {
                    return result;
                }
            }
            return QVariant();
        }
        default:
            return QVariant();
    }
}

// ------------------------------------------------------------------
// Подстановка переменных (без изменений)
// ------------------------------------------------------------------
QString State::expandVars(const QString& input, const Scope* local_scope) const {
    QHash<QString, QString> empty;
    return expandVarsWithSpecial(input, local_scope, empty);
}

QString State::expandVarsWithSpecial(const QString& input,
                                     const Scope* local_scope,
                                     const QHash<QString, QString>& specialVars) const {
    QString result = input;
    QRegularExpressionMatchIterator it = varRegex_.globalMatch(input);
    QList<QPair<int, int>> toReplace;
    QList<QString> replacements;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString varName;
        if (!match.captured(1).isEmpty()) {
            varName = match.captured(1); // ${var}
        } else if (!match.captured(2).isEmpty()) {
            varName = match.captured(2); // $var
        } else if (!match.captured(3).isEmpty()) {
            varName = match.captured(3); // {var}
        } else {
            continue;
        }
        QString value;
        bool found = false;
        auto specIt = specialVars.find(varName);
        if (specIt != specialVars.end()) { value = *specIt; found = true; }
        if (!found && local_scope) {
            auto opt = local_scope->get(varName);
            if (opt.has_value()) { value = *opt; found = true; }
        }
        if (!found) {
            auto opt = getGlobal(varName);
            if (opt.has_value()) { value = *opt; found = true; }
        }
        toReplace.append(qMakePair(match.capturedStart(), match.capturedLength()));
        replacements.append(found ? value : QString());
    }
    for (int i = toReplace.size() - 1; i >= 0; --i) {
        int start = toReplace[i].first;
        int len = toReplace[i].second;
        result.replace(start, len, replacements[i]);
    }
    return result;
}

} // namespace pmake::core