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
        case AstNode::BOOL_LITERAL:
            return node->value.toBool() ? "true" : "false";
        case AstNode::NUMBER_LITERAL:
            return QString::number(node->value.toDouble());
        case AstNode::NULL_LITERAL:
            return QString();
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

                case AstNode::FUNCTION_CALL:
                    evaluateExpression(node);
                    frame.index = node->children.size();
                    break;

                case AstNode::IF:
                    evaluateIf(node);
                    frame.index = node->children.size();
                    break;
                case AstNode::FOREACH:
                    evaluateForeach(node);
                    frame.index = node->children.size();
                    break;
                case AstNode::SWITCH:
                    evaluateSwitch(node);
                    frame.index = node->children.size();
                    break;
                case AstNode::WHILE:
                    evaluateWhile(node);
                    frame.index = node->children.size();
                    break;
                case AstNode::NAMESPACE:
                    evaluateNamespace(node);
                    frame.index = node->children.size();
                    break;
                case AstNode::FUNCTION_DEF:
                    frame.index = node->children.size();
                    break;

                case AstNode::ALIAS: evaluateAlias(node); break;
                case AstNode::INCLUDE: evaluateInclude(node); break;
                case AstNode::IMPORT: evaluateImport(node); break;
                case AstNode::CONST_DECL: evaluateConst(node); break;
                case AstNode::ENUM_DECL: evaluateEnum(node); break;
                default: break;
            }
        }

        if (frame.index < node->children.size()) {
            AstNode* child = node->children[frame.index++];
            if (child) stack.push({child, 0});
        } else {
            stack.pop();
        }
    }
}

void ParserImpl::evaluateAssignment(AstNode* assign) {
    if (assign->children.isEmpty()) return;
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

    auto evalBool = [this](AstNode* child) -> bool {
        if (!child) return false;
        QVariant v = evaluateExpression(child);
        if (v.metaType().id() == QMetaType::Bool) return v.toBool();
        QString s = getRawString(child);
        return (s == "true" || s == "1" || s == "yes" || s == "on");
    };

    for (auto child : rule->children) {
        if (child->type != AstNode::ASSIGNMENT) continue;
        const QString& key = child->name;
        if (child->children.isEmpty()) continue;
        AstNode* valNode = child->children[0];

        if (key == "command")               coreRule.command = getRawString(valNode);
        else if (key == "description")      coreRule.description = getRawString(valNode);
        else if (key == "depfile")          coreRule.depfile = getRawString(valNode);
        else if (key == "deps")             coreRule.deps = getRawString(valNode);
        else if (key == "rspfile")          coreRule.rspfile = getRawString(valNode);
        else if (key == "rspfile_content")  coreRule.rspfile_content = getRawString(valNode);
        else if (key == "generator")        coreRule.generator = evalBool(valNode);
        else if (key == "restat")           coreRule.restat = evalBool(valNode);
        else if (key == "pool") {
            QString raw = getRawString(valNode);
            if (raw.startsWith("${") && raw.endsWith("}"))
                raw = raw.mid(2, raw.length() - 3);
            coreRule.pool = (raw == "console" || raw == "1" || raw == "true");
        }
    }
    state_.addRule(coreRule);
}

// Хелпер: добавить inputs из AST-узла в target-вектор с учётом
// STRING_LITERAL / VARIABLE_REF / CONCAT. Используется трижды —
// для inputs, implicit, order_only.
void ParserImpl::evaluateBuild(AstNode* build) {
    core::Build coreBuild;

    // ---- outputs ----
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

    // Хелпер: добавить один AST-узел как input(ы) в target.
    // STRING_LITERAL без ${} — атом (пробелы — часть имени).
    // STRING_LITERAL с ${} — раскрыть + split.
    // VARIABLE_REF / CONCAT / FUNCTION_CALL — раскрыть + split.
    auto addInput = [this](AstNode* part, QVector<QString>& target) {
        if (!part) return;
        switch (part->type) {
            case AstNode::STRING_LITERAL: {
                const QString& raw = part->value.toString();
                if (raw.contains("${")) {
                    QString expanded = evaluateExpression(part).toString();
                    for (const QString& p : expanded.split(' ', Qt::SkipEmptyParts))
                        if (!p.isEmpty()) target.append(p);
                } else if (!raw.isEmpty()) {
                    target.append(raw);
                }
                return;
            }
            case AstNode::VARIABLE_REF: {
                QString val = evaluateExpression(part).toString();
                for (const QString& p : val.split(' ', Qt::SkipEmptyParts))
                    if (!p.isEmpty()) target.append(p);
                return;
            }
            default: {
                QString val = evaluateExpression(part).toString();
                for (const QString& p : val.split(' ', Qt::SkipEmptyParts))
                    if (!p.isEmpty()) target.append(p);
                return;
            }
        }
    };

    // Хелпер: обработать CONCAT или одиночный узел — добавить в target.
    auto addInputsFromNode = [&addInput](AstNode* inNode, QVector<QString>& target) {
        if (!inNode) return;
        if (inNode->type == AstNode::CONCAT) {
            for (auto part : inNode->children) addInput(part, target);
        } else {
            addInput(inNode, target);
        }
    };

    // ---- inputs (обычные) ----
    if (build->children.size() >= 2) {
        addInputsFromNode(build->children[1], coreBuild.inputs);
    }

    // ---- implicit inputs (| "hdr.h") ----
    if (build->implicitInputs) {
        addInputsFromNode(build->implicitInputs, coreBuild.implicit);
    }

    // ---- order-only inputs (|| "stamp") ----
    if (build->orderOnlyInputs) {
        addInputsFromNode(build->orderOnlyInputs, coreBuild.order_only);
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
    if (ifNode->children.isEmpty()) return;
    bool condition = evaluateExpression(ifNode->children[0]).toBool();
    if (condition) {
        if (ifNode->children.size() >= 2) {
            evaluateAst(ifNode->children[1]);
        }
    } else {
        for (int i = 2; i < ifNode->children.size(); ++i) {
            auto child = ifNode->children[i];
            if (child->type == AstNode::ELSEIF) {
                if (child->children.size() < 2) continue;
                bool elseIfCond = evaluateExpression(child->children[0]).toBool();
                if (elseIfCond) {
                    evaluateAst(child->children[1]);
                    break;
                }
            } else if (child->type == AstNode::ELSE) {
                if (!child->children.isEmpty()) {
                    evaluateAst(child->children[0]);
                }
                break;
            }
        }
    }
}

void ParserImpl::evaluateForeach(AstNode* foreachNode) {
    if (foreachNode->children.isEmpty()) return;
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
        if (foreachNode->children.size() >= 2) {
            evaluateAst(foreachNode->children[1]);
        }
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
    if (switchNode->children.size() < 2) return;
    QVariant value = evaluateExpression(switchNode->children[0]);
    AstNode* casesBlock = switchNode->children[1];
    if (!casesBlock) return;

    bool matched = false;
    for (AstNode* child : casesBlock->children) {
        if (child->type == AstNode::CASE) {
            if (child->children.isEmpty()) continue;
            QVariant caseValue = evaluateExpression(child->children[0]);
            if (caseValue.toString() == value.toString()) {
                if (child->children.size() >= 2) {
                    evaluateAst(child->children[1]);
                }
                matched = true;
                break;
            }
        } else if (child->type == AstNode::DEFAULT) {
            if (!matched) {
                if (!child->children.isEmpty()) {
                    evaluateAst(child->children[0]);
                }
                break;
            }
        }
    }
}

void ParserImpl::evaluateWhile(AstNode* whileNode) {
    if (whileNode->children.size() < 2) return;
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
    if (namespaceNode->children.isEmpty()) return;
    core::Scope scope;
    scopeStack_.push(&scope);
    evaluateAst(namespaceNode->children[0]);
    scopeStack_.pop();
}

void ParserImpl::evaluateConst(AstNode* constNode) {
    if (constNode->children.isEmpty()) return;
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