#pragma once

#include <QString>
#include <QHash>
#include <QMutex>

namespace pmake::util {

// Кэш для mtime файлов – избегаем повторных stat
class FileCache {
public:
    static FileCache& instance();

    qint64 getMtime(const QString& path); // возвращает -1 если нет
    void touch(const QString& path);       // обновить кэш (после записи)
    void setMtime(const QString& path, qint64 mtime); // заполнить кэш напрямую
    void invalidate(const QString& path);  // если файл удалён
    void clear();

private:
    QHash<QString, qint64> cache_;
    QMutex mutex_;
};

// Утилиты
bool fileExists(const QString& path);     // ВСЕГДА реальный stat (без кэша)
qint64 getFileMtime(const QString& path); // через кэш

// Быстрый stat — один системный вызов отдает и mtime, и size.
// На Windows использует GetFileAttributesExW (в 10 раз быстрее QFileInfo).
// На Linux/macOS использует ::stat.
bool statFileFast(const QString& path, qint64& mtime, qint64& size);

bool readFile(const QString& path, QByteArray& out);
bool writeFile(const QString& path, const QByteArray& data);
bool ensureDirectory(const QString& path);

} // namespace pmake::util