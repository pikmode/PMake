#include "core/builtins.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QThread>
#include <QCryptographicHash>
#include <QRegularExpression>

namespace pmake::core {

static QStringList toStringList(const QVector<QVariant>& args) {
    QStringList list;
    for (const QVariant& v : args) {
        if (v.canConvert<QVariantList>()) {
            QVariantList vl = v.toList();
            for (const QVariant& item : vl) list.append(item.toString());
        } else {
            list.append(v.toString());
        }
    }
    return list;
}

// --- Существующие функции ---

static QVariant globFunction(const QVector<QVariant>& args, State* state) {
    if (args.isEmpty()) return QVariant();
    QString pattern = args[0].toString();
    QStringList result;
    QDir dir;
    if (args.size() > 1) {
        QString base = args[1].toString();
        dir.setPath(base);
    } else {
        auto projDir = state->getGlobal("project_directory");
        if (projDir.has_value()) dir.setPath(*projDir);
        else dir.setPath(QDir::currentPath());
    }
    QStringList files = dir.entryList(QStringList(pattern), QDir::Files | QDir::NoDotAndDotDot);
    for (QString& f : files) f = dir.absoluteFilePath(f);
    QVariantList resultVariant;
    for (const QString& f : files) resultVariant.append(f);
    return QVariant(resultVariant);
}

static QVariant replaceSuffixFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return QVariant();
    QString newSuffix = args[1].toString();
    QVariantList items;
    if (args[0].canConvert<QVariantList>()) {
        items = args[0].toList();
    } else {
        items.append(args[0]);
    }
    QStringList result;
    for (const QVariant& v : items) {
        QString s = v.toString();
        QFileInfo fi(s);
        QString base = fi.completeBaseName();
        QString path = fi.absolutePath();
        result.append(path + "/" + base + newSuffix);
    }
    return QVariant(result);
}

static QVariant joinFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return QVariant();
    QStringList parts = toStringList(args);
    QString glue = parts.takeLast();
    return QVariant(parts.join(glue));
}

static QVariant relativePathFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return QVariant();
    QString path = args[0].toString();
    QString base = args[1].toString();
    QDir dir(base);
    return QVariant(dir.relativeFilePath(path));
}

static QVariant basenameFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString path = args[0].toString();
    QFileInfo fi(path);
    return QVariant(fi.fileName());
}

static QVariant dirnameFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString path = args[0].toString();
    QFileInfo fi(path);
    return QVariant(fi.absolutePath());
}

static QVariant envFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString name = args[0].toString();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(name)) return QVariant(env.value(name));
    return QVariant();
}

static QVariant existsFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return false;
    QString path = args[0].toString();
    return QVariant(util::fileExists(path));
}

static QVariant fileMtimeFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant(-1);
    QString path = args[0].toString();
    return QVariant(util::getFileMtime(path));
}

static QVariant readFileFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString path = args[0].toString();
    QByteArray data;
    if (util::readFile(path, data))
        return QVariant(QString::fromUtf8(data));
    return QVariant();
}

// --- Новые функции для PMake 1.0 ---

static QVariant extensionFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString path = args[0].toString();
    QFileInfo fi(path);
    return QVariant(fi.suffix());
}

static QVariant filenameFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString path = args[0].toString();
    QFileInfo fi(path);
    return QVariant(fi.fileName());
}

static QVariant splitFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return QVariant();
    QString str = args[0].toString();
    QString delimiter = args[1].toString();
    QStringList parts = str.split(delimiter, Qt::KeepEmptyParts);
    QVariantList result;
    for (const QString& p : parts) result.append(p);
    return QVariant(result);
}

static QVariant replaceFunction(const QVector<QVariant>& args) {
    if (args.size() < 3) return QVariant();
    QString str = args[0].toString();
    QString from = args[1].toString();
    QString to = args[2].toString();
    return QVariant(str.replace(from, to));
}

static QVariant startsWithFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return false;
    QString str = args[0].toString();
    QString prefix = args[1].toString();
    return QVariant(str.startsWith(prefix));
}

static QVariant endsWithFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return false;
    QString str = args[0].toString();
    QString suffix = args[1].toString();
    return QVariant(str.endsWith(suffix));
}

static QVariant containsFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return false;
    QString str = args[0].toString();
    QString substr = args[1].toString();
    return QVariant(str.contains(substr));
}

static QVariant cwdFunction() {
    return QVariant(QDir::currentPath());
}

static QVariant absolutePathFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString path = args[0].toString();
    QFileInfo fi(path);
    if (fi.isAbsolute())
        return QVariant(fi.absoluteFilePath());
    else
        return QVariant(QDir::current().absoluteFilePath(path));
}

static QVariant hashFunction(const QVector<QVariant>& args) {
    if (args.isEmpty()) return QVariant();
    QString str = args[0].toString();
    QByteArray data = str.toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
    return QVariant(QString::fromUtf8(hash.toHex()));
}

static QVariant writeFileFunction(const QVector<QVariant>& args) {
    if (args.size() < 2) return false;
    QString path = args[0].toString();
    QString content = args[1].toString();
    QByteArray data = content.toUtf8();
    bool ok = util::writeFile(path, data);
    return QVariant(ok);
}

QVariant Builtins::call(const QString& name,
                        const QVector<QVariant>& args,
                        State* state,
                        Scope* scope) {
    (void)scope;
    // Существующие
    if (name == "glob") return globFunction(args, state);
    if (name == "replace_suffix") return replaceSuffixFunction(args);
    if (name == "join") return joinFunction(args);
    if (name == "relative_path") return relativePathFunction(args);
    if (name == "basename") return basenameFunction(args);
    if (name == "dirname") return dirnameFunction(args);
    if (name == "env") return envFunction(args);
    if (name == "exists") return existsFunction(args);
    if (name == "file_mtime") return fileMtimeFunction(args);
    if (name == "read_file") return readFileFunction(args);

    // Новые
    if (name == "extension") return extensionFunction(args);
    if (name == "filename") return filenameFunction(args);
    if (name == "split") return splitFunction(args);
    if (name == "replace") return replaceFunction(args);
    if (name == "starts_with") return startsWithFunction(args);
    if (name == "ends_with") return endsWithFunction(args);
    if (name == "contains") return containsFunction(args);
    if (name == "cwd") return cwdFunction();
    if (name == "absolute_path") return absolutePathFunction(args);
    if (name == "hash") return hashFunction(args);
    if (name == "write_file") return writeFileFunction(args);

    LOG_WARN(QString("Unknown builtin function: %1").arg(name));
    return QVariant();
}

} // namespace pmake::core