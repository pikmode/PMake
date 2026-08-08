// deps/deps_log.cpp
#include "deps/deps_log.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include <QDataStream>
#include <QFile>
#include <QDir>

namespace pmake::deps {

// ----------------------------------------------------------------------------
// Singleton
// ----------------------------------------------------------------------------

DepsLog& DepsLog::instance() {
    static DepsLog log;
    return log;
}

// ----------------------------------------------------------------------------
// Загрузка/сохранение
// ----------------------------------------------------------------------------

bool DepsLog::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_DEBUG(QString("DepsLog: cannot open %1 for reading").arg(path));
        return false;
    }

    QDataStream in(&file);
    quint32 magic;
    in >> magic;
    if (magic != 0x44455053) { // "DEPS"
        LOG_WARN("DepsLog: invalid magic number");
        return false;
    }

    quint32 version;
    in >> version;
    if (version != 1) {
        LOG_WARN(QString("DepsLog: unsupported version %1").arg(version));
        return false;
    }

    deserialize(in);
    LOG_INFO(QString("DepsLog: loaded %1 entries from %2").arg(entries_.size()).arg(path));
    return true;
}

bool DepsLog::save(const QString& path) const {
    QFile file(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("DepsLog: cannot open %1 for writing").arg(path));
        return false;
    }

    QDataStream out(&file);
    out << quint32(0x44455053); // "DEPS"
    out << quint32(1); // version
    serialize(out);
    LOG_INFO(QString("DepsLog: saved %1 entries to %2").arg(entries_.size()).arg(path));
    return true;
}

// ----------------------------------------------------------------------------
// Сериализация
// ----------------------------------------------------------------------------

void DepsLog::serialize(QDataStream& out) const {
    out << quint32(entries_.size());
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        const DepsEntry& entry = it.value();
        out << entry.output;
        out << quint32(entry.inputs.size());
        for (const QString& in : entry.inputs) {
            out << in;
        }
        out << entry.mtime;
    }
}

void DepsLog::deserialize(QDataStream& in) {
    quint32 count;
    in >> count;
    entries_.clear();
    for (quint32 i = 0; i < count; ++i) {
        DepsEntry entry;
        in >> entry.output;
        quint32 inputCount;
        in >> inputCount;
        entry.inputs.clear();
        entry.inputs.reserve(inputCount);
        for (quint32 j = 0; j < inputCount; ++j) {
            QString input;
            in >> input;
            entry.inputs.append(input);
        }
        in >> entry.mtime;
        entry.isLoaded = true;
        entries_[entry.output] = entry;
    }
}

// ----------------------------------------------------------------------------
// Работа с записями
// ----------------------------------------------------------------------------

void DepsLog::record(const QString& output, const QVector<QString>& inputs) {
    DepsEntry entry;
    entry.output = output;
    entry.inputs = inputs;
    entry.mtime = getFileMtime(output);
    entry.isLoaded = true;
    entries_[output] = entry;
    mtimeCache_[output] = entry.mtime;
    LOG_DEBUG(QString("DepsLog: recorded %1 → %2 inputs").arg(output).arg(inputs.size()));
}

QVector<QString> DepsLog::inputsFor(const QString& output) const {
    auto it = entries_.find(output);
    if (it != entries_.end())
        return it.value().inputs;
    return QVector<QString>();
}

bool DepsLog::isDirty(const QString& output) const {
    auto it = entries_.find(output);
    if (it == entries_.end())
        return true; // нет записи – считается грязным
    qint64 curMtime = getFileMtime(output);
    if (curMtime != it.value().mtime)
        return true;
    // Можно также проверить все входы, но это делается на уровне графа
    return false;
}

// ----------------------------------------------------------------------------
// Динамический reverse index
// ----------------------------------------------------------------------------

QHash<QString, QVector<QString>> DepsLog::buildReverseIndex() const {
    QHash<QString, QVector<QString>> reverseIdx;

    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        const DepsEntry& entry = it.value();
        for (const QString& input : entry.inputs) {
            // Добавляем output в список для этого input
            if (!reverseIdx[input].contains(entry.output)) {
                reverseIdx[input].append(entry.output);
            }
        }
    }

    LOG_DEBUG(QString("DepsLog: built dynamic reverse index with %1 entries")
              .arg(reverseIdx.size()));
    return reverseIdx;
}

bool DepsLog::isInputChanged(const QString& input, qint64 currentMtime) const {
    // Мы не храним mtime для входов напрямую, но можем сравнить с сохранённым mtime
    // выходов, которые используют этот вход. Если хотя бы один выход имеет mtime
    // меньше, чем текущее mtime входа, то вход изменился.
    // Но эта логика сложнее, она реализована в Engine.
    return false;
}

// ----------------------------------------------------------------------------
// Вспомогательные методы
// ----------------------------------------------------------------------------

qint64 DepsLog::getFileMtime(const QString& path) const {
    auto it = mtimeCache_.find(path);
    if (it != mtimeCache_.end())
        return it.value();
    qint64 m = util::getFileMtime(path);
    mtimeCache_[path] = m;
    return m;
}

void DepsLog::addToMtimeCache(const QString& path, qint64 mtime) const {
    mtimeCache_[path] = mtime;
}

} // namespace pmake::deps