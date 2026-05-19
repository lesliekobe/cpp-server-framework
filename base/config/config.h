/*
 * config.h - 配置解析模块
 *
 * 支持：
 *   - JSON 配置文件读写（简化版 nlohmann/json 风格）
 *   - INI 文件解析
 *   - 命令行参数解析（getopt 风格）
 *   - 热加载配置（文件监视）
 *   - 分层配置（默认 + 用户覆盖）
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <variant>

namespace framework {

// ============ 配置值类型 ============
using ConfigValue = std::variant<
    std::nullptr_t, bool, int, int64_t, double, std::string,
    std::vector<int>, std::vector<int64_t>, std::vector<double>,
    std::vector<std::string>
>;

// ============ 配置节点 ============
class ConfigNode {
public:
    ConfigNode() = default;
    explicit ConfigNode(const ConfigValue& v) : value_(v) {}

    // 类型查询
    bool is_null()    const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool()    const { return std::holds_alternative<bool>(value_); }
    bool is_int()     const { return std::holds_alternative<int>(value_); }
    bool is_int64()   const { return std::holds_alternative<int64_t>(value_); }
    bool is_double()  const { return std::holds_alternative<double>(value_); }
    bool is_string()  const { return std::holds_alternative<std::string>(value_); }
    bool is_array()   const;

    // 值访问
    bool               as_bool()    const;
    int                as_int()     const;
    int64_t            as_int64()   const;
    double             as_double()  const;
    std::string         as_string() const;
    std::vector<ConfigValue> as_array() const;

    // 子节点
    bool               has_key(const std::string& key) const;
    const ConfigNode*  get_key(const std::string& key) const;
    ConfigNode*        get_key(const std::string& key);
    void               set_key(const std::string& key, ConfigValue v);

    // 迭代
    size_t             num_keys() const { return children_.size(); }
    const std::unordered_map<std::string, ConfigNode>& keys() const { return children_; }

    std::string path_; // for debug
private:
    std::optional<ConfigValue> value_;
    std::unordered_map<std::string, ConfigNode> children_;
};

// ============ 配置管理 ============
class Config {
public:
    Config();

    // 加载 JSON 文件
    bool load_json(const std::string& path);

    // 加载 INI 文件
    bool load_ini(const std::string& path);

    // 合并另一个 Config（后者覆盖前者）
    void merge(const Config& other);

    // 获取节点（支持路径如 "server.host"）
    const ConfigNode* get(const std::string& path) const;

    // 获取值（带默认值）
    template<typename T>
    T get_value(const std::string& path, T default_val) const;

    // 热加载监视
    using ChangeCallback = std::function<void(const std::string& path)>;
    void watch(const std::string& path, ChangeCallback cb);
    void unwatch_all();

    // 轮询检查文件变化（需在主循环调用）
    void poll_changes();

    // 清空
    void clear() { root_ = ConfigNode(); watchers_.clear(); }

    // 访问根节点
    ConfigNode&       root()       { return root_; }
    const ConfigNode& root() const { return root_; }

    // 文件路径
    const std::string& file_path() const { return file_path_; }

    // 序列化回 JSON
    std::string        to_json(bool pretty = true) const;

private:
    bool parse_json_stream(std::istream& is);
    bool parse_ini_stream(std::istream& is);
    void notify_watchers(const std::string& path);

    ConfigNode                              root_;
    std::string                             file_path_;
    std::vector<char>                       last_content_;
    std::vector<ChangeCallback>             watchers_;
    std::vector<std::string>                watch_paths_;
};

// ============ 命令行参数 ============
struct CmdLineArg {
    std::string       name;
    char              short_name{0};
    bool              has_value{false};
    bool              found{false};
    std::string       value;
    std::string       description;
};

class CmdLineParser {
public:
    CmdLineParser() = default;

    // 添加选项
    void add(const std::string& name, char short_name, bool has_val,
             const std::string& desc, const std::string& default_val = "");

    // 解析（argc, argv）
    bool parse(int argc, char** argv);

    // 查询
    bool     has(const std::string& name) const;
    std::string get(const std::string& name) const;
    std::string get(const std::string& name, const std::string& default_val) const;

    // 未知参数
    std::vector<std::string> rest() const { return rest_; }

    // 帮助信息
    std::string help() const;

private:
    std::unordered_map<std::string, CmdLineArg> args_;
    std::vector<std::string>                   rest_;
};

} // namespace framework