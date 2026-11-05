#pragma once

#include "parser/parser.h"
#include "parser/lexer.h"
#include "parser/parser_utils.h"
#include "core/state.h"
#include "core/ast.h"
#include "core/value.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace pmake::parser {

using pmake::core::AstNode;
using pmake::core::Value;
using pmake::core::ValueList;    // ← ДОБАВЛЕНО

class ParserImpl {
public:
    ParserImpl(std::string_view data, std::string_view filename,
               std::string_view config, std::string_view platform);
    ~ParserImpl();

    std::unique_ptr<core::State> parse();

    static int operationCounter;
    static int includeDepth;
    static int functionCallDepth;

    static constexpr int MAX_RULES = 10000;
    static constexpr int MAX_BUILDS = 10000;
    static constexpr int MAX_AST_NODES = 50000;
    static constexpr int MAX_FOREACH_ITERATIONS = 10000;
    static constexpr int MAX_INCLUDE_DEPTH = 50;
    static constexpr int MAX_EXPR_DEPTH = 100;
    static constexpr int MAX_STATEMENT_DEPTH = 100;
    static constexpr int MAX_OPERATIONS = 1000000;

private:
    Lexer lexer_;
    std::string filename_;
    std::string config_;
    std::string platform_;
    core::State state_;
    std::unordered_set<std::string> processingStack_;
    std::unordered_set<std::string> definedRules_;
    std::unordered_set<std::string> definedAliases_;
    std::unordered_set<std::string> definedNamespaces_;
    std::unordered_set<std::string> definedEnums_;
    std::unordered_set<std::string> definedFunctions_;
    std::unordered_set<std::string> definedBuildOutputs_;
    std::vector<core::Scope*> scopeStack_;
    bool versionDeclared_ = false;
    bool versionChecked_ = false;
    double fileVersion_ = 0.0;

    AstNode* root_ = nullptr;
    std::vector<AstNode*> astStack_;
    std::vector<std::string> errors_;
    bool hasError_ = false;

    Token cur_;
    int currentColumn_ = 0;

    void advance();
    bool match(TokenType t);
    bool expect(TokenType t, std::string_view msg);
    void error(std::string_view msg, bool recover = true);
    std::string_view str(int id) const;
    void skipNewlines();
    void skipNewlinesAndIndents();
    bool skipToBlockStart(int baseIndent);
    AstNode* currentBlock();
    void pushBlock(AstNode* node);
    void popBlock();

    AstNode* parseExpressionPrecedence(int minPrec = 0, int depth = 0);
    int getPrecedence(TokenType t);
    AstNode* parseAtom();
    AstNode* parseValueSequence(bool stopOnNewline = true);
    std::vector<AstNode*> parseValueList();
    AstNode* parseValue();

    void parseVersionDirective();
    std::string parseIdent();
    void parseTopLevel();
    void parseStatement(int depth = 0);
    void parseAssignment(AstNode* parent);
    void parseRule(AstNode* parent);
    void parseBuild(AstNode* parent);
    void parseConfigBlock(AstNode* parent);
    void parsePlatform(AstNode* parent);
    void parseIf(AstNode* parent);
    void parseForeach(AstNode* parent);
    void parseFunction(AstNode* parent);
    void parseAlias(AstNode* parent);
    void parseInclude(AstNode* parent);
    void parseSwitch(AstNode* parent);
    void parseWhile(AstNode* parent);
    void parseBreak(AstNode* parent);
    void parseContinue(AstNode* parent);
    void parseImport(AstNode* parent);
    void parseNamespace(AstNode* parent);
    void parseConst(AstNode* parent);
    void parseEnum(AstNode* parent);
    void parseCase(AstNode* parent);
    void parseDefault(AstNode* parent);
    AstNode* parseAttributeBlock();
    void parseBlockDirect(AstNode* block);

    AstNode* makeStringLiteral(std::string_view value);
    AstNode* makeNumberLiteral(double value);
    AstNode* makeBoolLiteral(bool value);
    AstNode* makeNullLiteral();
    AstNode* makeVariableRef(std::string_view name);
    AstNode* makeBinaryOp(int op, AstNode* left, AstNode* right);
    AstNode* makeUnaryOp(int op, AstNode* operand);
    AstNode* makeConcat(const std::vector<AstNode*>& parts);
    AstNode* makeList(const std::vector<AstNode*>& elements);

    bool checkDuplicate(std::string_view name,
                        const std::unordered_set<std::string>& set,
                        std::string_view type);
    void addDefinition(std::string_view name, std::unordered_set<std::string>& set);
    void checkIncludeCycle(std::string_view path);
    void checkType(AstNode* node, std::string_view expectedType, std::string_view context);
    void checkFunctionArgs(std::string_view name,
                           const std::vector<Value>& args,
                           int minCount, int maxCount = -1);

    void evaluateAst(AstNode* root);
    void evaluateAssignment(AstNode* assign);
    void evaluateRule(AstNode* rule);
    void evaluateBuild(AstNode* build);
    void evaluateConfig(AstNode* config);
    void evaluatePlatform(AstNode* platform);
    void evaluateIf(AstNode* ifNode);
    void evaluateForeach(AstNode* foreachNode);
    void evaluateAlias(AstNode* alias);
    void evaluateInclude(AstNode* include);
    void evaluateSwitch(AstNode* switchNode);
    void evaluateWhile(AstNode* whileNode);
    void evaluateImport(AstNode* importNode);
    void evaluateNamespace(AstNode* namespaceNode);
    void evaluateConst(AstNode* constNode);
    void evaluateEnum(AstNode* enumNode);

    std::string resolveVariable(std::string_view name,
                                std::unordered_set<std::string>& visiting);
    Value evaluateExpression(AstNode* expr);
    Value callBuiltin(std::string_view name, const std::vector<Value>& args);

    std::string normalizePath(std::string_view path);

    static std::string astNodeToStringWithState(AstNode* node, const core::State* state);
};

} // namespace pmake::parser