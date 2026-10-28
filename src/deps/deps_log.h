// deps/deps_log.h
#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QSet>
#include <QFile>
#include <QByteArray>
#include <QRecursiveMutex>

namespace pmake::deps {

class DepsLog;

/**
 * @brief Запись о зависимостях для одного выходного файла.
 */
struct DepsEntry {
    QString output;
    QVector<QString> inputs;
    qint64 mtime = -1;
    bool isLoaded = false;
};

/**
 * @brief Хранилище неявных зависимостей (из depfile).
 *
 * Thread-safe: все публичные методы защищены recursive QRecursiveMutex,
 * потому что record() вызывается из воркеров Node::build().
 *
 * Non-copyable / non-movable: содержит mutex. Использовать через instance().
 */
class DepsLog {
public:
    static DepsLog& instance();

    DepsLog(const DepsLog&) = delete;
    DepsLog& operator=(const DepsLog&) = delete;
    DepsLog(DepsLog&&) = delete;
    DepsLog& operator=(DepsLog&&) = delete;

    // ------------------------------------------------------------------------
    // Загрузка / сохранение
    // ------------------------------------------------------------------------
    bool load(const QString& path);
    bool save(const QString& path) const;

    // ------------------------------------------------------------------------
    // Работа с записями
    // ------------------------------------------------------------------------
    void record(const QString& output, const QVector<QString>& inputs);
    QVector<QString> inputsFor(const QString& output) const;
    bool isDirty(const QString& output) const;

    QHash<QString, DepsEntry>& entries() { return entries_; }
    const QHash<QString, DepsEntry>& entries() const { return entries_; }

    void clear();

    // ------------------------------------------------------------------------
    // Динамический reverse index
    // ------------------------------------------------------------------------
    QHash<QString, QVector<QString>> buildReverseIndex() const;
    bool isInputChanged(const QString& input, qint64 currentMtime) const;

private:
    DepsLog() = default;

    QHash<QString, DepsEntry> entries_;
    mutable QHash<QString, qint64> mtimeCache_;
    mutable QRecursiveMutex mutex_;

    qint64 getFileMtime(const QString& path) const;
    void addToMtimeCache(const QString& path, qint64 mtime) const;

    void serialize(QDataStream& out) const;
    void deserialize(QDataStream& in);
};

} // namespace pmake::deps