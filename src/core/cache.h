// core/cache.h
#pragma once

#include <QString>
#include <memory>
#include "core/state.h"
#include "deps/deps_log.h"

namespace pmake::core {

/**
 * @brief Бинарный кэш состояния сборки.
 *
 * Хранит только mtime для листовых узлов и не инвалидируется при изменении build.pmake.
 * Версия 5.
 *
 * DepsLog НЕ хранится по значению — используется singleton `DepsLog::instance()`
 * (он thread-safe и живёт всё время работы программы).
 */
class Cache {
public:
    Cache();

    bool save(const QString& path) const;
    bool load(const QString& path);

    bool isValid(const QString& mainFile = QString()) const;

    void store(const State& state, const QString& mainFile = QString());
    std::unique_ptr<State> restore() const;

    const QHash<QString, FileState>& fileStates() const { return data_.fileStates; }

    // DepsLog — доступ через singleton
    const deps::DepsLog& depsLog() const { return deps::DepsLog::instance(); }
    void setDepsLog(const deps::DepsLog& /*log*/) {
        // no-op: DepsLog — singleton, копирование не имеет смысла
    }

private:
    struct Data {
        quint32 version = 5;
        QHash<QString, QString> globalVars;
        QVector<Rule> rules;
        QVector<Build> builds;
        QVector<QHash<QString, QString>> buildLocalVars;
        QHash<QString, Pool> pools;
        QVector<QString> defaultTargets;
        QVector<QString> includedFilesList;
        QHash<QString, FileState> fileStates;
        // deps::DepsLog depsLog;  <-- УДАЛЕНО (используем singleton)
    };

    Data data_;
    bool loaded_ = false;

    void serialize(QDataStream& out) const;
    void deserialize(QDataStream& in);
};

} // namespace pmake::core