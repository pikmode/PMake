#include "core/state.h"

#include <string>
#include <string_view>
#include <utility>

namespace pmake::core {

Scope::Scope(Scope* parent) : parent_(parent) {}

void Scope::set(std::string_view name, std::string_view value) {
    vars_[std::string(name)] = std::string(value);
}

std::optional<std::string> Scope::get(std::string_view name) const {
    auto it = vars_.find(std::string(name));
    if (it != vars_.end()) return it->second;
    if (parent_) return parent_->get(name);
    return std::nullopt;
}

void Scope::remove(std::string_view name) {
    vars_.erase(std::string(name));
}

bool Scope::has(std::string_view name) const {
    return get(name).has_value();
}

State::State() {}

State::~State() {
    for (auto& [k, func] : functions_) {
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
    , cacheValid_(other.cacheValid_)
    , forceRebuild_(other.forceRebuild_) {
    other.functions_.clear();
}

State& State::operator=(State&& other) noexcept {
    if (this == &other) return *this;
    for (auto& [k, func] : functions_) delete func.body;
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
    forceRebuild_   = other.forceRebuild_;

    other.functions_.clear();
    return *this;
}

void State::setGlobal(std::string_view name, std::string_view value) {
    global_scope_.set(name, value);
}
std::optional<std::string> State::getGlobal(std::string_view name) const {
    return global_scope_.get(name);
}
const std::unordered_map<std::string, std::string>& State::globals() const {
    return global_scope_.locals();
}

void State::addRule(const Rule& rule) { rules_[rule.name] = rule; }
const Rule* State::getRule(std::string_view name) const {
    auto it = rules_.find(std::string(name));
    return (it != rules_.end()) ? &it->second : nullptr;
}
const std::unordered_map<std::string, Rule>& State::rules() const { return rules_; }

void State::addBuild(const Build& build) { builds_.push_back(build); }
const std::vector<Build>& State::builds() const { return builds_; }

void State::addPool(const Pool& pool) { pools_[pool.name] = pool; }
const Pool* State::getPool(std::string_view name) const {
    auto it = pools_.find(std::string(name));
    return (it != pools_.end()) ? &it->second : nullptr;
}
const std::unordered_map<std::string, Pool>& State::pools() const { return pools_; }

void State::setFileState(std::string_view path, const FileState& state) {
    fileStates_[std::string(path)] = state;
}
std::optional<FileState> State::getFileState(std::string_view path) const {
    auto it = fileStates_.find(std::string(path));
    if (it != fileStates_.end()) return it->second;
    return std::nullopt;
}
const std::unordered_map<std::string, FileState>& State::fileStates() const { return fileStates_; }

std::vector<std::string>& State::defaultTargets() { return defaultTargets_; }
const std::vector<std::string>& State::defaultTargets() const { return defaultTargets_; }
std::vector<std::string>& State::includedFiles() { return includedFiles_; }
const std::vector<std::string>& State::includedFiles() const { return includedFiles_; }

void State::addConfig(const ConfigBlock& block) { configs_[block.name] = block; }
bool State::hasConfig(std::string_view name) const {
    return configs_.find(std::string(name)) != configs_.end();
}
const ConfigBlock* State::getConfig(std::string_view name) const {
    auto it = configs_.find(std::string(name));
    return (it != configs_.end()) ? &it->second : nullptr;
}
const std::unordered_map<std::string, ConfigBlock>& State::configs() const { return configs_; }

void State::addPlatform(const PlatformBlock& block) { platforms_[block.name] = block; }
bool State::hasPlatform(std::string_view name) const {
    return platforms_.find(std::string(name)) != platforms_.end();
}
const PlatformBlock* State::getPlatform(std::string_view name) const {
    auto it = platforms_.find(std::string(name));
    return (it != platforms_.end()) ? &it->second : nullptr;
}
const std::unordered_map<std::string, PlatformBlock>& State::platforms() const { return platforms_; }

void State::addFunction(const FunctionDef& func) {
    auto it = functions_.find(func.name);
    if (it != functions_.end() && it->second.body != func.body) {
        delete it->second.body;
    }
    functions_[func.name] = func;
}
const FunctionDef* State::getFunction(std::string_view name) const {
    auto it = functions_.find(std::string(name));
    return (it != functions_.end()) ? &it->second : nullptr;
}
const std::unordered_map<std::string, FunctionDef>& State::functions() const { return functions_; }

void State::setCommandHash(std::string_view output, std::string_view hash) const {
    std::lock_guard<std::mutex> lk(commandHashesMutex_);
    commandHashes_[std::string(output)] = std::string(hash);
}
std::optional<std::string> State::getCommandHash(std::string_view output) const {
    std::lock_guard<std::mutex> lk(commandHashesMutex_);
    auto it = commandHashes_.find(std::string(output));
    if (it != commandHashes_.end()) return it->second;
    return std::nullopt;
}
void State::setCommandHashes(const std::unordered_map<std::string, std::string>& hashes) {
    std::lock_guard<std::mutex> lk(commandHashesMutex_);
    commandHashes_ = hashes;
}
const std::unordered_map<std::string, std::string>& State::commandHashes() const {
    return commandHashes_;
}

} // namespace pmake::core