// core/cache.cpp
#include "core/cache.h"
#include "util/filesystem.h"
#include "util/logging.h"
#include "deps/deps_log.h"
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QDir>

namespace pmake::core {

Cache::Cache() = default;

void Cache::store(const State& state, const QString& /*mainFile*/) {
    data_.version = 5;
    // mainFile и его mtime не сохраняются

    data_.globalVars = state.globals();
    data_.rules = state.rules().values();
    data_.builds = state.builds();
    data_.pools = state.pools();
    data_.defaultTargets = state.defaultTargets();
    data_.includedFilesList = state.includedFiles();

    data_.buildLocalVars.clear();
    for (const Build& build : state.builds()) {
        QHash<QString, QString> locals;
        if (build.scope) {
            locals = build.scope->locals();
        }
        data_.buildLocalVars.append(locals);
    }

    // Сохраняем только состояния файлов (листовые узлы)
    data_.fileStates = state.fileStates();

    // Сохраняем DepsLog
    data_.depsLog = deps::DepsLog::instance();
}

bool Cache::save(const QString& path) const {
    QFile file(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("Cannot open cache file for writing: " + path);
        return false;
    }
    QDataStream out(&file);
    serialize(out);
    return true;
}

void Cache::serialize(QDataStream& out) const {
    out << quint32(0x504D4B45); // "PMKE"
    out << quint32(data_.version);

    // --- global vars ---
    out << quint32(data_.globalVars.size());
    for (auto it = data_.globalVars.begin(); it != data_.globalVars.end(); ++it) {
        out << it.key() << it.value();
    }

    // --- rules ---
    out << quint32(data_.rules.size());
    for (const Rule& rule : data_.rules) {
        out << rule.name << rule.command << rule.depfile << rule.deps
            << rule.generator << rule.restat << rule.rspfile << rule.rspfile_content
            << rule.pool << rule.description;
    }

    // --- builds ---
    out << quint32(data_.builds.size());
    for (int i = 0; i < data_.builds.size(); ++i) {
        const Build& build = data_.builds[i];
        out << build.outputs << build.inputs << build.implicit << build.order_only
            << build.rule_name;
        const QHash<QString, QString>& locals = data_.buildLocalVars[i];
        out << quint32(locals.size());
        for (auto it = locals.begin(); it != locals.end(); ++it) {
            out << it.key() << it.value();
        }
    }

    // --- pools ---
    out << quint32(data_.pools.size());
    for (auto it = data_.pools.begin(); it != data_.pools.end(); ++it) {
        out << it.key() << it.value().depth;
    }

    // --- default targets ---
    out << data_.defaultTargets;

    // --- included files list ---
    out << data_.includedFilesList;

    // --- file states (только листья) ---
    out << quint32(data_.fileStates.size());
    for (auto it = data_.fileStates.begin(); it != data_.fileStates.end(); ++it) {
        out << it.key() << it.value().mtime << it.value().size << it.value().hash;
    }

    // --- DepsLog ---
    const auto& entries = data_.depsLog.entries();
    out << quint32(entries.size());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const deps::DepsEntry& entry = it.value();
        out << entry.output;
        out << quint32(entry.inputs.size());
        for (const QString& in : entry.inputs) {
            out << in;
        }
        out << entry.mtime;
    }
}

bool Cache::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QDataStream in(&file);
    quint32 magic;
    in >> magic;
    if (magic != 0x504D4B45) return false;
    deserialize(in);
    loaded_ = true;
    return true;
}

void Cache::deserialize(QDataStream& in) {
    quint32 version;
    in >> version;
    if (version != 5) {
        LOG_WARN(QString("Unsupported cache version %1, ignoring").arg(version));
        return;
    }
    data_.version = version;

    quint32 count;

    // global vars
    in >> count;
    data_.globalVars.clear();
    for (quint32 i = 0; i < count; ++i) {
        QString key, val;
        in >> key >> val;
        data_.globalVars[key] = val;
    }

    // rules
    in >> count;
    data_.rules.clear();
    for (quint32 i = 0; i < count; ++i) {
        Rule rule;
        in >> rule.name >> rule.command >> rule.depfile >> rule.deps
           >> rule.generator >> rule.restat >> rule.rspfile >> rule.rspfile_content
           >> rule.pool >> rule.description;
        data_.rules.append(rule);
    }

    // builds
    in >> count;
    data_.builds.clear();
    data_.buildLocalVars.clear();
    for (quint32 i = 0; i < count; ++i) {
        Build build;
        in >> build.outputs >> build.inputs >> build.implicit >> build.order_only
           >> build.rule_name;
        quint32 localCount;
        in >> localCount;
        QHash<QString, QString> locals;
        for (quint32 j = 0; j < localCount; ++j) {
            QString key, val;
            in >> key >> val;
            locals[key] = val;
        }
        data_.builds.append(build);
        data_.buildLocalVars.append(locals);
    }

    // pools
    in >> count;
    data_.pools.clear();
    for (quint32 i = 0; i < count; ++i) {
        QString name; int depth;
        in >> name >> depth;
        Pool pool; pool.name = name; pool.depth = depth;
        data_.pools[name] = pool;
    }

    // default targets
    in >> data_.defaultTargets;

    // included files list
    in >> data_.includedFilesList;

    // file states
    in >> count;
    data_.fileStates.clear();
    for (quint32 i = 0; i < count; ++i) {
        QString path;
        qint64 mtime, size;
        QString hash;
        in >> path >> mtime >> size >> hash;
        FileState fs; fs.mtime = mtime; fs.size = size; fs.hash = hash;
        data_.fileStates[path] = fs;
    }

    // DepsLog
    quint32 depsCount;
    in >> depsCount;
    deps::DepsLog& depsLog = deps::DepsLog::instance();
    depsLog.clear();
    auto& entries = depsLog.entries();
    for (quint32 i = 0; i < depsCount; ++i) {
        deps::DepsEntry entry;
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
        entries[entry.output] = entry;
    }
}

bool Cache::isValid(const QString& /*mainFile*/) const {
    // Кэш всегда считается валидным, если он загружен (не проверяем mainFile)
    return loaded_;
}

std::unique_ptr<State> Cache::restore() const {
    auto state = std::make_unique<State>();

    for (auto it = data_.globalVars.begin(); it != data_.globalVars.end(); ++it) {
        state->setGlobal(it.key(), it.value());
    }

    for (const Rule& rule : data_.rules) {
        state->addRule(rule);
    }

    for (int i = 0; i < data_.builds.size(); ++i) {
        Build build = data_.builds[i];
        auto scope = QSharedPointer<Scope>::create();
        const QHash<QString, QString>& locals = data_.buildLocalVars[i];
        for (auto it = locals.begin(); it != locals.end(); ++it) {
            scope->set(it.key(), it.value());
        }
        build.scope = scope;
        state->addBuild(build);
    }

    for (const auto& pool : data_.pools) {
        state->addPool(pool);
    }

    state->defaultTargets() = data_.defaultTargets;
    state->includedFiles() = data_.includedFilesList;

    for (auto it = data_.fileStates.begin(); it != data_.fileStates.end(); ++it) {
        state->setFileState(it.key(), it.value());
    }

    // DepsLog уже восстановлен в deserialize
    return state;
}

} // namespace pmake::core