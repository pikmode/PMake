// deps/deps_log.h
#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QSet>
#include <QFile>
#include <QByteArray>

namespace pmake::deps {

// Forward declaration
class DepsLog;

/**
 * @brief Запись о зависимостях для одного выходного файла.
 */
struct DepsEntry {
    QString output;                 // путь к выходному файлу (например, main.o)
    QVector<QString> inputs;        // список входных файлов (включая заголовки)
    qint64 mtime = -1;              // mtime выходного файла на момент записи
    bool isLoaded = false;          // флаг, что запись загружена из кэша
};

/**
 * @brief Хранилище неявных зависимостей (из depfile).
 *
 * Поддерживает:
 * - запись зависимостей для выходного файла (output → inputs)
 * - загрузку/сохранение в бинарный файл
 * - построение динамического reverse index (input → outputs)
 * - проверку актуальности по mtime
 */
class DepsLog {
public:
    static DepsLog& instance();

    // ------------------------------------------------------------------------
    // Загрузка/сохранение
    // ------------------------------------------------------------------------

    bool load(const QString& path);
    bool save(const QString& path) const;

    // ------------------------------------------------------------------------
    // Работа с записями
    // ------------------------------------------------------------------------

    /**
     * @brief Записать зависимости для выходного файла.
     * @param output Путь к выходному файлу (объектный файл)
     * @param inputs Список входных файлов (включая заголовки)
     */
    void record(const QString& output, const QVector<QString>& inputs);

    /**
     * @brief Получить список входов для выходного файла.
     */
    QVector<QString> inputsFor(const QString& output) const;

    /**
     * @brief Проверить, изменился ли выходной файл (по mtime).
     */
    bool isDirty(const QString& output) const;

    /**
     * @brief Получить все сохранённые записи (не-const доступ для изменения).
     */
    QHash<QString, DepsEntry>& entries() { return entries_; }

    /**
     * @brief Получить все сохранённые записи (константный доступ).
     */
    const QHash<QString, DepsEntry>& entries() const { return entries_; }

    /**
     * @brief Очистить все записи.
     */
    void clear() { entries_.clear(); mtimeCache_.clear(); }

    // ------------------------------------------------------------------------
    // Динамический reverse index
    // ------------------------------------------------------------------------

    /**
     * @brief Построить обратный индекс: input → список output'ов, которые от него зависят.
     * Используется на этапе инкрементальной сборки.
     */
    QHash<QString, QVector<QString>> buildReverseIndex() const;

    /**
     * @brief Проверить, изменился ли входной файл (по сравнению с сохранённым mtime).
     * Используется для быстрой проверки листовых узлов.
     */
    bool isInputChanged(const QString& input, qint64 currentMtime) const;

private:
    QHash<QString, DepsEntry> entries_;           // output → entry
    mutable QHash<QString, qint64> mtimeCache_;   // кэш mtime для быстрого доступа

    // Внутренние методы
    qint64 getFileMtime(const QString& path) const;
    void addToMtimeCache(const QString& path, qint64 mtime) const;

    // Сериализация
    void serialize(QDataStream& out) const;
    void deserialize(QDataStream& in);
};

} // namespace pmake::deps