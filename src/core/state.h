#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QSharedPointer>
#include <optional>
#include <QMutex>
#include <QRegularExpression>
#include "core/ast.h"

namespace pmake::core {

class Scope;

struct Rule {
    QString name;
    QString command;
    QString description;
    bool generator = false;
    bool restat = false;
    QString depfile;
    QString deps;
    QString rspfile;
    QString rspfile_content;
    bool pool = false;
};

struct Build {
    QVector<QString> outputs;
    QVector<QString> inputs;
    QVector<QString> implicit;
    QVector<QString> order_only;
    QString rule_name;
    QSharedPointer<Scope> scope;
};

struct Pool {
    QString name;
    int depth = 1;
};

struct FileState {
    qint64 mtime = -1;
    qint64 size = 0;
    QString hash;
};

struct ConfigBlock {
    QString name;
    QHash<QString, QString> vars;
};

struct PlatformBlock {
    QString name;
    QHash<QString, QString> vars;
};

// Определение пользовательской функции (владеет AST-телом)
struct FunctionDef {
    QString name;
    QStringList params;
    AstNode* body = nullptr;   // блок с телом (тип BLOCK)
};

// Scope – контекст переменных
class Scope {
public:
    explicit Scope(Scope* parent = nullptr);
    void set(const QString& name, const QString& value);
    std::optional<QString> get(const QString& name) const;
    void remove(const QString& name);
    bool has(const QString& name) const;
    const QHash<QString, QString>& locals() const { return vars_; }

private:
    Scope* parent_;
    QHash<QString, QString> vars_;
};

// State – основное хранилище сборки.
//
// ВАЖНО: copy запрещён, move разрешён явно.
// Причина: custom ~State удаляет functions_[].body (сырые AstNode*).
// Без явного move-ctor std::move(state_) в ParserImpl::parse() вызовет
// COPY (неявный move заблокирован user-defined destructor'ом), и два State
// начнут владеть одним AstNode* -> double-free при разрушении
// (стек-буфер overrun 0xC0000409 при include файла с function()).
class State {
public:
    State();
    ~State();

    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&& other) noexcept;
    State& operator=(State&& other) noexcept;

    // --- Глобальные переменные ---
    void setGlobal(const QString& name, const QString& value);
    std::optional<QString> getGlobal(const QString& name) const;
    const QHash<QString, QString>& globals() const;

    // --- Правила ---
    void addRule(const Rule& rule);
    const Rule* getRule(const QString& name) const;
    const QHash<QString, Rule>& rules() const;

    // --- Сборки ---
    void addBuild(const Build& build);
    const QVector<Build>& builds() const;

    // --- Пулы ---
    void addPool(const Pool& pool);
    const Pool* getPool(const QString& name) const;
    const QHash<QString, Pool>& pools() const;

    // --- Состояния файлов ---
    void setFileState(const QString& path, const FileState& state);
    std::optional<FileState> getFileState(const QString& path) const;
    const QHash<QString, FileState>& fileStates() const;

    QVector<QString>& defaultTargets();
    const QVector<QString>& defaultTargets() const;

    QVector<QString>& includedFiles();
    const QVector<QString>& includedFiles() const;

    // --- Конфигурации ---
    void addConfig(const ConfigBlock& block);
    bool hasConfig(const QString& name) const;
    const ConfigBlock* getConfig(const QString& name) const;
    const QHash<QString, ConfigBlock>& configs() const;

    // --- Платформы ---
    void addPlatform(const PlatformBlock& block);
    bool hasPlatform(const QString& name) const;
    const PlatformBlock* getPlatform(const QString& name) const;
    const QHash<QString, PlatformBlock>& platforms() const;

    // --- Пользовательские функции ---
    void addFunction(const FunctionDef& func);
    const FunctionDef* getFunction(const QString& name) const;
    const QHash<QString, FunctionDef>& functions() const;

    // --- Command hashes (thread-safe) ---
    // setCommandHash вызывается из воркеров (Node::build), поэтому нужен mutex.
    // commandHashes() возвращает ссылку — использовать только из main-thread
    // после engine.run() (когда все воркеры завершены).
    void setCommandHash(const QString& output, const QString& hash) const;
    std::optional<QString> getCommandHash(const QString& output) const;
    void setCommandHashes(const QHash<QString, QString>& hashes);
    const QHash<QString, QString>& commandHashes() const;

    // Вызов пользовательской функции
    QVariant callUserFunction(const QString& name,
                              const QVector<QVariant>& args,
                              Scope* callerScope = nullptr);

    // Выполнение AST-узла в заданном контексте
    QVariant executeAstNode(AstNode* node, Scope* localScope);

    // --- Подстановка переменных ---
    QString expandVars(const QString& input, const Scope* local_scope = nullptr) const;
    QString expandVarsWithSpecial(const QString& input,
                                  const Scope* local_scope,
                                  const QHash<QString, QString>& specialVars) const;

private:
    Scope global_scope_;
    QHash<QString, Rule> rules_;
    QVector<Build> builds_;
    QHash<QString, Pool> pools_;
    QVector<QString> defaultTargets_;
    QVector<QString> includedFiles_;
    QHash<QString, FileState> fileStates_;
    QHash<QString, ConfigBlock> configs_;
    QHash<QString, PlatformBlock> platforms_;
    QHash<QString, FunctionDef> functions_;
    mutable QHash<QString, QString> commandHashes_;
    mutable QMutex commandHashesMutex_;   // thread-safety для setCommandHash
    QRegularExpression varRegex_;
};

} // namespace pmake::core