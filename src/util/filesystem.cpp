#include "util/filesystem.h"
#include <QFile>
#include <QDateTime>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

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
        
    qint64 mtime = -1, size = 0;
    if (statFileFast(path, mtime, size)) {
        cache_[path] = mtime;
    } else {
        cache_[path] = -1;
    }
    return cache_[path];
}

void FileCache::touch(const QString& path) {
    QMutexLocker lock(&mutex_);
    qint64 mtime = -1, size = 0;
    if (statFileFast(path, mtime, size)) {
        cache_[path] = mtime;
    }
}

void FileCache::setMtime(const QString& path, qint64 mtime) {
    QMutexLocker lock(&mutex_);
    cache_[path] = mtime;
}

void FileCache::invalidate(const QString& path) {
    QMutexLocker lock(&mutex_);
    cache_.remove(path);
}

void FileCache::clear() {
    QMutexLocker lock(&mutex_);
    cache_.clear();
}

bool statFileFast(const QString& path, qint64& mtime, qint64& size) {
#ifdef Q_OS_WIN
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(reinterpret_cast<LPCWSTR>(path.utf16()),
                              GetFileExInfoStandard, &attr)) {
        mtime = -1;
        size = 0;
        return false;
    }
    ULARGE_INTEGER t;
    t.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    t.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    mtime = static_cast<qint64>(t.QuadPart / 10000ULL) - 11644473600000LL;

    ULARGE_INTEGER s;
    s.LowPart = attr.nFileSizeLow;
    s.HighPart = attr.nFileSizeHigh;
    size = static_cast<qint64>(s.QuadPart);
    return true;
#else
    struct stat st;
    if (::stat(path.toUtf8().constData(), &st) != 0) {
        mtime = -1;
        size = 0;
        return false;
    }
    mtime = static_cast<qint64>(st.st_mtime) * 1000LL;
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    mtime += st.st_mtim.tv_nsec / 1000000LL;
#elif defined(__APPLE__)
    mtime += st.st_mtimespec.tv_nsec / 1000000LL;
#endif
    size = static_cast<qint64>(st.st_size);
    return true;
#endif
}

// ВАЖНО: fileExists ВСЕГДА делает реальный stat, игнорируя кэш.
// Это нужно, чтобы после выполнения команды (g++, python) Pmake видел,
// что выходной файл реально создался.
bool fileExists(const QString& path) {
    qint64 mtime = -1, size = 0;
    return statFileFast(path, mtime, size);
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