#include "parser/parser_impl.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pmake::parser {

// ---- parseRule ----
void ParserImpl::parseRule(AstNode* parent) {
    if (state_.rules().size() >= static_cast<std::size_t>(MAX_RULES)) {
        error("Too many rules (max 10000)", true);
        return;
    }

    std::string name{parseIdent()};
    if (name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::RULE;
    node->name = std::move(name);
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = std::move(block->children);
    delete block;

    skipNewlinesAndIndents();

    parent->children.push_back(node);
    skipNewlinesAndIndents();
}

// ---- parseBuild ----
void ParserImpl::parseBuild(AstNode* parent) {
    if (state_.builds().size() >= static_cast<std::size_t>(MAX_BUILDS)) {
        error("Too many builds (max 10000)", true);
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::BUILD;
    node->line = cur_.line;
    node->column = cur_.column;

    std::vector<AstNode*> outputs;
    int guard = 0;
    while (true) {
        if (++guard > 100000) { // FIX: было 1000
            error("Infinite loop in build outputs", true);
            break;
        }
        if (cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF) {
            outputs.push_back(parseAtom());
        } else if (cur_.type == TOK_IDENT) {
            error(std::format("Expected string literal in quotes, got identifier '{}'. Use \"{}\".",
                              str(cur_.strId), str(cur_.strId)), true);
            advance();
            continue;
        } else {
            break;
        }
        if (cur_.type == TOK_COMMA) advance();
        else break;
    }
    if (outputs.empty()) {
        error("Expected at least one output for build", true);
        return;
    }
    if (outputs.size() > 100000) { // FIX: было 1000
        error("Too many outputs in build (max 100000)", true);
        return;
    }

    for (auto out : outputs) {
        if (out->type == AstNode::STRING_LITERAL) {
            std::string outName = out->value.to_string();
            if (!outName.empty() && !outName.contains("${")) {
                if (definedBuildOutputs_.contains(outName)) {
                    error(std::format("Build output '{}' already defined.", outName), true);
                } else {
                    addDefinition(outName, definedBuildOutputs_);
                }
            }
        }
    }

    if (cur_.type != TOK_COLON) {
        error("Expected ':' after outputs", true);
        return;
    }
    advance();

    if (cur_.type != TOK_IDENT) {
        error("Expected rule name (identifier)", true);
        return;
    }
    std::string ruleName{str(cur_.strId)};
    node->name = ruleName;
    advance();

    std::vector<AstNode*> inputs;
    std::vector<AstNode*> implicitInputs;
    std::vector<AstNode*> orderOnlyInputs;
    int section = 0;
    guard = 0;
    while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF && cur_.type != TOK_LBRACE) {
        if (++guard > 100000) { // FIX: было 1000
            error("Infinite loop in build inputs", true);
            break;
        }
        if (cur_.type == TOK_PIPE) {
            if (section >= 1) {
                error("Duplicate '|' in build inputs", true);
            }
            section = 1;
            advance();
            continue;
        }
        if (cur_.type == TOK_OR) {
            if (section >= 2) {
                error("Duplicate '||' in build inputs", true);
            }
            section = 2;
            advance();
            continue;
        }
        if (cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF) {
            AstNode* val = parseAtom();
            if (section == 0)      inputs.push_back(val);
            else if (section == 1) implicitInputs.push_back(val);
            else                   orderOnlyInputs.push_back(val);
        } else if (cur_.type == TOK_IDENT) {
            error(std::format("Expected string literal in quotes, got identifier '{}'. Use \"{}\".",
                              str(cur_.strId), str(cur_.strId)), true);
            advance();
            continue;
        } else {
            break;
        }
        if (cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF || cur_.type == TOK_LBRACE) break;
    }

    std::unordered_map<std::string, std::string> vars;
    if (cur_.type == TOK_LBRACE) {
        advance();
        skipNewlinesAndIndents();
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            if (cur_.type == TOK_IDENT) {
                std::string key{parseIdent()};
                bool isAppend = false;
                if (cur_.type == TOK_EQUALS) {
                    isAppend = false;
                    advance();
                } else if (cur_.type == TOK_PLUS_EQUALS) {
                    isAppend = true;
                    advance();
                } else {
                    error("Expected '=' or '+='", true);
                    break;
                }
                // CHANGED: stopAtIdentAssign=true — для однострочных
                // build "out" : rule { a = "x" b = "y" }.
                auto val = parseValueSequence(false, /*stopAtIdentAssign=*/true);
                std::string value = astNodeToStringWithState(val, &state_);
                if (isAppend) {
                    std::string current;
                    auto it = vars.find(key);
                    if (it != vars.end()) current = it->second;
                    vars[key] = current.empty() ? value : current + " " + value;
                } else {
                    vars[key] = value;
                }
                node->vars[key] = vars[key];
                skipNewlinesAndIndents();
            } else {
                break;
            }
        }
        expect(TOK_RBRACE, "Expected '}'");
        if (cur_.type == TOK_NEWLINE) advance();
    } else if (cur_.type == TOK_INDENT && cur_.indent > 0) {
        auto block = parseAttributeBlock();
        for (auto child : block->children) {
            if (child->type == AstNode::ASSIGNMENT) {
                std::string val = astNodeToStringWithState(child->children[0], &state_);
                vars[child->name] = val;
                node->vars[child->name] = val;
            }
        }
        delete block;
    } else {
        if (cur_.type == TOK_NEWLINE) advance();
    }

    auto outConcat = makeConcat(outputs);
    auto inConcat  = makeConcat(inputs);
    node->children.push_back(outConcat);
    node->children.push_back(inConcat);

    if (!implicitInputs.empty()) {
        node->implicitInputs = makeConcat(implicitInputs);
    }
    if (!orderOnlyInputs.empty()) {
        node->orderOnlyInputs = makeConcat(orderOnlyInputs);
    }

    parent->children.push_back(node);
    skipNewlinesAndIndents();
}

// ---- parseConfigBlock ----
void ParserImpl::parseConfigBlock(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CONFIG;
    node->name = std::string{parseIdent()};
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = std::move(block->children);
    delete block;

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endconfig") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.push_back(node);
}

// ---- parsePlatform ----
void ParserImpl::parsePlatform(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::PLATFORM;
    node->name = std::string{parseIdent()};
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = std::move(block->children);
    delete block;

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endplatform") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.push_back(node);
}

// ---- parseAlias ----
void ParserImpl::parseAlias(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::ALIAS;
    node->name = std::string{parseIdent()};
    if (node->name.empty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedAliases_, "Alias")) return;
    addDefinition(node->name, definedAliases_);

    if (cur_.type != TOK_COLON) {
        error("Expected ':' after alias name", true);
        return;
    }
    advance();

    auto targets = parseValueList();

    if (targets.empty() && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
        error("No valid targets for alias", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    for (auto target : targets) {
        node->children.push_back(target);
    }

    if (cur_.type == TOK_NEWLINE) advance();
    parent->children.push_back(node);
    skipNewlinesAndIndents();
}

} // namespace pmake::parser