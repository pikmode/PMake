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
 * Хранит:
 * - версию
 * - mtime главного файла и включённых
 * - глобальные переменные, правила, сборки, пулы
 * - локальные переменные для каждого build
 * - состояние файлов (mtime + size) для листовых узлов
 * - (опционально) кэш DepsLog
 */
class Cache {
public:
    Cache();

    bool save(const QString& path) const;
    bool load(const QString& path);

    bool isValid(const QString& mainFile) const;

    void store(const State& state, const QString& mainFile);
    std::unique_ptr<State> restore() const;

    // Доступ к кэшированным file states
    const QHash<QString, FileState>& fileStates() const { return data_.fileStates; }

    // Доступ к DepsLog (если сохранён)
    const deps::DepsLog& depsLog() const { return data_.depsLog; }
    void setDepsLog(const deps::DepsLog& log) { data_.depsLog = log; }

private:
    struct Data {
        quint32 version = 3; // увеличили версию
        QString mainFile;
        qint64 mainFileMtime = -1;
        QHash<QString, qint64> includedFiles; // путь -> mtime
        QHash<QString, QString> globalVars;
        QVector<Rule> rules;
        QVector<Build> builds;
        QVector<QHash<QString, QString>> buildLocalVars;
        QHash<QString, Pool> pools;
        QVector<QString> defaultTargets;
        QVector<QString> includedFilesList;

        // НОВОЕ: состояние файлов
        QHash<QString, FileState> fileStates;

        // НОВОЕ: кэш DepsLog (опционально)
        deps::DepsLog depsLog;
    };

    Data data_;
    bool loaded_ = false;

    void serialize(QDataStream& out) const;
    void deserialize(QDataStream& in);
};

} // namespace pmake::core