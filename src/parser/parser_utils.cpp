#include "parser/parser_utils.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include <QFile>

namespace pmake::parser {

double getPmakeVersion() {
    QByteArray data;

    // 1. Попытка прочитать из файловой системы (для обратной совместимости)
    if (!util::readFile("version.txt", data)) {
        // 2. Если не найден, пробуем из ресурсов Qt
        QFile resFile(":/version.txt");
        if (resFile.open(QIODevice::ReadOnly)) {
            data = resFile.readAll();
            resFile.close();
        }
    }

    if (data.isEmpty()) {
        LOG_WARN("version.txt not found, using default 1.0");
        return 1.0;
    }

    QString content = QString::fromUtf8(data).trimmed();
    bool ok;
    double version = content.toDouble(&ok);
    if (!ok) {
        LOG_WARN("Invalid version in version.txt, using default 1.0");
        return 1.0;
    }
    return version;
}

QString formatError(const QString& filename, int line, int column,
                    const QString& msg, const QString& token) {
    if (token.isEmpty())
        return QString("%1:%2:%3: error: %4").arg(filename).arg(line).arg(column).arg(msg);
    else
        return QString("%1:%2:%3: error: %4\n    %5").arg(filename).arg(line).arg(column).arg(msg).arg(token);
}

} // namespace pmake::parser