#include "parser/parser_impl.h"
#include "core/builtins.h"
#include "util/filesystem.h"

#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmake::parser {

static std::string getRawString(AstNode* node) {
    if (!node) return {};
    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return node->value.to_string();
        case AstNode::VARIABLE_REF:
            return "${" + node->name + "}";
        case AstNode::CONCAT: {
            std::string result;
            for (auto child : node->children)
                result += getRawString(child);
            return result;
        }
        case AstNode::BOOL_LITERAL:
            return node->value.to_bool() ? "true" : "false";
        case AstNode::NUMBER_LITERAL:
            return std::format("{}", node->value.as_number());
        case AstNode::NULL_LITERAL:
            return {};
        default:
            return {};
    }
}

static std::string expandVariables(std::string_view str,
                                   const std::function<std::string(std::string_view)>& resolver) {
    std::string result;
    result.reserve(str.size());
    std::size_t i = 0;
    while (i < str.size()) {
        if (str[i] == '$' && i + 1 < str.size() && str[i + 1] == '{') {
            std::size_t end = str.find('}', i + 2);
            if (end != std::string_view::npos) {
                std::string_view varName = str.substr(i + 2, end - (i + 2));
                result += resolver(varName);
                i = end + 1;
                continue;
            }
        }
        result.push_back(str[i]);
        ++i;
    }
    return result;
}

static std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        std::size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

void ParserImpl::evaluateAst(AstNode* root) {
    if (!root) return;

    struct Frame { AstNode* node; int index; };
    std::vector<Frame> stack;
    stack.push_back({root, 0});

    while (!stack.empty()) {
        if (pendingFlow_ != 0) return;

        Frame& frame = stack.back();
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
                    frame.index = static_cast<int>(node->children.size());
                    break;
                case AstNode::IF:
                    evaluateIf(node);
                    frame.index = static_cast<int>(node->children.size());
                    break;
                case AstNode::FOREACH:
                    evaluateForeach(node);
                    frame.index = static_cast<int>(node->children.size());
                    break;
                case AstNode::SWITCH:
                    evaluateSwitch(node);
                    frame.index = static_cast<int>(node->children.size());
                    break;
                case AstNode::WHILE:
                    evaluateWhile(node);
                    frame.index = static_cast<int>(node->children.size());
                    break;
                case AstNode::NAMESPACE:
                    evaluateNamespace(node);
                    frame.index = static_cast<int>(node->children.size());
                    break;
                case AstNode::FUNCTION_DEF:
                    frame.index = static_cast<int>(node->children.size());
                    break;

                case AstNode::BREAK:    pendingFlow_ = 1; break;
                case AstNode::CONTINUE: pendingFlow_ = 2; break;

                case AstNode::ALIAS: evaluateAlias(node); break;
                case AstNode::INCLUDE: evaluateInclude(node); break;
                case AstNode::IMPORT: evaluateImport(node); break;
                case AstNode::CONST_DECL: evaluateConst(node); break;
                case AstNode::ENUM_DECL: evaluateEnum(node); break;
                default: break;
            }
        }

        if (frame.index < static_cast<int>(node->children.size())) {
            AstNode* child = node->children[frame.index++];
            if (child) stack.push_back({child, 0});
        } else {
            stack.pop_back();
        }
    }
}

void ParserImpl::evaluateAssignment(AstNode* assign) {
    if (assign->children.empty()) return;
    std::string newPart = evaluateExpression(assign->children[0]).to_string();
    if (assign->isAppend) {
        std::string current;
        if (!scopeStack_.empty()) {
            auto opt = scopeStack_.back()->get(assign->name);
            if (opt.has_value()) current = *opt;
        }
        if (current.empty()) {
            auto opt = state_.getGlobal(assign->name);
            if (opt.has_value()) current = *opt;
        }
        std::string result = current.empty() ? newPart : current + " " + newPart;
        if (!scopeStack_.empty()) scopeStack_.back()->set(assign->name, result);
        else                       state_.setGlobal(assign->name, result);
    } else {
        if (!scopeStack_.empty()) scopeStack_.back()->set(assign->name, newPart);
        else                       state_.setGlobal(assign->name, newPart);
    }
}

void ParserImpl::evaluateRule(AstNode* rule) {
    core::Rule coreRule;
    coreRule.name = rule->name;

    auto evalBool = [this](AstNode* child) -> bool {
        if (!child) return false;
        Value v = evaluateExpression(child);
        if (v.is_bool()) return v.as_bool();
        std::string s = getRawString(child);
        return (s == "true" || s == "1" || s == "yes" || s == "on");
    };

    for (auto child : rule->children) {
        if (child->type != AstNode::ASSIGNMENT) continue;
        const std::string& key = child->name;
        if (child->children.empty()) continue;
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
            std::string raw = getRawString(valNode);
            if (raw.size() >= 3 && raw.starts_with("${") && raw.ends_with("}"))
                raw = raw.substr(2, raw.size() - 3);
            coreRule.pool = (raw == "console" || raw == "1" || raw == "true");
        }
    }
    state_.addRule(coreRule);
}

void ParserImpl::evaluateBuild(AstNode* build) {
    core::Build coreBuild;

    auto addOutput = [this](AstNode* part, std::vector<std::string>& target) {
        if (!part) return;
        switch (part->type) {
            case AstNode::VARIABLE_REF: {
                Value v = evaluateExpression(part);
                if (v.is_list()) {
                    for (const auto& item : *v.as_list()) {
                        std::string s = item.to_string();
                        if (!s.empty()) target.push_back(std::move(s));
                    }
                } else {
                    for (auto& p : split_ws(v.to_string())) {
                        if (!p.empty()) target.push_back(std::move(p));
                    }
                }
                return;
            }
            case AstNode::STRING_LITERAL: {
                const std::string& raw = part->value.to_string();
                std::string expanded = evaluateExpression(part).to_string();
                if (raw.find("${") != std::string::npos) {
                    for (auto& p : split_ws(expanded)) {
                        if (!p.empty()) target.push_back(std::move(p));
                    }
                } else if (!expanded.empty()) {
                    target.push_back(std::move(expanded));
                }
                return;
            }
            default: {
                std::string s = evaluateExpression(part).to_string();
                if (!s.empty()) target.push_back(std::move(s));
                return;
            }
        }
    };

    if (build->children.size() >= 1) {
        AstNode* outNode = build->children[0];
        if (outNode->type == AstNode::CONCAT) {
            for (auto part : outNode->children) addOutput(part, coreBuild.outputs);
        } else {
            addOutput(outNode, coreBuild.outputs);
        }
    }

    auto addInput = [this](AstNode* part, std::vector<std::string>& target) {
        if (!part) return;
        switch (part->type) {
            case AstNode::STRING_LITERAL: {
                const std::string& raw = part->value.to_string();
                if (raw.find("${") != std::string::npos) {
                    std::string expanded = evaluateExpression(part).to_string();
                    for (auto& p : split_ws(expanded))
                        if (!p.empty()) target.push_back(std::move(p));
                } else if (!raw.empty()) {
                    target.push_back(raw);
                }
                return;
            }
            case AstNode::VARIABLE_REF: {
                std::string val = evaluateExpression(part).to_string();
                for (auto& p : split_ws(val))
                    if (!p.empty()) target.push_back(std::move(p));
                return;
            }
            default: {
                std::string val = evaluateExpression(part).to_string();
                for (auto& p : split_ws(val))
                    if (!p.empty()) target.push_back(std::move(p));
                return;
            }
        }
    };

    auto addInputsFromNode = [&addInput](AstNode* inNode, std::vector<std::string>& target) {
        if (!inNode) return;
        if (inNode->type == AstNode::CONCAT) {
            for (auto part : inNode->children) addInput(part, target);
        } else {
            addInput(inNode, target);
        }
    };

    if (build->children.size() >= 2) {
        addInputsFromNode(build->children[1], coreBuild.inputs);
    }
    if (build->implicitInputs) {
        addInputsFromNode(build->implicitInputs, coreBuild.implicit);
    }
    if (build->orderOnlyInputs) {
        addInputsFromNode(build->orderOnlyInputs, coreBuild.order_only);
    }

    coreBuild.rule_name = build->name;
    coreBuild.scope = std::make_shared<core::Scope>();
    for (const auto& [k, v] : build->vars) {
        coreBuild.scope->set(k, v);
    }
    state_.addBuild(coreBuild);
}

void ParserImpl::evaluateConfig(AstNode* config) {
    core::ConfigBlock block;
    block.name = config->name;
    for (auto child : config->children) {
        if (child->type == AstNode::ASSIGNMENT) {
            block.vars[child->name] = evaluateExpression(child->children[0]).to_string();
        }
    }
    state_.addConfig(block);
}

void ParserImpl::evaluatePlatform(AstNode* platform) {
    core::PlatformBlock block;
    block.name = platform->name;
    for (auto child : platform->children) {
        if (child->type == AstNode::ASSIGNMENT) {
            block.vars[child->name] = evaluateExpression(child->children[0]).to_string();
        }
    }
    state_.addPlatform(block);
}

void ParserImpl::evaluateIf(AstNode* ifNode) {
    if (ifNode->children.empty()) return;
    bool condition = evaluateExpression(ifNode->children[0]).to_bool();
    if (condition) {
        if (ifNode->children.size() >= 2) {
            evaluateAst(ifNode->children[1]);
        }
    } else {
        for (std::size_t i = 2; i < ifNode->children.size(); ++i) {
            auto child = ifNode->children[i];
            if (child->type == AstNode::ELSEIF) {
                if (child->children.size() < 2) continue;
                bool elseIfCond = evaluateExpression(child->children[0]).to_bool();
                if (elseIfCond) {
                    evaluateAst(child->children[1]);
                    break;
                }
            } else if (child->type == AstNode::ELSE) {
                if (!child->children.empty()) {
                    evaluateAst(child->children[0]);
                }
                break;
            }
        }
    }
}

void ParserImpl::evaluateForeach(AstNode* foreachNode) {
    if (foreachNode->children.empty()) return;
    Value listVal = evaluateExpression(foreachNode->children[0]);
    std::vector<std::string> items;
    if (listVal.is_list()) {
        for (const auto& v : *listVal.as_list()) {
            items.push_back(v.to_string());
        }
    } else {
        items = split_ws(listVal.to_string());
    }
    if (static_cast<int>(items.size()) > MAX_FOREACH_ITERATIONS) {
        error(std::format("Foreach list size {} exceeds max {}",
                          items.size(), MAX_FOREACH_ITERATIONS), true);
        return;
    }
    int iteration = 0;
    for (const auto& item : items) {
        if (++iteration > MAX_FOREACH_ITERATIONS) break;
        if (!scopeStack_.empty()) scopeStack_.back()->set(foreachNode->name, item);
        else                       state_.setGlobal(foreachNode->name, item);
        if (foreachNode->children.size() >= 2) {
            evaluateAst(foreachNode->children[1]);
        }
        if (pendingFlow_ == 1) { pendingFlow_ = 0; break; }
        if (pendingFlow_ == 2) { pendingFlow_ = 0; continue; }
    }
}

void ParserImpl::evaluateAlias(AstNode* alias) {
    core::Build build;
    build.outputs = { alias->name };
    build.rule_name = "phony";
    for (auto child : alias->children) {
        if (child->type == AstNode::CONCAT) {
            for (auto part : child->children) {
                std::string target = evaluateExpression(part).to_string();
                if (!target.empty()) build.inputs.push_back(std::move(target));
                else                 std::println(std::cerr, "Empty target in alias '{}'", alias->name);
            }
        } else {
            std::string target = evaluateExpression(child).to_string();
            if (!target.empty()) build.inputs.push_back(std::move(target));
            else                 std::println(std::cerr, "Empty target in alias '{}'", alias->name);
        }
    }
    state_.addBuild(build);
}

// ============================================================
// NEW (#4): evaluateInclude / evaluateImport через общую реализацию.
//
// На фазе eval — resolve path (переменные уже установлены) →
// cycle check → read file → parseAstOnly sub → eval sub-AST
// в parent state_ с временной подменой currentEvalFilename_.
// ============================================================
void ParserImpl::evaluateInclude(AstNode* include) {
    evaluateIncludeImpl(include, "include");
}

void ParserImpl::evaluateImport(AstNode* importNode) {
    evaluateIncludeImpl(importNode, "import");
}

void ParserImpl::evaluateIncludeImpl(AstNode* node, std::string_view kind) {
    if (!node || node->children.empty()) return;

    // 1. resolve path (теперь переменные установлены — если include внутри foreach/if)
    std::string rawPath = evaluateExpression(node->children[0]).to_string();
    if (rawPath.empty()) {
        error(std::format("{} path is empty", kind), true);
        return;
    }

    // 2. resolve относительно currentEvalFilename_ (а не корневого build.pmake)
    std::filesystem::path curPath(currentEvalFilename_);
    std::string absPath = (curPath.parent_path() / rawPath).lexically_normal().string();
    std::string canonical = normalizePath(absPath);

    // 3. cycle check
    if (processingStack_.contains(canonical)) {
        error(std::format("Include/import cycle detected: {}", canonical), false);
        return;
    }

    if (++ParserImpl::includeDepth > MAX_INCLUDE_DEPTH) {
        error("Include depth exceeds 500", true);
        --ParserImpl::includeDepth;
        return;
    }

    // 4. read file
    std::string data;
    if (!util::readFile(canonical, data)) {
        error(std::format("Cannot read {} file: {} (resolved: {})",
                          kind, rawPath, canonical), true);
        --ParserImpl::includeDepth;
        return;
    }

    // 5. parse only AST (без eval)
    ParserImpl subParser(data, canonical, config_, platform_);
    subParser.processingStack_ = processingStack_;
    subParser.parseAstOnly();

    // 6. копируем functions из sub в parent — ГЛУБОКАЯ копия через cloneAst,
    // иначе double-free: sub и parent оба удалят func.body при деструкторе
    for (const auto& [name, func] : subParser.state().functions()) {
        core::FunctionDef copy = func;
        copy.body = cloneAst(func.body);
        state_.addFunction(copy);
    }

    // 7. eval sub-AST в parent state_, с временной подменой filename
    processingStack_.insert(canonical);
    std::string savedFilename = currentEvalFilename_;
    currentEvalFilename_ = canonical;

    try {
        evaluateAst(subParser.root_);
    } catch (...) {
        currentEvalFilename_ = savedFilename;
        processingStack_.erase(canonical);
        --ParserImpl::includeDepth;
        throw;
    }

    currentEvalFilename_ = savedFilename;
    processingStack_.erase(canonical);
    --ParserImpl::includeDepth;

    // 8. трекаем include в state_ (для clean/отладки)
    state_.includedFiles().push_back(canonical);
}

void ParserImpl::evaluateSwitch(AstNode* switchNode) {
    if (switchNode->children.size() < 2) return;
    Value value = evaluateExpression(switchNode->children[0]);
    AstNode* casesBlock = switchNode->children[1];
    if (!casesBlock) return;

    bool matched = false;
    for (AstNode* child : casesBlock->children) {
        if (child->type == AstNode::CASE) {
            if (child->children.empty()) continue;
            Value caseValue = evaluateExpression(child->children[0]);
            if (caseValue.to_string() == value.to_string()) {
                if (child->children.size() >= 2) {
                    evaluateAst(child->children[1]);
                }
                matched = true;
                break;
            }
        } else if (child->type == AstNode::DEFAULT) {
            if (!matched) {
                if (!child->children.empty()) {
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
    while (evaluateExpression(condition).to_bool()) {
        if (++iteration > 1000) {
            error("While loop exceeded 1000 iterations", true);
            break;
        }
        evaluateAst(body);
        if (pendingFlow_ == 1) { pendingFlow_ = 0; break; }
        if (pendingFlow_ == 2) { pendingFlow_ = 0; continue; }
    }
}

void ParserImpl::evaluateNamespace(AstNode* namespaceNode) {
    if (namespaceNode->children.empty()) return;
    core::Scope scope;
    scopeStack_.push_back(&scope);
    evaluateAst(namespaceNode->children[0]);
    scopeStack_.pop_back();
}

void ParserImpl::evaluateConst(AstNode* constNode) {
    if (constNode->children.empty()) return;
    std::string value = evaluateExpression(constNode->children[0]).to_string();
    state_.setGlobal(constNode->name, value);
}

void ParserImpl::evaluateEnum(AstNode* enumNode) {
    std::string values;
    for (auto child : enumNode->children) {
        if (!values.empty()) values += ',';
        values += child->value.to_string();
    }
    state_.setGlobal(enumNode->name, values);
}

std::string ParserImpl::resolveVariable(std::string_view name,
                                        std::unordered_set<std::string>& visiting) {
    std::string nameStr(name);
    if (visiting.contains(nameStr)) {
        error(std::format("Circular variable reference detected: {}", name), true);
        return {};
    }
    visiting.insert(nameStr);
    std::string value;
    if (!scopeStack_.empty()) {
        auto opt = scopeStack_.back()->get(nameStr);
        if (opt.has_value()) value = *opt;
    }
    if (value.empty()) {
        auto opt = state_.getGlobal(nameStr);
        if (opt.has_value()) value = *opt;
    }
    visiting.erase(nameStr);
    return value;
}

Value ParserImpl::evaluateExpression(AstNode* expr) {
    if (!expr) return Value();
    switch (expr->type) {
        case AstNode::STRING_LITERAL: {
            std::string str = expr->value.to_string();
            return Value(expandVariables(str, [this](std::string_view varName) -> std::string {
                std::unordered_set<std::string> visiting;
                return resolveVariable(varName, visiting);
            }));
        }
        case AstNode::NUMBER_LITERAL:
            return Value(expr->value.as_number());
        case AstNode::BOOL_LITERAL:
            return Value(expr->value.to_bool());
        case AstNode::NULL_LITERAL:
            return Value();
        case AstNode::VARIABLE_REF: {
            std::unordered_set<std::string> visiting;
            return Value(resolveVariable(expr->name, visiting));
        }
        case AstNode::FUNCTION_CALL: {
            std::vector<Value> args;
            args.reserve(expr->children.size());
            for (auto arg : expr->children) {
                args.push_back(evaluateExpression(arg));
            }
            return callBuiltin(expr->name, args);
        }
        case AstNode::CONCAT: {
            std::string result;
            for (auto child : expr->children) {
                result += evaluateExpression(child).to_string();
            }
            return Value(std::move(result));
        }
        case AstNode::BINARY_OP: {
            Value left = evaluateExpression(expr->left);
            Value right = evaluateExpression(expr->right);
            if (left.is_number() && right.is_number()) {
                double l = left.as_number();
                double r = right.as_number();
                switch (expr->op) {
                    case TOK_PLUS:  return Value(l + r);
                    case TOK_MINUS: return Value(l - r);
                    case TOK_STAR:  return Value(l * r);
                    case TOK_SLASH: return Value(r != 0.0 ? l / r : 0.0);
                    case TOK_EQ:    return Value(l == r);
                    case TOK_NEQ:   return Value(l != r);
                    case TOK_LT:    return Value(l < r);
                    case TOK_GT:    return Value(l > r);
                    case TOK_LE:    return Value(l <= r);
                    case TOK_GE:    return Value(l >= r);
                    default: break;
                }
            } else {
                std::string l = left.to_string();
                std::string r = right.to_string();
                switch (expr->op) {
                    case TOK_PLUS:  return Value(l + r);
                    case TOK_EQ:    return Value(l == r);
                    case TOK_NEQ:   return Value(l != r);
                    case TOK_LT:    return Value(l < r);
                    case TOK_GT:    return Value(l > r);
                    case TOK_LE:    return Value(l <= r);
                    case TOK_GE:    return Value(l >= r);
                    case TOK_AND:   return Value(left.to_bool() && right.to_bool());
                    case TOK_OR:    return Value(left.to_bool() || right.to_bool());
                    default: break;
                }
            }
            return Value();
        }
        case AstNode::UNARY_OP: {
            Value operand = evaluateExpression(expr->left);
            if (expr->op == TOK_NOT) return Value(!operand.to_bool());
            if (expr->op == TOK_MINUS) {
                if (operand.is_number()) return Value(-operand.as_number());
                return Value();
            }
            return operand;
        }
        case AstNode::LIST: {
            ValueList list;
            for (auto child : expr->children) {
                list.push_back(evaluateExpression(child));
            }
            return Value(std::move(list));
        }
        default:
            return Value();
    }
}

Value ParserImpl::callBuiltin(std::string_view name, const std::vector<Value>& args) {
    std::string nameStr(name);
    if (state_.getFunction(nameStr)) {
        if (++ParserImpl::functionCallDepth > 100) {
            error("Function call depth exceeds 100", true);
            --ParserImpl::functionCallDepth;
            return Value();
        }
        Value result = state_.callUserFunction(
            nameStr, args,
            scopeStack_.empty() ? nullptr : scopeStack_.back());
        --ParserImpl::functionCallDepth;
        return result;
    }
    return core::Builtins::call(
        nameStr, args, &state_,
        scopeStack_.empty() ? nullptr : scopeStack_.back());
}

} // namespace pmake::parser