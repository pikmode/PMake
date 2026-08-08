#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QVariant>

namespace pmake::core {

struct AstNode {
    enum Type {
        NONE,
        BLOCK,
        ASSIGNMENT,
        RULE,
        BUILD,
        CONFIG,
        PLATFORM,
        IF,
        ELSEIF,
        ELSE,
        FOREACH,
        FUNCTION_DEF,
        FUNCTION_CALL,
        ALIAS,
        INCLUDE,
        EXPRESSION,
        STRING_LITERAL,
        NUMBER_LITERAL,
        BOOL_LITERAL,
        VARIABLE_REF,
        BINARY_OP,
        UNARY_OP,
        LIST,
        CONCAT,
        RETURN,
        NULL_LITERAL,
        BREAK,
        CONTINUE,
        SWITCH,
        WHILE,
        IMPORT,
        NAMESPACE,
        CONST_DECL,
        ENUM_DECL,
        CASE,
        DEFAULT
    };

    Type type = NONE;
    QString name;
    QVector<AstNode*> children;
    QVariant value;
    QHash<QString, QString> vars;
    QString loopVar;
    QVariant loopList;
    AstNode* parent = nullptr;
    bool isExecuted = false;
    int line = 0;
    int column = 0;
    bool isAppend = false;   // для оператора +=

    AstNode* left = nullptr;
    AstNode* right = nullptr;
    int op = 0;

    QStringList params;
    AstNode* body = nullptr;

    // ---- новые поля для build ----
    int outputCount = 0;                     // количество выходных файлов
    QVector<AstNode*> attributes;            // атрибуты (ASSIGNMENT)

    ~AstNode() {
        qDeleteAll(children);
        qDeleteAll(attributes);
        delete left;
        delete right;
        delete body;
    }
};

} // namespace pmake::core