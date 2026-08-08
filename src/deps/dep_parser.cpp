#include "deps/dep_parser.h"
#include "util/logging.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace pmake::deps {

QVector<QString> DepParser::parse(const QString& depfile_path) {
    QFile file(depfile_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Не выводим предупреждение – при чистой сборке файла ещё нет
        return {};
    }

    QVector<QString> inputs;
    QTextStream in(&file);
    QString line;
    QString combined;
    bool isContinuation = false;

    while (in.readLineInto(&line)) {
        line = line.trimmed();
        if (line.isEmpty()) continue;

        if (line.endsWith('\\')) {
            line.chop(1);
            combined += line + " ";
            isContinuation = true;
        } else {
            combined += line + " ";
            isContinuation = false;
        }

        if (!isContinuation) {
            QString full = combined.trimmed();
            combined.clear();

            int colonPos = full.indexOf(':');
            if (colonPos == -1) continue;
            QString rest = full.mid(colonPos + 1).trimmed();

            QString temp = rest;
            temp.replace("\\ ", "\x01");
            QStringList tokens = temp.split(' ', Qt::SkipEmptyParts);
            for (QString& token : tokens) {
                token.replace("\x01", " ");
                if (token.startsWith('"') && token.endsWith('"')) {
                    token = token.mid(1, token.length()-2);
                } else if (token.startsWith('\'') && token.endsWith('\'')) {
                    token = token.mid(1, token.length()-2);
                }
                if (!token.isEmpty()) {
                    inputs.append(token);
                }
            }
        }
    }

    // Обработка остатка без завершающего newline
    if (!combined.isEmpty()) {
        QString full = combined.trimmed();
        int colonPos = full.indexOf(':');
        if (colonPos != -1) {
            QString rest = full.mid(colonPos + 1).trimmed();
            QString temp = rest;
            temp.replace("\\ ", "\x01");
            QStringList tokens = temp.split(' ', Qt::SkipEmptyParts);
            for (QString& token : tokens) {
                token.replace("\x01", " ");
                if (token.startsWith('"') && token.endsWith('"')) {
                    token = token.mid(1, token.length()-2);
                } else if (token.startsWith('\'') && token.endsWith('\'')) {
                    token = token.mid(1, token.length()-2);
                }
                if (!token.isEmpty()) {
                    inputs.append(token);
                }
            }
        }
    }

    file.close();
    return inputs;
}

} // namespace pmake::deps