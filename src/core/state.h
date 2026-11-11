#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/ast.h"
#include "core/value.h"

namespace pmake::core {

class Scope;

struct Rule {
    std::string name;
    std::string command;
    std::string description;
    bool generator = false;
    bool restat = false;
    std::string depfile;
    std::string deps;
    std::string rspfile;
    std::string rspfile_content;
    bool pool = false;
};

struct Build {
    std::vector<std::string> outputs;
    std::vector<std::string> inputs;
    std::vector<std::string> implicit;
    std::vector<std::string> order_only;
    std::string rule_name;
    std::shared_ptr<Scope> scope;
};

struct Pool {
    std::string name;
    int depth = 1;
};

struct FileState {
    std::int64_t mtime = -1;
    std::int64_t size = 0;
    std::string hash;
};

struct ConfigBlock {
    std::string name;
    std::unordered_map<std::string, std::string> vars;
};

struct PlatformBlock {
    std::string name;
    std::unordered_map<std::string, std::string> vars;
};

struct FunctionDef {
    std::string name;
    std::vector<std::string> params;
    AstNode* body = nullptr;
};

class Scope {
public:
    explicit Scope(Scope* parent = nullptr);
    void set(std::string_view name, std::string_view value);
    std::optional<std::string> get(std::string_view name) const;
    void remove(std::string_view name);
    bool has(std::string_view name) const;
    const std::unordered_map<std::string, std::string>& locals() const { return vars_; }

private:
    Scope* parent_;
    std::unordered_map<std::string, std::string> vars_;
};

class State {
public:
    State();
    ~State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&& other) noexcept;
    State& operator=(State&& other) noexcept;

    void setGlobal(std::string_view name, std::string_view value);
    std::optional<std::string> getGlobal(std::string_view name) const;
    const std::unordered_map<std::string, std::string>& globals() const;

    void addRule(const Rule& rule);
    const Rule* getRule(std::string_view name) const;
    const std::unordered_map<std::string, Rule>& rules() const;

    void addBuild(const Build& build);
    const std::vector<Build>& builds() const;

    void addPool(const Pool& pool);
    const Pool* getPool(std::string_view name) const;
    const std::unordered_map<std::string, Pool>& pools() const;

    void setFileState(std::string_view path, const FileState& state);
    std::optional<FileState> getFileState(std::string_view path) const;
    const std::unordered_map<std::string, FileState>& fileStates() const;

    std::vector<std::string>& defaultTargets();
    const std::vector<std::string>& defaultTargets() const;

    std::vector<std::string>& includedFiles();
    const std::vector<std::string>& includedFiles() const;

    void addConfig(const ConfigBlock& block);
    bool hasConfig(std::string_view name) const;
    const ConfigBlock* getConfig(std::string_view name) const;
    const std::unordered_map<std::string, ConfigBlock>& configs() const;

    void addPlatform(const PlatformBlock& block);
    bool hasPlatform(std::string_view name) const;
    const PlatformBlock* getPlatform(std::string_view name) const;
    const std::unordered_map<std::string, PlatformBlock>& platforms() const;

    void addFunction(const FunctionDef& func);
    const FunctionDef* getFunction(std::string_view name) const;
    const std::unordered_map<std::string, FunctionDef>& functions() const;

    void setCommandHash(std::string_view output, std::string_view hash) const;
    std::optional<std::string> getCommandHash(std::string_view output) const;
    void setCommandHashes(const std::unordered_map<std::string, std::string>& hashes);
    const std::unordered_map<std::string, std::string>& commandHashes() const;

    void setCacheValid(bool valid) { cacheValid_ = valid; }
    bool isCacheValid() const { return cacheValid_; }

    // NEW: Флаг для принудительной пересборки при смене конфига/платформы
    void setForceRebuild(bool force) { forceRebuild_ = force; }
    bool forceRebuild() const { return forceRebuild_; }

    Value callUserFunction(std::string_view name,
                           const std::vector<Value>& args,
                           Scope* callerScope = nullptr);
    Value executeAstNode(AstNode* node, Scope* localScope);

    std::string expandVars(std::string_view input, const Scope* local_scope = nullptr) const;
    std::string expandVarsWithSpecial(std::string_view input,
                                      const Scope* local_scope,
                                      const std::unordered_map<std::string, std::string>& specialVars) const;

private:
    Scope global_scope_;
    std::unordered_map<std::string, Rule> rules_;
    std::vector<Build> builds_;
    std::unordered_map<std::string, Pool> pools_;
    std::vector<std::string> defaultTargets_;
    std::vector<std::string> includedFiles_;
    std::unordered_map<std::string, FileState> fileStates_;
    std::unordered_map<std::string, ConfigBlock> configs_;
    std::unordered_map<std::string, PlatformBlock> platforms_;
    std::unordered_map<std::string, FunctionDef> functions_;
    mutable std::unordered_map<std::string, std::string> commandHashes_;
    mutable std::mutex commandHashesMutex_;
    bool cacheValid_ = false;
    bool forceRebuild_ = false; // NEW
};

} // namespace pmake::core