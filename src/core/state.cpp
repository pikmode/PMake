#include "core/state.h"
#include "util/logging.h"
#include <utility>

namespace pmake::core {

// ==================================================================
// Scope
// ==================================================================
Scope::Scope(Scope* parent) : parent_(parent) {}

void Scope::set(const QString& name, const QString& value) {
    vars_[name] = value;
}

std::optional<QString> Scope::get(const QString& name) const {
    auto it = vars_.find(name);
    if (it != vars_.end()) return it.value();
    if (parent_) return parent_->get(name);
    return std::nullopt;
}

void Scope::remove(const QString& name) {
    vars_.remove(name);
}

bool Scope::has(const QString& name) const {
    return get(name).has_value();
}

// ==================================================================
// State
// ==================================================================
State::State() {}

State::~State() {
    for (auto& func : functions_) {
        delete func.body;
    }
    functions_.clear();
}

State::State(State&& other) noexcept
    : global_scope_(std::move(other.global_scope_))
    , rules_(std::move(other.rules_))
    , builds_(std::move(other.builds_))
    , pools_(std::move(other.pools_))
    , defaultTargets_(std::move(other.defaultTargets_))
    , includedFiles_(std::move(other.includedFiles_))
    , fileStates_(std::move(other.fileStates_))
    , configs_(std::move(other.configs_))
    , platforms_(std::move(other.platforms_))
    , functions_(std::move(other.functions_))
    , commandHashes_(std::move(other.commandHashes_))
    , cacheValid_(other.cacheValid_) {
    other.functions_.clear();
}

State& State::operator=(State&& other) noexcept {
    if (this == &other) return *this;

    for (auto& func : functions_) delete func.body;
    functions_.clear();

    global_scope_   = std::move(other.global_scope_);
    rules_          = std::move(other.rules_);
    builds_         = std::move(other.builds_);
    pools_          = std::move(other.pools_);
    defaultTargets_ = std::move(other.defaultTargets_);
    includedFiles_  = std::move(other.includedFiles_);
    fileStates_     = std::move(other.fileStates_);
    configs_        = std::move(other.configs_);
    platforms_      = std::move(other.platforms_);
    functions_      = std::move(other.functions_);
    commandHashes_  = std::move(other.commandHashes_);
    cacheValid_     = other.cacheValid_;

    other.functions_.clear();
    return *this;
}

void State::setGlobal(const QString& name, const QString& value) {
    global_scope_.set(name, value);
}

std::optional<QString> State::getGlobal(const QString& name) const {
    return global_scope_.get(name);
}

const QHash<QString, QString>& State::globals() const {
    return global_scope_.locals();
}

void State::addRule(const Rule& rule) {
    rules_[rule.name] = rule;
}

const Rule* State::getRule(const QString& name) const {
    auto it = rules_.find(name);
    return (it != rules_.end()) ? &it.value() : nullptr;
}

const QHash<QString, Rule>& State::rules() const {
    return rules_;
}

void State::addBuild(const Build& build) {
    builds_.append(build);
}

const QVector<Build>& State::builds() const {
    return builds_;
}

void State::addPool(const Pool& pool) {
    pools_[pool.name] = pool;
}

const Pool* State::getPool(const QString& name) const {
    auto it = pools_.find(name);
    return (it != pools_.end()) ? &it.value() : nullptr;
}

const QHash<QString, Pool>& State::pools() const {
    return pools_;
}

void State::setFileState(const QString& path, const FileState& state) {
    fileStates_[path] = state;
}

std::optional<FileState> State::getFileState(const QString& path) const {
    auto it = fileStates_.find(path);
    if (it != fileStates_.end()) return it.value();
    return std::nullopt;
}

const QHash<QString, FileState>& State::fileStates() const {
    return fileStates_;
}

QVector<QString>& State::defaultTargets() {
    return defaultTargets_;
}

const QVector<QString>& State::defaultTargets() const {
    return defaultTargets_;
}

QVector<QString>& State::includedFiles() {
    return includedFiles_;
}

const QVector<QString>& State::includedFiles() const {
    return includedFiles_;
}

void State::addConfig(const ConfigBlock& block) {
    configs_[block.name] = block;
}

bool State::hasConfig(const QString& name) const {
    return configs_.contains(name);
}

const ConfigBlock* State::getConfig(const QString& name) const {
    auto it = configs_.find(name);
    return (it != configs_.end()) ? &it.value() : nullptr;
}

const QHash<QString, ConfigBlock>& State::configs() const {
    return configs_;
}

void State::addPlatform(const PlatformBlock& block) {
    platforms_[block.name] = block;
}

bool State::hasPlatform(const QString& name) const {
    return platforms_.contains(name);
}

const PlatformBlock* State::getPlatform(const QString& name) const {
    auto it = platforms_.find(name);
    return (it != platforms_.end()) ? &it.value() : nullptr;
}

const QHash<QString, PlatformBlock>& State::platforms() const {
    return platforms_;
}

void State::addFunction(const FunctionDef& func) {
    auto it = functions_.find(func.name);
    if (it != functions_.end() && it->body != func.body) {
        delete it->body;
    }
    functions_[func.name] = func;
}

const FunctionDef* State::getFunction(const QString& name) const {
    auto it = functions_.find(name);
    return (it != functions_.end()) ? &it.value() : nullptr;
}

const QHash<QString, FunctionDef>& State::functions() const {
    return functions_;
}

void State::setCommandHash(const QString& output, const QString& hash) const {
    QMutexLocker locker(&commandHashesMutex_);
    commandHashes_[output] = hash;
}

std::optional<QString> State::getCommandHash(const QString& output) const {
    QMutexLocker locker(&commandHashesMutex_);
    auto it = commandHashes_.find(output);
    if (it != commandHashes_.end()) return it.value();
    return std::nullopt;
}

void State::setCommandHashes(const QHash<QString, QString>& hashes) {
    QMutexLocker locker(&commandHashesMutex_);
    commandHashes_ = hashes;
}

const QHash<QString, QString>& State::commandHashes() const {
    return commandHashes_;
}

} // namespace pmake::core