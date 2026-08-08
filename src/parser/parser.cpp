#include "parser/parser.h"
#include "parser/parser_impl.h"
#include "util/filesystem.h"
#include "util/logging.h"

namespace pmake::parser {

std::unique_ptr<core::State> Parser::parse(const QString& filename,
                                          const QString& config,
                                          const QString& platform) {
    QByteArray data;
    if (!util::readFile(filename, data)) {
        LOG_ERROR(QString("Cannot read file: %1").arg(filename));
        return nullptr;
    }
    ParserImpl impl(data, filename, config, platform);
    return impl.parse();
}

std::unique_ptr<core::State> Parser::parseFromString(const QByteArray& data,
                                                     const QString& filename,
                                                     const QString& config,
                                                     const QString& platform) {
    ParserImpl impl(data, filename, config, platform);
    return impl.parse();
}

} // namespace pmake::parser