#include "parser/parser_impl.h"
#include <QSet>
#include <QFileInfo>

namespace pmake::parser {

// ---- Вспомогательная функция: преобразование AST в строку с подстановкой переменных ----
static QString astNodeToStringWithState(AstNode* node, const core::State* state) {
    if (!node) return QString();
    switch (node->type) {
        case AstNode::STRING_LITERAL:
            return node->value.toString();
        case AstNode::VARIABLE_REF: {
            auto opt = state->getGlobal(node->name);
            if (opt.has_value()) return *opt;
            return node->name;
        }
        case AstNode::CONCAT: {
            QString result;
            for (auto child : node->children) {
                result += astNodeToStringWithState(child, state);
            }
            return result;
        }
        case AstNode::NUMBER_LITERAL:
            return QString::number(node->value.toDouble());
        case AstNode::BOOL_LITERAL:
            return node->value.toBool() ? "true" : "false";
        default:
            return QString();
    }
}

// ---- parseStatement ----
void ParserImpl::parseStatement(int depth) {
    if (depth > MAX_STATEMENT_DEPTH) {
        error("Too deep recursion in parser (max 100)", true);
        return;
    }

    if (++ParserImpl::operationCounter > MAX_OPERATIONS) {
        error("Too many operations (max 1000000), aborting", false);
        return;
    }

    if (cur_.type != TOK_IDENT) {
        error("Expected statement (identifier)", true);
        advance();
        return;
    }

    QString word = str(cur_.strId);
    auto parent = currentBlock();

    if (word == "pmake_required_version") {
        if (!versionDeclared_) {
            advance();
            parseVersionDirective();
        } else {
            error("pmake_required_version may only be declared once.", true);
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
            if (cur_.type == TOK_NEWLINE) advance();
        }
        return;
    }

    if (word == "pmake") {
        error("'pmake' is deprecated. Use 'pmake_required_version = 1.0'.", true);
        advance();
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    if (word == "return") {
        advance();
        AstNode* expr = nullptr;
        if (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF && cur_.type != TOK_INDENT) {
            expr = parseValueSequence(true);
        } else {
            expr = makeStringLiteral("");
        }
        auto node = makeStringLiteral("");
        node->type = AstNode::RETURN;
        node->children.append(expr);
        parent->children.append(node);
        skipNewlinesAndIndents();
        return;
    }

    if (word == "config") {
        advance();
        if (cur_.type == TOK_EQUALS) {
            advance();
            auto val = parseValueSequence(true);
            auto node = makeStringLiteral("");
            node->type = AstNode::ASSIGNMENT;
            node->name = "config";
            node->children.append(val);
            parent->children.append(node);
            skipNewlinesAndIndents();
            return;
        } else {
            parseConfigBlock(parent);
            return;
        }
    }

    if (word == "clean") {
        advance();
        if (cur_.type != TOK_EQUALS) {
            error("Expected '=' after clean", true);
            while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
            if (cur_.type == TOK_NEWLINE) advance();
            return;
        }
        advance();
        auto target = parseValueSequence(true);
        auto node = makeStringLiteral("");
        node->type = AstNode::ALIAS;
        node->name = "clean";
        node->children.append(target);
        parent->children.append(node);
        skipNewlinesAndIndents();
        return;
    }

    if (word == "rule") {
        advance();
        parseRule(parent);
        return;
    }

    if (word == "build") {
        advance();
        parseBuild(parent);
        return;
    }

    if (word == "platform") {
        advance();
        parsePlatform(parent);
        return;
    }

    if (word == "if") {
        advance();
        parseIf(parent);
        return;
    }

    if (word == "foreach") {
        advance();
        parseForeach(parent);
        return;
    }

    if (word == "function") {
        advance();
        parseFunction(parent);
        return;
    }

    if (word == "alias") {
        advance();
        parseAlias(parent);
        return;
    }

    if (word == "include") {
        advance();
        parseInclude(parent);
        return;
    }

    if (word == "switch") {
        advance();
        parseSwitch(parent);
        return;
    }

    if (word == "while") {
        advance();
        parseWhile(parent);
        return;
    }

    if (word == "break") {
        parseBreak(parent);
        return;
    }

    if (word == "continue") {
        parseContinue(parent);
        return;
    }

    if (word == "import") {
        advance();
        parseImport(parent);
        return;
    }

    if (word == "namespace") {
        advance();
        parseNamespace(parent);
        return;
    }

    if (word == "const") {
        advance();
        parseConst(parent);
        return;
    }

    if (word == "enum") {
        advance();
        parseEnum(parent);
        return;
    }

    parseAssignment(parent);
}

// ---- parseAssignment ----
void ParserImpl::parseAssignment(AstNode* parent) {
    if (cur_.type != TOK_IDENT) {
        error("Expected identifier in assignment", true);
        advance();
        return;
    }

    QString name = parseIdent();

    bool isAppend = false;
    if (cur_.type == TOK_EQUALS) {
        isAppend = false;
        advance();
    } else if (cur_.type == TOK_PLUS_EQUALS) {
        isAppend = true;
        advance();
    } else {
        error("Expected '=' or '+='", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    auto valNode = parseValueSequence(true);
    auto node = makeStringLiteral("");
    node->type = AstNode::ASSIGNMENT;
    node->name = name;
    node->isAppend = isAppend;
    node->children.append(valNode);
    node->line = cur_.line;
    node->column = cur_.column;
    skipNewlinesAndIndents();
    parent->children.append(node);
}

// ---- parseRule ----
void ParserImpl::parseRule(AstNode* parent) {
    if (state_.rules().size() >= MAX_RULES) {
        error("Too many rules (max 10000)", true);
        return;
    }

    QString name = parseIdent();
    if (name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(name, definedRules_, "Rule")) return;
    addDefinition(name, definedRules_);

    auto node = makeStringLiteral("");
    node->type = AstNode::RULE;
    node->name = name;
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = block->children;
    block->children.clear();
    delete block;

    skipNewlinesAndIndents();

    core::Rule coreRule;
    coreRule.name = name;
    for (auto child : node->children) {
        if (child->type == AstNode::ASSIGNMENT) {
            QString key = child->name;
            QString value = astNodeToStringWithState(child->children[0], &state_);
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
    state_.addRule(std::move(coreRule));

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseBuild (БЕЗ добавления в state_) ----
void ParserImpl::parseBuild(AstNode* parent) {
    if (state_.builds().size() >= MAX_BUILDS) {
        error("Too many builds (max 10000)", true);
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::BUILD;
    node->line = cur_.line;
    node->column = cur_.column;

    // ---- Парсинг выходов ----
    QVector<AstNode*> outputs;
    int guard = 0;
    while (true) {
        if (++guard > 1000) {
            error("Infinite loop in build outputs", true);
            break;
        }
        if (cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF) {
            outputs.append(parseValue());
        } else if (cur_.type == TOK_IDENT) {
            error(QString("Expected string literal in quotes, got identifier '%1'. Use \"%1\".").arg(str(cur_.strId)), true);
            advance();
            continue;
        } else {
            break;
        }
        if (cur_.type == TOK_COMMA) advance();
        else break;
    }
    if (outputs.isEmpty()) {
        error("Expected at least one output for build", true);
        return;
    }
    if (outputs.size() > 1000) {
        error("Too many outputs in build (max 1000)", true);
        return;
    }

    // ---- Проверка дубликатов (исправлено: пропускаем строки с переменными) ----
    for (auto out : outputs) {
        if (out->type == AstNode::STRING_LITERAL) {
            QString outName = out->value.toString();
            // Пропускаем строки, содержащие переменные (они будут вычислены позже)
            if (!outName.isEmpty() && !outName.contains("${")) {
                if (definedBuildOutputs_.contains(outName)) {
                    error(QString("Build output '%1' already defined.").arg(outName), true);
                } else {
                    addDefinition(outName, definedBuildOutputs_);
                }
            }
        }
        // Если это не STRING_LITERAL (например, VARIABLE_REF или CONCAT), то пропускаем.
    }

    if (cur_.type != TOK_COLON) {
        error("Expected ':' after outputs", true);
        return;
    }
    advance();

    // ---- Имя правила ----
    if (cur_.type != TOK_IDENT) {
        error("Expected rule name (identifier)", true);
        return;
    }
    QString ruleName = str(cur_.strId);
    node->name = ruleName;
    advance();

    // ---- Парсинг входов (останавливаемся на новой строке или '{') ----
    QVector<AstNode*> inputs;
    guard = 0;
    while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF && cur_.type != TOK_LBRACE) {
        if (++guard > 1000) {
            error("Infinite loop in build inputs", true);
            break;
        }
        if (cur_.type == TOK_STRING || cur_.type == TOK_VAR_REF) {
            inputs.append(parseValue());
        } else if (cur_.type == TOK_IDENT) {
            error(QString("Expected string literal in quotes, got identifier '%1'. Use \"%1\".").arg(str(cur_.strId)), true);
            advance();
            continue;
        } else {
            break;
        }
        if (cur_.type == TOK_NEWLINE || cur_.type == TOK_EOF || cur_.type == TOK_LBRACE) break;
    }

    // ---- Локальный блок атрибутов ----
    QHash<QString, QString> vars;
    if (cur_.type == TOK_LBRACE) {
        advance();
        skipNewlinesAndIndents();
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            if (cur_.type == TOK_IDENT) {
                QString key = parseIdent();
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
                auto val = parseValueSequence(false);
                QString value = astNodeToStringWithState(val, &state_);
                if (isAppend) {
                    QString current = vars.value(key);
                    vars[key] = current.isEmpty() ? value : current + " " + value;
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
                QString val = astNodeToStringWithState(child->children[0], &state_);
                vars[child->name] = val;
                node->vars[child->name] = val;
            }
        }
        delete block;
    } else {
        if (cur_.type == TOK_NEWLINE) advance();
    }

    // ---- СОЗДАЁМ CONCAT-УЗЛЫ (всегда) ----
    auto outConcat = makeConcat(outputs);
    auto inConcat  = makeConcat(inputs);
    node->children.append(outConcat);
    node->children.append(inConcat);

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseConfigBlock ----
void ParserImpl::parseConfigBlock(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CONFIG;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = block->children;
    block->children.clear();
    delete block;

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endconfig") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parsePlatform ----
void ParserImpl::parsePlatform(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::PLATFORM;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    node->line = cur_.line;
    node->column = cur_.column;

    skipNewlines();
    auto block = parseAttributeBlock();
    node->children = block->children;
    block->children.clear();
    delete block;

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endplatform") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseIf ----
void ParserImpl::parseIf(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::IF;
    node->line = cur_.line;
    node->column = cur_.column;

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type != TOK_NEWLINE && cur_.type != TOK_INDENT && cur_.type != TOK_LBRACE) {
        error("Expected newline or block after if condition", true);
    }
    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block after if", true);
        return;
    }

    auto thenBlock = makeStringLiteral("");
    thenBlock->type = AstNode::BLOCK;
    pushBlock(thenBlock);
    parseBlockDirect(thenBlock);
    popBlock();
    node->children.append(thenBlock);

    int guard = 0;
    while (true) {
        if (++guard > 10000) {
            error("Infinite loop in if/elseif/else", true);
            break;
        }
        skipNewlinesAndIndents();
        if (cur_.type == TOK_IDENT && str(cur_.strId) == "elseif") {
            advance();
            auto elseifNode = makeStringLiteral("");
            elseifNode->type = AstNode::ELSEIF;
            elseifNode->children.append(parseExpressionPrecedence(0, 0));
            if (cur_.type == TOK_NEWLINE) advance();
            skipNewlines();
            if (!skipToBlockStart(0)) {
                error("Expected block after elseif", true);
                break;
            }
            auto elseIfBlock = makeStringLiteral("");
            elseIfBlock->type = AstNode::BLOCK;
            pushBlock(elseIfBlock);
            parseBlockDirect(elseIfBlock);
            popBlock();
            elseifNode->children.append(elseIfBlock);
            node->children.append(elseifNode);
        } else if (cur_.type == TOK_IDENT && str(cur_.strId) == "else") {
            advance();
            auto elseNode = makeStringLiteral("");
            elseNode->type = AstNode::ELSE;
            if (cur_.type == TOK_NEWLINE) advance();
            skipNewlines();
            if (!skipToBlockStart(0)) {
                error("Expected block after else", true);
                break;
            }
            auto elseBlock = makeStringLiteral("");
            elseBlock->type = AstNode::BLOCK;
            pushBlock(elseBlock);
            parseBlockDirect(elseBlock);
            popBlock();
            elseNode->children.append(elseBlock);
            node->children.append(elseNode);
            break;
        } else {
            break;
        }
    }

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endif") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseBlockDirect ----
void ParserImpl::parseBlockDirect(AstNode* block) {
    if (cur_.type == TOK_LBRACE) {
        advance(); // съедаем {
        skipNewlinesAndIndents(); // пропускаем начальные пробелы/переносы
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            // Если встретили пустую строку или отступ – пропускаем
            if (cur_.type == TOK_NEWLINE || cur_.type == TOK_INDENT) {
                skipNewlinesAndIndents();
                continue;
            }
            // Парсим один оператор
            parseStatement(0);
            // После оператора пропускаем пробелы и переносы
            skipNewlinesAndIndents();
        }
        expect(TOK_RBRACE, "Expected '}'");
        // После } может быть перевод строки – пропустим его снаружи
    } else if (cur_.type == TOK_INDENT) {
        int baseIndent = cur_.indent;
        advance(); // съедаем INDENT
        // Пропускаем возможные пустые строки после индента
        skipNewlines();
        while (true) {
            if (cur_.type == TOK_EOF) break;
            // Проверка на завершающие ключевые слова
            if (cur_.type == TOK_IDENT) {
                QString word = str(cur_.strId);
                if (word == "endforeach" || word == "endwhile" || word == "endif" ||
                    word == "endfunction" || word == "endnamespace" || word == "endswitch" ||
                    word == "endconfig" || word == "endplatform") {
                    break;
                }
            }
            // Выход по уменьшению отступа
            if (cur_.type == TOK_INDENT && cur_.indent <= baseIndent) {
                break;
            }
            // Пропускаем пустые строки
            if (cur_.type == TOK_NEWLINE) {
                advance();
                continue;
            }
            // Если текущий токен – INDENT с большим отступом, это начало вложенного блока,
            // но parseStatement сам обработает его, поэтому просто вызываем parseStatement.
            // Однако parseStatement ожидает IDENT, поэтому нужно убедиться, что мы не передаём INDENT.
            // В текущей логике, если cur_.type == TOK_INDENT, то это начало вложенного блока с отступом,
            // и parseStatement не сможет его обработать. Поэтому мы должны пропустить INDENT,
            // но тогда мы потеряем информацию об отступе для вложенных конструкций (if, foreach).
            // Однако эти конструкции используют skipToBlockStart, который проверяет INDENT.
            // Поэтому мы НЕ должны пропускать INDENT здесь. Вместо этого мы должны дать
            // возможность parseStatement увидеть INDENT как начало блока, но parseStatement
            // ожидает IDENT. Значит, нам нужно, чтобы перед вызовом parseStatement мы пропустили INDENT,
            // но только если он больше baseIndent? Но тогда мы потеряем отступ для вложенных блоков,
            // которые используют отступы вместо фигурных скобок.
            // В текущей реализации pmake для вложенных блоков используются фигурные скобки (в правилах, if и т.д.),
            // поэтому внутри блока с отступами мы не ожидаем вложенных блоков без фигурных скобок.
            // Поэтому можно безопасно пропустить INDENT, если он больше baseIndent.
            if (cur_.type == TOK_INDENT && cur_.indent > baseIndent) {
                // Это начало вложенного блока с отступом – но мы не поддерживаем такое внутри блока с отступами,
                // поэтому просто пропускаем INDENT и продолжаем.
                advance();
                continue;
            }
            // Если текущий токен – IDENT, парсим оператор
            if (cur_.type == TOK_IDENT) {
                parseStatement(0);
                // После оператора пропускаем пробелы и переносы
                skipNewlinesAndIndents();
            } else {
                // Неизвестный токен – ошибка
                error("Unexpected token in block", true);
                break;
            }
        }
    } else {
        error("Expected indentation or '{' for block", true);
    }
}

// ---- parseForeach ----
void ParserImpl::parseForeach(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::FOREACH;
    node->line = cur_.line;
    node->column = cur_.column;

    // ---- Имя переменной ----
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    // ---- Ключевое слово 'in' ----
    if (cur_.type != TOK_IDENT || str(cur_.strId) != "in") {
        error("Expected 'in' after foreach variable", true);
        return;
    }
    advance();

    // ---- Список (выражение) ----
    node->children.append(parseExpressionPrecedence(0, 0));

    // ---- Ожидаем начало блока (аналогично parseIf) ----
    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();

    if (!skipToBlockStart(0)) {
        error("Expected block after foreach", true);
        return;
    }

    // ---- Создаём блок и наполняем через parseBlockDirect ----
    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();

    node->children.append(bodyBlock);

    // ---- Если использовались фигурные скобки, parseBlockDirect уже съел '}'
    //      Если отступы – тоже всё корректно.

    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseFunction ----
void ParserImpl::parseFunction(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::FUNCTION_DEF;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedFunctions_, "Function")) return;
    addDefinition(node->name, definedFunctions_);

    expect(TOK_LPAREN, "Expected '('");
    QStringList params;
    while (cur_.type != TOK_RPAREN && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            params.append(str(cur_.strId));
            advance();
        } else {
            error("Expected parameter name", true);
            break;
        }
        if (cur_.type == TOK_COMMA) advance();
    }
    expect(TOK_RPAREN, "Expected ')'");
    if (cur_.type == TOK_NEWLINE) advance();

    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block for function body", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();

    core::FunctionDef func;
    func.name = node->name;
    func.params = params;
    func.body = bodyBlock;
    state_.addFunction(func);

    node->params = params;
    node->body = bodyBlock;
    parent->children.append(node);
}

// ---- parseAlias ----
void ParserImpl::parseAlias(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::ALIAS;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
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

    if (targets.isEmpty() && cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
        error("No valid targets for alias", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }

    for (auto target : targets) {
        node->children.append(target);
    }

    if (cur_.type == TOK_NEWLINE) advance();
    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseInclude ----
void ParserImpl::parseInclude(AstNode* parent) {
    if (++ParserImpl::includeDepth > MAX_INCLUDE_DEPTH) {
        error("Include depth exceeds 50", true);
        --ParserImpl::includeDepth;
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::INCLUDE;
    auto pathNode = parseValue();
    node->children.append(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    QString path = astNodeToStringWithState(pathNode, &state_);
    if (path.isEmpty()) {
        error("Include path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    QString canonical = normalizePath(path);
    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    QByteArray data;
    if (!util::readFile(path, data)) {
        error(QString("Cannot read include file: %1").arg(path), true);
        processingStack_.remove(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(data, path, config_, platform_);
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& rule : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& cfg : subState->configs()) state_.addConfig(cfg);
        for (const auto& plat : subState->platforms()) state_.addPlatform(plat);
        for (const auto& func : subState->functions()) state_.addFunction(func);
        for (const auto& key : subState->globals().keys()) {
            auto val = subState->getGlobal(key);
            if (val.has_value()) state_.setGlobal(key, *val);
        }
        state_.defaultTargets().append(subState->defaultTargets());
        state_.includedFiles().append(path);
    } else {
        error(QString("Failed to parse include: %1").arg(path), true);
    }

    processingStack_.remove(canonical);
    --ParserImpl::includeDepth;
    parent->children.append(node);
}

// ---- parseSwitch ----
void ParserImpl::parseSwitch(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::SWITCH;
    node->line = cur_.line;
    node->column = cur_.column;

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block after switch", true);
        return;
    }

    auto casesBlock = makeStringLiteral("");
    casesBlock->type = AstNode::BLOCK;
    pushBlock(casesBlock);
    parseBlockDirect(casesBlock);
    popBlock();

    for (auto child : casesBlock->children) {
        if (child->type != AstNode::CASE && child->type != AstNode::DEFAULT) {
            error("Only 'case' and 'default' allowed inside switch", true);
        }
    }
    node->children.append(casesBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endswitch") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseWhile ----
void ParserImpl::parseWhile(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::WHILE;
    node->line = cur_.line;
    node->column = cur_.column;

    node->children.append(parseExpressionPrecedence(0, 0));

    if (cur_.type == TOK_NEWLINE) advance();
    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block after while", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();
    node->children.append(bodyBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endwhile") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseBreak ----
void ParserImpl::parseBreak(AstNode* parent) {
    advance();
    auto node = makeStringLiteral("");
    node->type = AstNode::BREAK;
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.append(node);
    if (cur_.type == TOK_NEWLINE) advance();
}

// ---- parseContinue ----
void ParserImpl::parseContinue(AstNode* parent) {
    advance();
    auto node = makeStringLiteral("");
    node->type = AstNode::CONTINUE;
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.append(node);
    if (cur_.type == TOK_NEWLINE) advance();
}

// ---- parseImport ----
void ParserImpl::parseImport(AstNode* parent) {
    if (++ParserImpl::includeDepth > MAX_INCLUDE_DEPTH) {
        error("Import depth exceeds 50", true);
        --ParserImpl::includeDepth;
        return;
    }

    auto node = makeStringLiteral("");
    node->type = AstNode::IMPORT;
    node->line = cur_.line;
    node->column = cur_.column;

    auto pathNode = parseValue();
    node->children.append(pathNode);
    if (cur_.type == TOK_NEWLINE) advance();

    QString path = astNodeToStringWithState(pathNode, &state_);
    if (path.isEmpty()) {
        error("Import path is empty", true);
        --ParserImpl::includeDepth;
        return;
    }

    QString canonical = normalizePath(path);
    checkIncludeCycle(canonical);
    processingStack_.insert(canonical);

    QByteArray data;
    if (!util::readFile(path, data)) {
        error(QString("Cannot read import file: %1").arg(path), true);
        processingStack_.remove(canonical);
        --ParserImpl::includeDepth;
        return;
    }

    ParserImpl subParser(data, path, config_, platform_);
    auto subState = subParser.parse();
    if (subState) {
        for (const auto& rule : subState->rules()) state_.addRule(rule);
        for (const auto& build : subState->builds()) state_.addBuild(build);
        for (const auto& cfg : subState->configs()) state_.addConfig(cfg);
        for (const auto& plat : subState->platforms()) state_.addPlatform(plat);
        for (const auto& func : subState->functions()) state_.addFunction(func);
        for (const auto& key : subState->globals().keys()) {
            auto val = subState->getGlobal(key);
            if (val.has_value()) state_.setGlobal(key, *val);
        }
        state_.defaultTargets().append(subState->defaultTargets());
        state_.includedFiles().append(path);
    } else {
        error(QString("Failed to parse import: %1").arg(path), true);
    }

    processingStack_.remove(canonical);
    --ParserImpl::includeDepth;
    parent->children.append(node);
}

// ---- parseNamespace ----
void ParserImpl::parseNamespace(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::NAMESPACE;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedNamespaces_, "Namespace")) return;
    addDefinition(node->name, definedNamespaces_);

    skipNewlines();
    if (!skipToBlockStart(0)) {
        error("Expected block for namespace", true);
        return;
    }

    auto bodyBlock = makeStringLiteral("");
    bodyBlock->type = AstNode::BLOCK;
    pushBlock(bodyBlock);
    parseBlockDirect(bodyBlock);
    popBlock();
    node->children.append(bodyBlock);

    skipNewlinesAndIndents();
    if (cur_.type == TOK_IDENT && str(cur_.strId) == "endnamespace") {
        advance();
        skipNewlinesAndIndents();
    }

    parent->children.append(node);
}

// ---- parseConst ----
void ParserImpl::parseConst(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::CONST_DECL;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (cur_.type != TOK_EQUALS) {
        error("Expected '=' after const name", true);
        return;
    }
    advance();
    auto val = parseValueSequence(true);
    node->children.append(val);
    node->line = cur_.line;
    node->column = cur_.column;
    parent->children.append(node);
    skipNewlinesAndIndents();
}

// ---- parseEnum ----
void ParserImpl::parseEnum(AstNode* parent) {
    auto node = makeStringLiteral("");
    node->type = AstNode::ENUM_DECL;
    node->name = parseIdent();
    if (node->name.isEmpty()) {
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (checkDuplicate(node->name, definedEnums_, "Enum")) return;
    addDefinition(node->name, definedEnums_);

    if (cur_.type != TOK_LBRACE) {
        error("Expected '{' after enum name", true);
        return;
    }
    advance();

    QVector<AstNode*> enumerators;
    while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
        if (cur_.type == TOK_IDENT) {
            QString name = parseIdent();
            auto item = makeStringLiteral(name);
            enumerators.append(item);
            if (cur_.type == TOK_COMMA) advance();
        } else {
            error("Expected enumerator name", true);
            break;
        }
    }
    expect(TOK_RBRACE, "Expected '}'");
    node->children = enumerators;
    node->line = cur_.line;
    node->column = cur_.column;

    if (cur_.type == TOK_NEWLINE) advance();
    parent->children.append(node);
}

// ---- parseAttributeBlock ----
AstNode* ParserImpl::parseAttributeBlock() {
    auto block = makeStringLiteral("");
    block->type = AstNode::BLOCK;

    if (cur_.type == TOK_LBRACE) {
        advance();
        skipNewlinesAndIndents();
        int guard = 0;
        while (cur_.type != TOK_RBRACE && cur_.type != TOK_EOF) {
            if (++guard > 10000) {
                error("Infinite loop in attribute block (brace)", true);
                break;
            }
            if (cur_.type == TOK_IDENT) {
                QString key = parseIdent();
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
                auto val = parseValueSequence(false);
                auto assign = makeStringLiteral("");
                assign->type = AstNode::ASSIGNMENT;
                assign->name = key;
                assign->isAppend = isAppend;
                assign->children.append(val);
                block->children.append(assign);
                skipNewlinesAndIndents();
            } else {
                break;
            }
        }
        expect(TOK_RBRACE, "Expected '}'");
        if (cur_.type == TOK_NEWLINE) advance();
    } else {
        int baseIndent = cur_.indent;
        if (!skipToBlockStart(0)) {
            error("Expected block", true);
            return block;
        }
        int guard = 0;
        while (true) {
            if (++guard > 10000) {
                error("Infinite loop in attribute block (indent)", true);
                break;
            }
            if (cur_.type == TOK_EOF) break;
            if (cur_.type == TOK_INDENT && cur_.indent == baseIndent) {
                advance();
                break;
            }
            if (cur_.type == TOK_INDENT && cur_.indent < baseIndent) {
                break;
            }
            if (cur_.type == TOK_IDENT) {
                QString key = parseIdent();
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
                auto val = parseValueSequence(false);
                auto assign = makeStringLiteral("");
                assign->type = AstNode::ASSIGNMENT;
                assign->name = key;
                assign->isAppend = isAppend;
                assign->children.append(val);
                block->children.append(assign);
                skipNewlinesAndIndents();
            } else {
                break;
            }
        }
    }
    return block;
}

// ---- parseVersionDirective ----
void ParserImpl::parseVersionDirective() {
    if (versionDeclared_) {
        error("pmake_required_version may only be declared once.", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    if (cur_.type != TOK_EQUALS) {
        error("Expected '=' after pmake_required_version", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    advance();
    if (cur_.type != TOK_NUMBER) {
        error("Expected version number (e.g., 1.1)", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
        return;
    }
    fileVersion_ = cur_.doubleVal;
    advance();
    double currentVersion = getPmakeVersion();
    if (fileVersion_ > currentVersion) {
        error(QString("This project requires PMake %1. Current parser supports PMake %2.")
              .arg(fileVersion_).arg(currentVersion), false);
        return;
    }
    versionDeclared_ = true;
    versionChecked_ = true;
}

// ---- parseTopLevel ----
void ParserImpl::parseTopLevel() {
    advance();
    while (cur_.type == TOK_NEWLINE || cur_.type == TOK_INDENT) advance();

    if (cur_.type == TOK_EOF) {
        error("Empty file", true);
        return;
    }

    if (cur_.type != TOK_IDENT || str(cur_.strId) != "pmake_required_version") {
        error("First directive must be 'pmake_required_version = 1.0'", true);
        while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
        if (cur_.type == TOK_NEWLINE) advance();
    } else {
        advance();
        parseVersionDirective();
        if (!versionDeclared_) {
            if (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) {
                while (cur_.type != TOK_NEWLINE && cur_.type != TOK_EOF) advance();
                if (cur_.type == TOK_NEWLINE) advance();
            }
        }
    }

    skipNewlinesAndIndents();

    int loopGuard = 0;
    while (cur_.type != TOK_EOF) {
        if (++loopGuard > 50000) {
            error("Infinite loop in parser at top level", true);
            break;
        }
        parseStatement(0);
        skipNewlinesAndIndents();
    }
}

// ---- Публичный метод parse ----
std::unique_ptr<core::State> ParserImpl::parse() {
    try {
        parseTopLevel();
        if (!errors_.isEmpty()) {
            for (const QString& err : errors_) {
                LOG_ERROR(err);
            }
            return nullptr;
        }
        if (hasError_) {
            LOG_ERROR("Skipping AST evaluation due to parse errors.");
        }

        evaluateAst(root_);

    } catch (const std::exception& e) {
        LOG_ERROR(QString("Parsing failed: %1").arg(e.what()));
        return nullptr;
    } catch (...) {
        LOG_ERROR("Unknown exception during parsing");
        return nullptr;
    }

    if (!state_.getRule("phony")) {
        core::Rule phony;
        phony.name = "phony";
        phony.command = "";
        state_.addRule(std::move(phony));
    }
    return std::make_unique<core::State>(std::move(state_));
}

} // namespace pmake::parser