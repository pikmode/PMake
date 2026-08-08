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
    void invalidate(const QString& path);  // если файл удалён
    void clear();

private:
    QHash<QString, qint64> cache_;
    QMutex mutex_;
};

// Утилиты
bool fileExists(const QString& path);
qint64 getFileMtime(const QString& path); // через кэш
bool readFile(const QString& path, QByteArray& out);
bool writeFile(const QString& path, const QByteArray& data);
bool ensureDirectory(const QString& path); // создать папку, если её нет

} // namespace pmake::util