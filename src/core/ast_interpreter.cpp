#include "core/state.h"
#include "core/builtins.h"
#include "util/logging.h"
#include "parser/lexer.h"

namespace pmake::core {

QVariant State::callUserFunction(const QString& name,
                                 const QVector<QVariant>& args,
                                 Scope* callerScope) {
    const FunctionDef* func = getFunction(name);
    if (!func) {
        LOG_ERROR(QString("Function '%1' not defined").arg(name));
        return QVariant();
    }

    Scope localScope(callerScope);
    for (int i = 0; i < func->params.size(); ++i) {
        QString paramName = func->params[i];
        QString paramValue = (i < args.size()) ? args[i].toString() : QString();
        localScope.set(paramName, paramValue);
    }

    if (func->body) {
        for (AstNode* child : func->body->children) {
            QVariant result = executeAstNode(child, &localScope);
            if (child->type == AstNode::RETURN) {
                return result;
            }
        }
    }

    return QVariant();
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
                    case pmake::parser::TOK_PLUS:  return QVariant(leftNum + rightNum);
                    case pmake::parser::TOK_MINUS: return QVariant(leftNum - rightNum);
                    case pmake::parser::TOK_STAR:  return QVariant(leftNum * rightNum);
                    case pmake::parser::TOK_SLASH: return QVariant(rightNum != 0 ? leftNum / rightNum : 0);
                    case pmake::parser::TOK_EQ:    return QVariant(leftNum == rightNum);
                    case pmake::parser::TOK_NEQ:   return QVariant(leftNum != rightNum);
                    case pmake::parser::TOK_LT:    return QVariant(leftNum < rightNum);
                    case pmake::parser::TOK_GT:    return QVariant(leftNum > rightNum);
                    case pmake::parser::TOK_LE:    return QVariant(leftNum <= rightNum);
                    case pmake::parser::TOK_GE:    return QVariant(leftNum >= rightNum);
                    default: break;
                }
            } else {
                QString leftStr = left.toString();
                QString rightStr = right.toString();
                switch (node->op) {
                    case pmake::parser::TOK_PLUS:  return QVariant(leftStr + rightStr);
                    case pmake::parser::TOK_EQ:    return QVariant(leftStr == rightStr);
                    case pmake::parser::TOK_NEQ:   return QVariant(leftStr != rightStr);
                    case pmake::parser::TOK_LT:    return QVariant(leftStr < rightStr);
                    case pmake::parser::TOK_GT:    return QVariant(leftStr > rightStr);
                    case pmake::parser::TOK_LE:    return QVariant(leftStr <= rightStr);
                    case pmake::parser::TOK_GE:    return QVariant(leftStr >= rightStr);
                    case pmake::parser::TOK_AND:   return QVariant(left.toBool() && right.toBool());
                    case pmake::parser::TOK_OR:    return QVariant(left.toBool() || right.toBool());
                    default: break;
                }
            }
            return QVariant();
        }
        case AstNode::UNARY_OP: {
            QVariant operand = executeAstNode(node->left, localScope);
            if (node->op == pmake::parser::TOK_NOT)
                return QVariant(!operand.toBool());
            else if (node->op == pmake::parser::TOK_MINUS) {
                bool ok;
                int num = operand.toInt(&ok);
                if (ok) return QVariant(-num);
                return QVariant();
            }
            return operand;
        }
        case AstNode::FUNCTION_CALL: {
            QVector<QVariant> args;
            for (auto arg : node->children) {
                args.append(executeAstNode(arg, localScope));
            }
            if (getFunction(node->name)) {
                return callUserFunction(node->name, args, localScope);
            } else {
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

} // namespace pmake::core