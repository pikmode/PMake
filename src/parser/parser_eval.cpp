#include "parser/parser_impl.h"
#include "core/builtins.h"
#include <QRegularExpression>
#include <QStack>

namespace pmake::parser {

static QString getRawString(AstNode* node) {
    if (!node) return QString();
    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return node->value.toString();
        case AstNode::VARIABLE_REF:
            return "${" + node->name + "}";
        case AstNode::CONCAT: {
            QString result;
            for (auto child : node->children)
                result += getRawString(child);
            return result;
        }
        default:
            return QString();
    }
}

void ParserImpl::evaluateAst(AstNode* root) {
    if (!root) return;

    struct Frame {
        AstNode* node;
        int index;
    };

    QStack<Frame> stack;
    stack.push({root, 0});

    while (!stack.isEmpty()) {
        Frame& frame = stack.top();
        AstNode* node = frame.node;

        if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
            error("Too many operations in AST evaluation (max 500000)", false);
            return;
        }

        if (frame.index == 0) {
            switch (node->type) {
                case AstNode::ASSIGNMENT: evaluateAssignment(node); break;
                case AstNode::RULE: evaluateRule(node); break;
                case AstNode::BUILD: evaluateBuild(node); break;
                case AstNode::CONFIG: evaluateConfig(node); break;
                case AstNode::PLATFORM: evaluatePlatform(node); break;

                // --- Узлы, которые сами обрабатывают своих детей ---
                case AstNode::IF:
                    evaluateIf(node);
                    frame.index = node->children.size(); // пропускаем обход детей
                    break;
                case AstNode::FOREACH:
                    evaluateForeach(node);
                    frame.index = node->children.size(); // пропускаем обход детей
                    break;
                case AstNode::SWITCH:
                    evaluateSwitch(node);
                    frame.index = node->children.size(); // пропускаем обход детей
                    break;
                case AstNode::WHILE:
                    evaluateWhile(node);
                    frame.index = node->children.size(); // пропускаем обход детей
                    break;
                case AstNode::NAMESPACE:
                    evaluateNamespace(node);
                    frame.index = node->children.size(); // пропускаем обход детей
                    break;
                case AstNode::FUNCTION_DEF:
                    // Если у вас есть обработка FUNCTION_DEF, то тоже пропускаем детей
                    // (тело выполняется только при вызове, а не при определении)
                    // evaluateFunction(node);
                    // frame.index = node->children.size();
                    break;

                case AstNode::ALIAS: evaluateAlias(node); break;
                case AstNode::INCLUDE: evaluateInclude(node); break;
                case AstNode::IMPORT: evaluateImport(node); break;
                case AstNode::CONST_DECL: evaluateConst(node); break;
                case AstNode::ENUM_DECL: evaluateEnum(node); break;
                default: break;
            }
        }

        // Если узел имеет дочерние элементы и мы не пропустили их (frame.index < size),
        // то обрабатываем следующий дочерний узел
        if (frame.index < node->children.size()) {
            AstNode* child = node->children[frame.index++];
            if (child) {
                stack.push({child, 0});
            }
        } else {
            stack.pop();
        }
    }
}

void ParserImpl::evaluateAssignment(AstNode* assign) {
    QString newPart = evaluateExpression(assign->children[0]).toString();
    if (assign->isAppend) {
        QString current;
        if (!scopeStack_.isEmpty()) {
            auto opt = scopeStack_.top()->get(assign->name);
            if (opt.has_value()) current = *opt;
        }
        if (current.isEmpty()) {
            auto opt = state_.getGlobal(assign->name);
            if (opt.has_value()) current = *opt;
        }
        QString result = current.isEmpty() ? newPart : current + " " + newPart;
        if (!scopeStack_.isEmpty()) {
            scopeStack_.top()->set(assign->name, result);
        } else {
            state_.setGlobal(assign->name, result);
        }
    } else {
        if (!scopeStack_.isEmpty()) {
            scopeStack_.top()->set(assign->name, newPart);
        } else {
            state_.setGlobal(assign->name, newPart);
        }
    }
}

void ParserImpl::evaluateRule(AstNode* rule) {
    core::Rule coreRule;
    coreRule.name = rule->name;
    for (auto child : rule->children) {
        if (child->type == AstNode::ASSIGNMENT) {
            QString key = child->name;
            QString value = getRawString(child->children[0]);
            if (key == "command") coreRule.command = value;
            else if (key == "description") coreRule.description = value;
            else if (key == "generator") coreRule.generator = (value == "true" || value == "1");
            else if (key == "restat") coreRule.restat = (value == "true" || value == "1");
            else if (key == "depfile") coreRule.depfile = value;
            else if (key == "deps") coreRule.deps = value;
            else if (key == "rspfile") coreRule.rspfile = value;
            else if (key == "rspfile_content") coreRule.rspfile_content = value;
            else if (key == "pool") coreRule.pool = (value == "console" || value == "1");
        }
    }
    state_.addRule(coreRule);
}

void ParserImpl::evaluateBuild(AstNode* build) {
    core::Build coreBuild;
    if (build->children.size() >= 1) {
        AstNode* outNode = build->children[0];
        if (outNode->type == AstNode::CONCAT) {
            for (auto part : outNode->children) {
                QString out = evaluateExpression(part).toString();
                if (!out.isEmpty()) coreBuild.outputs.append(out);
            }
        } else {
            QString out = evaluateExpression(outNode).toString();
            if (!out.isEmpty()) coreBuild.outputs.append(out);
        }
    }
    if (build->children.size() >= 2) {
        AstNode* inNode = build->children[1];
        // Разбираем входы – если это CONCAT, обходим части, иначе разбиваем строку по пробелам
        if (inNode->type == AstNode::CONCAT) {
            for (auto part : inNode->children) {
                QString partStr = evaluateExpression(part).toString();
                QStringList parts = partStr.split(' ', Qt::SkipEmptyParts);
                for (const QString& p : parts) {
                    if (!p.isEmpty()) coreBuild.inputs.append(p);
                }
            }
        } else {
            QString in = evaluateExpression(inNode).toString();
            QStringList parts = in.split(' ', Qt::SkipEmptyParts);
            for (const QString& p : parts) {
                if (!p.isEmpty()) coreBuild.inputs.append(p);
            }
        }
    }
    coreBuild.rule_name = build->name;
    coreBuild.scope = QSharedPointer<core::Scope>::create();
    for (auto it = build->vars.begin(); it != build->vars.end(); ++it) {
        coreBuild.scope->set(it.key(), it.value());
    }
    state_.addBuild(coreBuild);
}

void ParserImpl::evaluateConfig(AstNode* config) {
    core::ConfigBlock block;
    block.name = config->name;
    for (auto child : config->children) {
        if (child->type == AstNode::ASSIGNMENT) {
            block.vars[child->name] = evaluateExpression(child->children[0]).toString();
        }
    }
    state_.addConfig(block);
}

void ParserImpl::evaluatePlatform(AstNode* platform) {
    core::PlatformBlock block;
    block.name = platform->name;
    for (auto child : platform->children) {
        if (child->type == AstNode::ASSIGNMENT) {
            block.vars[child->name] = evaluateExpression(child->children[0]).toString();
        }
    }
    state_.addPlatform(block);
}

void ParserImpl::evaluateIf(AstNode* ifNode) {
    bool condition = evaluateExpression(ifNode->children[0]).toBool();
    if (condition) {
        evaluateAst(ifNode->children[1]);
    } else {
        for (int i = 2; i < ifNode->children.size(); ++i) {
            auto child = ifNode->children[i];
            if (child->type == AstNode::ELSEIF) {
                bool elseIfCond = evaluateExpression(child->children[0]).toBool();
                if (elseIfCond) {
                    evaluateAst(child->children[1]);
                    break;
                }
            } else if (child->type == AstNode::ELSE) {
                evaluateAst(child->children[0]);
                break;
            }
        }
    }
}

void ParserImpl::evaluateForeach(AstNode* foreachNode) {
    QVariant listVal = evaluateExpression(foreachNode->children[0]);
    QVariantList list;
    if (listVal.metaType().id() == QMetaType::QVariantList) {
        list = listVal.toList();
    } else {
        QStringList parts = listVal.toString().split(' ', Qt::SkipEmptyParts);
        for (const QString& p : parts) list.append(p);
    }
    if (list.size() > MAX_FOREACH_ITERATIONS) {
        error(QString("Foreach list size %1 exceeds max %2").arg(list.size()).arg(MAX_FOREACH_ITERATIONS), true);
        return;
    }
    int iteration = 0;
    for (const QVariant& item : list) {
        if (++iteration > MAX_FOREACH_ITERATIONS) break;
        if (!scopeStack_.isEmpty()) {
            scopeStack_.top()->set(foreachNode->name, item.toString());
        } else {
            state_.setGlobal(foreachNode->name, item.toString());
        }
        evaluateAst(foreachNode->children[1]);
    }
}

void ParserImpl::evaluateAlias(AstNode* alias) {
    core::Build build;
    build.outputs = { alias->name };
    build.rule_name = "phony";
    for (auto child : alias->children) {
        if (child->type == AstNode::CONCAT) {
            for (auto part : child->children) {
                QString target = evaluateExpression(part).toString();
                if (!target.isEmpty()) build.inputs.append(target);
                else LOG_WARN(QString("Empty target in alias '%1'").arg(alias->name));
            }
        } else {
            QString target = evaluateExpression(child).toString();
            if (!target.isEmpty()) build.inputs.append(target);
            else LOG_WARN(QString("Empty target in alias '%1'").arg(alias->name));
        }
    }
    state_.addBuild(build);
}

void ParserImpl::evaluateInclude(AstNode* include) { Q_UNUSED(include); }
void ParserImpl::evaluateSwitch(AstNode* switchNode) {
    QVariant value = evaluateExpression(switchNode->children[0]);
    auto casesBlock = switchNode->children[1];
    bool matched = false;
    for (auto child : casesBlock->children) {
        if (child->type == AstNode::CASE) {
            QVariant caseValue = evaluateExpression(child->children[0]);
            if (caseValue == value) {
                evaluateAst(child->children[1]);
                matched = true;
                break;
            }
        } else if (child->type == AstNode::DEFAULT) {
            if (!matched) {
                evaluateAst(child->children[0]);
                break;
            }
        }
    }
}
void ParserImpl::evaluateWhile(AstNode* whileNode) {
    AstNode* condition = whileNode->children[0];
    AstNode* body = whileNode->children[1];
    int iteration = 0;
    while (evaluateExpression(condition).toBool()) {
        if (++iteration > 1000) {
            error("While loop exceeded 1000 iterations", true);
            break;
        }
        evaluateAst(body);
    }
}
void ParserImpl::evaluateImport(AstNode* importNode) { Q_UNUSED(importNode); }
void ParserImpl::evaluateNamespace(AstNode* namespaceNode) {
    core::Scope scope;
    scopeStack_.push(&scope);
    evaluateAst(namespaceNode->children[0]);
    scopeStack_.pop();
}
void ParserImpl::evaluateConst(AstNode* constNode) {
    QString value = evaluateExpression(constNode->children[0]).toString();
    state_.setGlobal(constNode->name, value);
}
void ParserImpl::evaluateEnum(AstNode* enumNode) {
    QStringList values;
    for (auto child : enumNode->children) {
        values.append(child->value.toString());
    }
    state_.setGlobal(enumNode->name, values.join(','));
}

QString ParserImpl::resolveVariable(const QString& name, QSet<QString>& visiting) {
    if (visiting.contains(name)) {
        error(QString("Circular variable reference detected: %1").arg(name), true);
        return QString();
    }
    visiting.insert(name);
    QString value;
    if (!scopeStack_.isEmpty()) {
        auto opt = scopeStack_.top()->get(name);
        if (opt.has_value()) value = *opt;
    }
    if (value.isEmpty()) {
        auto opt = state_.getGlobal(name);
        if (opt.has_value()) value = *opt;
    }
    visiting.remove(name);
    return value;
}

QVariant ParserImpl::evaluateExpression(AstNode* expr) {
    if (!expr) return QVariant();
    switch (expr->type) {
        case AstNode::STRING_LITERAL: {
            QString str = expr->value.toString();
            QRegularExpression re("\\$\\{([^}]+)\\}");
            QRegularExpressionMatchIterator it = re.globalMatch(str);
            int offset = 0;
            QString result;
            while (it.hasNext()) {
                auto match = it.next();
                result += str.mid(offset, match.capturedStart() - offset);
                QString varName = match.captured(1);
                QSet<QString> visiting;
                QString val = resolveVariable(varName, visiting);
                result += val;
                offset = match.capturedEnd();
            }
            result += str.mid(offset);
            return result;
        }
        case AstNode::NUMBER_LITERAL:
            return expr->value.toDouble();
        case AstNode::BOOL_LITERAL:
            return expr->value.toBool();
        case AstNode::NULL_LITERAL:
            return QVariant();
        case AstNode::VARIABLE_REF: {
            QSet<QString> visiting;
            return resolveVariable(expr->name, visiting);
        }
        case AstNode::FUNCTION_CALL: {
            QVector<QVariant> args;
            args.reserve(expr->children.size());
            for (auto arg : expr->children) {
                args.append(evaluateExpression(arg));
            }
            return callBuiltin(expr->name, args);
        }
        case AstNode::CONCAT: {
            QString result;
            for (auto child : expr->children) {
                result += evaluateExpression(child).toString();
            }
            return result;
        }
        case AstNode::BINARY_OP: {
            QVariant left = evaluateExpression(expr->left);
            QVariant right = evaluateExpression(expr->right);
            bool leftOk, rightOk;
            int leftNum = left.toInt(&leftOk);
            int rightNum = right.toInt(&rightOk);
            if (leftOk && rightOk) {
                switch (expr->op) {
                    case TOK_PLUS:  return QVariant(leftNum + rightNum);
                    case TOK_MINUS: return QVariant(leftNum - rightNum);
                    case TOK_STAR:  return QVariant(leftNum * rightNum);
                    case TOK_SLASH: return QVariant(rightNum != 0 ? leftNum / rightNum : 0);
                    case TOK_EQ:    return QVariant(leftNum == rightNum);
                    case TOK_NEQ:   return QVariant(leftNum != rightNum);
                    case TOK_LT:    return QVariant(leftNum < rightNum);
                    case TOK_GT:    return QVariant(leftNum > rightNum);
                    case TOK_LE:    return QVariant(leftNum <= rightNum);
                    case TOK_GE:    return QVariant(leftNum >= rightNum);
                    default: break;
                }
            } else {
                QString leftStr = left.toString();
                QString rightStr = right.toString();
                switch (expr->op) {
                    case TOK_PLUS:  return QVariant(leftStr + rightStr);
                    case TOK_EQ:    return QVariant(leftStr == rightStr);
                    case TOK_NEQ:   return QVariant(leftStr != rightStr);
                    case TOK_LT:    return QVariant(leftStr < rightStr);
                    case TOK_GT:    return QVariant(leftStr > rightStr);
                    case TOK_LE:    return QVariant(leftStr <= rightStr);
                    case TOK_GE:    return QVariant(leftStr >= rightStr);
                    case TOK_AND:   return QVariant(left.toBool() && right.toBool());
                    case TOK_OR:    return QVariant(left.toBool() || right.toBool());
                    default: break;
                }
            }
            return QVariant();
        }
        case AstNode::UNARY_OP: {
            QVariant operand = evaluateExpression(expr->left);
            if (expr->op == TOK_NOT)
                return QVariant(!operand.toBool());
            else if (expr->op == TOK_MINUS) {
                bool ok;
                int num = operand.toInt(&ok);
                if (ok) return QVariant(-num);
                return QVariant();
            }
            return operand;
        }
        case AstNode::LIST: {
            QVariantList list;
            for (auto child : expr->children) {
                list.append(evaluateExpression(child));
            }
            return QVariant(list);
        }
        default:
            return QVariant();
    }
}

QVariant ParserImpl::callBuiltin(const QString& name, const QVector<QVariant>& args) {
    if (state_.getFunction(name)) {
        if (++ParserImpl::functionCallDepth > 100) {
            error("Function call depth exceeds 100", true);
            --ParserImpl::functionCallDepth;
            return QVariant();
        }
        auto result = state_.callUserFunction(name, args, scopeStack_.isEmpty() ? nullptr : scopeStack_.top());
        --ParserImpl::functionCallDepth;
        return result;
    }
    return core::Builtins::call(name, args, &state_, scopeStack_.isEmpty() ? nullptr : scopeStack_.top());
}

} // namespace pmake::parser