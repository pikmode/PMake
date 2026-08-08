#pragma once

#include "parser/parser.h"
#include "parser/lexer.h"
#include "parser/parser_utils.h"
#include "core/state.h"
#include "core/ast.h"
#include "util/filesystem.h"
#include "util/logging.h"

#include <QStack>
#include <QSet>
#include <QVector>
#include <QHash>
#include <QString>
#include <memory>

namespace pmake::parser {

using pmake::core::AstNode;

class ParserImpl {
public:
    ParserImpl(const QByteArray& data, const QString& filename,
               const QString& config, const QString& platform);
    ~ParserImpl();

    std::unique_ptr<core::State> parse();

    // Глобальные счётчики
    static int operationCounter;
    static int includeDepth;
    static int functionCallDepth;

    // Константы ограничений (увеличены для стабильности)
    static constexpr int MAX_RULES = 10000;
    static constexpr int MAX_BUILDS = 10000;
    static constexpr int MAX_AST_NODES = 50000;
    static constexpr int MAX_FOREACH_ITERATIONS = 10000;
    static constexpr int MAX_INCLUDE_DEPTH = 50;
    static constexpr int MAX_EXPR_DEPTH = 100;
    static constexpr int MAX_STATEMENT_DEPTH = 100;
    static constexpr int MAX_OPERATIONS = 1000000; // увеличено до 1 млн

private:
    // ---- Данные ----
    Lexer lexer_;
    QString filename_;
    QString config_;
    QString platform_;
    core::State state_;
    QSet<QString> processingStack_;
    QSet<QString> definedRules_;
    QSet<QString> definedAliases_;
    QSet<QString> definedNamespaces_;
    QSet<QString> definedEnums_;
    QSet<QString> definedFunctions_;
    QSet<QString> definedBuildOutputs_;
    QStack<core::Scope*> scopeStack_;
    bool versionDeclared_ = false;
    bool versionChecked_ = false;
    double fileVersion_ = 0.0;

    AstNode* root_ = nullptr;
    QStack<AstNode*> astStack_;
    QVector<QString> errors_;
    bool hasError_ = false;

    Token cur_;
    int currentColumn_ = 0;

    // ---- Вспомогательные методы ----
    void advance();
    bool match(TokenType t);
    bool expect(TokenType t, const QString& msg);
    void error(const QString& msg, bool recover = true);
    QString str(int id) const;
    void skipNewlines();
    void skipNewlinesAndIndents();
    bool skipToBlockStart(int baseIndent);
    AstNode* currentBlock();
    void pushBlock(AstNode* node);
    void popBlock();

    // ---- Парсинг выражений ----
    AstNode* parseExpressionPrecedence(int minPrec = 0, int depth = 0);
    int getPrecedence(TokenType t);
    AstNode* parseAtom();
    AstNode* parseValueSequence(bool stopOnNewline = true);
    QVector<AstNode*> parseValueList();
    AstNode* parseValue();

    // ---- Парсинг конструкций ----
    void parseVersionDirective();
    QString parseIdent();
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
    AstNode* parseAttributeBlock();
    void parseBlockDirect(AstNode* block);

    // ---- Создание узлов AST ----
    AstNode* makeStringLiteral(const QString& value);
    AstNode* makeNumberLiteral(double value);
    AstNode* makeBoolLiteral(bool value);
    AstNode* makeNullLiteral();
    AstNode* makeVariableRef(const QString& name);
    AstNode* makeBinaryOp(int op, AstNode* left, AstNode* right);
    AstNode* makeUnaryOp(int op, AstNode* operand);
    AstNode* makeConcat(const QVector<AstNode*>& parts);
    AstNode* makeList(const QVector<AstNode*>& elements);

    // ---- Проверки ----
    bool checkDuplicate(const QString& name, const QSet<QString>& set, const QString& type);
    void addDefinition(const QString& name, QSet<QString>& set);
    void checkIncludeCycle(const QString& path);
    void checkType(AstNode* node, const QString& expectedType, const QString& context);
    void checkFunctionArgs(const QString& name, const QVector<QVariant>& args,
                           int minCount, int maxCount = -1);

    // ---- Выполнение AST (итеративное) ----
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

    QString resolveVariable(const QString& name, QSet<QString>& visiting);
    QVariant evaluateExpression(AstNode* expr);
    QVariant callBuiltin(const QString& name, const QVector<QVariant>& args);

    QString normalizePath(const QString& path);

    // ---- Вспомогательная функция для подстановки переменных на этапе парсинга ----
    static QString astNodeToStringWithState(AstNode* node, const core::State* state);
};

} // namespace pmake::parser