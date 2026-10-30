#pragma once

#include <QString>
#include <memory>
#include "core/state.h"
#include "deps/deps_log.h"

namespace pmake::core {

/**
 * @brief Бинарный кэш состояния сборки.
 * Версия 6: добавлен commandHashes (детект изменения command / flags).
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
    const QHash<QString, QString>& commandHashes() const { return data_.commandHashes; }

    const deps::DepsLog& depsLog() const { return deps::DepsLog::instance(); }
    void setDepsLog(const deps::DepsLog& /*log*/) {}

private:
    struct Data {
        quint32 version = 6;
        QHash<QString, QString> globalVars;
        QVector<Rule> rules;
        QVector<Build> builds;
        QVector<QHash<QString, QString>> buildLocalVars;
        QHash<QString, Pool> pools;
        QVector<QString> defaultTargets;
        QVector<QString> includedFilesList;
        QHash<QString, FileState> fileStates;
        QHash<QString, QString> commandHashes;   // NEW
    };

    Data data_;
    bool loaded_ = false;

    void serialize(QDataStream& out) const;
    void deserialize(QDataStream& in);
};

} // namespace pmake::core