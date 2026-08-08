#include "util/filesystem.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>

namespace pmake::util {

FileCache& FileCache::instance() {
    static FileCache f;
    return f;
}

qint64 FileCache::getMtime(const QString& path) {
    QMutexLocker lock(&mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end())
        return it.value();
    QFileInfo info(path);
    qint64 m = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    cache_[path] = m;
    return m;
}

void FileCache::touch(const QString& path) {
    QMutexLocker lock(&mutex_);
    cache_[path] = QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

void FileCache::invalidate(const QString& path) {
    QMutexLocker lock(&mutex_);
    cache_.remove(path);
}

void FileCache::clear() {
    QMutexLocker lock(&mutex_);
    cache_.clear();
}

bool fileExists(const QString& path) {
    return QFileInfo::exists(path);
}

qint64 getFileMtime(const QString& path) {
    return FileCache::instance().getMtime(path);
}

bool readFile(const QString& path, QByteArray& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    out = f.readAll();
    return true;
}

bool writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data) != data.size())
        return false;
    FileCache::instance().touch(path);
    return true;
}

bool ensureDirectory(const QString& path) {
    if (path.isEmpty()) return true;
    QDir dir(path);
    if (dir.exists()) return true;
    return dir.mkpath(".");
}

} // namespace pmake::util