#include "core/state.h"
#include "core/builtins.h"
#include "parser/lexer.h"

#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmake::core {

Value State::callUserFunction(std::string_view name,
                              const std::vector<Value>& args,
                              Scope* callerScope) {
    const FunctionDef* func = getFunction(name);
    if (!func) {
        std::println(std::cerr, "Function '{}' not defined", name);
        return Value();
    }

    Scope* execScope = callerScope ? callerScope : &global_scope_;

    std::vector<std::pair<std::string, std::optional<std::string>>> savedParams;
    savedParams.reserve(func->params.size());

    for (std::size_t i = 0; i < func->params.size(); ++i) {
        const std::string& paramName = func->params[i];
        std::string paramValue = (i < args.size()) ? args[i].to_string() : std::string();

        savedParams.emplace_back(paramName, execScope->get(paramName));
        execScope->set(paramName, paramValue);
    }

    Value result;
    if (func->body) {
        for (AstNode* child : func->body->children) {
            result = executeAstNode(child, execScope);
            if (child->type == AstNode::RETURN) break;
        }
    }

    for (const auto& p : savedParams) {
        if (p.second.has_value()) execScope->set(p.first, *p.second);
        else                       execScope->remove(p.first);
    }

    return result;
}

Value State::executeAstNode(AstNode* node, Scope* localScope) {
    if (!node) return Value();

    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return Value(expandVars(node->value.to_string(), localScope));

        case AstNode::NUMBER_LITERAL:
        case AstNode::BOOL_LITERAL:
            return node->value;

        case AstNode::VARIABLE_REF: {
            auto opt = localScope->get(node->name);
            if (opt.has_value()) return Value(*opt);
            auto optGlobal = getGlobal(node->name);
            if (optGlobal.has_value()) return Value(*optGlobal);
            return Value();
        }

        case AstNode::CONCAT: {
            std::string result;
            for (auto child : node->children) {
                result += executeAstNode(child, localScope).to_string();
            }
            return Value(std::move(result));
        }

        case AstNode::BINARY_OP: {
            Value left = executeAstNode(node->left, localScope);
            Value right = executeAstNode(node->right, localScope);
            if (left.is_number() && right.is_number()) {
                double l = left.as_number();
                double r = right.as_number();
                switch (node->op) {
                    case pmake::parser::TOK_PLUS:  return Value(l + r);
                    case pmake::parser::TOK_MINUS: return Value(l - r);
                    case pmake::parser::TOK_STAR:  return Value(l * r);
                    case pmake::parser::TOK_SLASH: return Value(r != 0.0 ? l / r : 0.0);
                    case pmake::parser::TOK_EQ:    return Value(l == r);
                    case pmake::parser::TOK_NEQ:   return Value(l != r);
                    case pmake::parser::TOK_LT:    return Value(l < r);
                    case pmake::parser::TOK_GT:    return Value(l > r);
                    case pmake::parser::TOK_LE:    return Value(l <= r);
                    case pmake::parser::TOK_GE:    return Value(l >= r);
                    default: break;
                }
            } else {
                std::string l = left.to_string();
                std::string r = right.to_string();
                switch (node->op) {
                    case pmake::parser::TOK_PLUS:  return Value(l + r);
                    case pmake::parser::TOK_EQ:    return Value(l == r);
                    case pmake::parser::TOK_NEQ:   return Value(l != r);
                    case pmake::parser::TOK_LT:    return Value(l < r);
                    case pmake::parser::TOK_GT:    return Value(l > r);
                    case pmake::parser::TOK_LE:    return Value(l <= r);
                    case pmake::parser::TOK_GE:    return Value(l >= r);
                    case pmake::parser::TOK_AND:   return Value(left.to_bool() && right.to_bool());
                    case pmake::parser::TOK_OR:    return Value(left.to_bool() || right.to_bool());
                    default: break;
                }
            }
            return Value();
        }

        case AstNode::UNARY_OP: {
            Value operand = executeAstNode(node->left, localScope);
            if (node->op == pmake::parser::TOK_NOT)
                return Value(!operand.to_bool());
            if (node->op == pmake::parser::TOK_MINUS) {
                if (operand.is_number()) return Value(-operand.as_number());
                return Value();
            }
            return operand;
        }

        case AstNode::FUNCTION_CALL: {
            std::vector<Value> args;
            for (auto arg : node->children) {
                args.push_back(executeAstNode(arg, localScope));
            }
            if (getFunction(node->name)) {
                return callUserFunction(node->name, args, localScope);
            } else {
                return Builtins::call(node->name, args, this, localScope);
            }
        }

        case AstNode::ASSIGNMENT: {
            std::string value = executeAstNode(node->children[0], localScope).to_string();
            localScope->set(node->name, value);
            return Value();
        }

        case AstNode::RETURN: {
            if (!node->children.empty()) {
                return executeAstNode(node->children[0], localScope);
            }
            return Value();
        }

        case AstNode::BLOCK: {
            for (auto child : node->children) {
                Value result = executeAstNode(child, localScope);
                if (child->type == AstNode::RETURN) return result;
            }
            return Value();
        }

        default:
            return Value();
    }
}

} // namespace pmake::core