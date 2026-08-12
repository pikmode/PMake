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
 */
class Cache {
public:
    Cache();

    bool save(const QString& path) const;
    bool load(const QString& path);

    // Всегда возвращает true, если кэш загружен (не проверяет mainFile)
    bool isValid(const QString& mainFile = QString()) const;

    // mainFile игнорируется
    void store(const State& state, const QString& mainFile = QString());
    std::unique_ptr<State> restore() const;

    const QHash<QString, FileState>& fileStates() const { return data_.fileStates; }
    const deps::DepsLog& depsLog() const { return data_.depsLog; }
    void setDepsLog(const deps::DepsLog& log) { data_.depsLog = log; }

private:
    struct Data {
        quint32 version = 5;          // новая версия
        // mainFile и includedFiles удалены
        QHash<QString, QString> globalVars;
        QVector<Rule> rules;
        QVector<Build> builds;
        QVector<QHash<QString, QString>> buildLocalVars;
        QHash<QString, Pool> pools;
        QVector<QString> defaultTargets;
        QVector<QString> includedFilesList;   // сохраняется, но не используется для валидации
        QHash<QString, FileState> fileStates; // только для листовых узлов
        deps::DepsLog depsLog;
    };

    Data data_;
    bool loaded_ = false;

    void serialize(QDataStream& out) const;
    void deserialize(QDataStream& in);
};

} // namespace pmake::core