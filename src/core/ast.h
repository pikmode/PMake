#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/value.h"

namespace pmake::core {

struct AstNode {
    enum Type {
        NONE, BLOCK, ASSIGNMENT, RULE, BUILD, CONFIG, PLATFORM,
        IF, ELSEIF, ELSE, FOREACH, FUNCTION_DEF, FUNCTION_CALL,
        ALIAS, INCLUDE, EXPRESSION, STRING_LITERAL, NUMBER_LITERAL,
        BOOL_LITERAL, VARIABLE_REF, BINARY_OP, UNARY_OP, LIST,
        CONCAT, RETURN, NULL_LITERAL, BREAK, CONTINUE, SWITCH,
        WHILE, IMPORT, NAMESPACE, CONST_DECL, ENUM_DECL, CASE, DEFAULT
    };

    Type type = NONE;
    std::string name;
    std::vector<AstNode*> children;
    Value value;
    std::unordered_map<std::string, std::string> vars;
    std::string loopVar;
    Value loopList;
    AstNode* parent = nullptr;
    bool isExecuted = false;
    int line = 0;
    int column = 0;
    bool isAppend = false;

    AstNode* left = nullptr;
    AstNode* right = nullptr;
    int op = 0;

    std::vector<std::string> params;
    AstNode* body = nullptr;

    int outputCount = 0;
    std::vector<AstNode*> attributes;
    AstNode* implicitInputs = nullptr;
    AstNode* orderOnlyInputs = nullptr;

    ~AstNode() {
        for (auto* c : children)    delete c;
        for (auto* a : attributes)  delete a;
        delete left;
        delete right;
        delete body;
        delete implicitInputs;
        delete orderOnlyInputs;
    }
};

} // namespace pmake::core