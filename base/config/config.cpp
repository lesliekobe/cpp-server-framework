/*
 * config.cpp
 */
#include "base/config/config.h"
#include "logger/logger.h"
#include "adapter/platform.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <regex>

namespace framework {

// ============ ConfigNode ============
bool ConfigNode::is_array() const {
    if (!value_.has_value()) return false;
    return std::holds_alternative<std::vector<int>>(*value_) ||
           std::holds_alternative<std::vector<int64_t>>(*value_) ||
           std::holds_alternative<std::vector<double>>(*value_) ||
           std::holds_alternative<std::vector<std::string>>(*value_);
}

bool ConfigNode::as_bool() const {
    if (std::holds_alternative<bool>(*value_)) return std::get<bool>(*value_);
    if (std::holds_alternative<std::string>(*value_)) {
        const std::string& s = std::get<std::string>(*value_);
        return s == "true" || s == "1" || s == "yes" || s == "on";
    }
    return false;
}
int ConfigNode::as_int() const {
    if (std::holds_alternative<int>(*value_)) return std::get<int>(*value_);
    if (std::holds_alternative<int64_t>(*value_)) return (int)std::get<int64_t>(*value_);
    if (std::holds_alternative<double>(*value_)) return (int)std::get<double>(*value_);
    if (std::holds_alternative<std::string>(*value_)) {
        return std::stoi(std::get<std::string>(*value_));
    }
    return 0;
}
int64_t ConfigNode::as_int64() const {
    if (std::holds_alternative<int64_t>(*value_)) return std::get<int64_t>(*value_);
    if (std::holds_alternative<int>(*value_)) return (int64_t)std::get<int>(*value_);
    if (std::holds_alternative<double>(*value_)) return (int64_t)std::get<double>(*value_);
    if (std::holds_alternative<std::string>(*value_)) {
        return std::stoll(std::get<std::string>(*value_));
    }
    return 0;
}
double ConfigNode::as_double() const {
    if (std::holds_alternative<double>(*value_)) return std::get<double>(*value_);
    if (std::holds_alternative<int>(*value_)) return (double)std::get<int>(*value_);
    if (std::holds_alternative<int64_t>(*value_)) return (double)std::get<int64_t>(*value_);
    if (std::holds_alternative<std::string>(*value_)) {
        return std::stod(std::get<std::string>(*value_));
    }
    return 0.0;
}
std::string ConfigNode::as_string() const {
    if (std::holds_alternative<std::string>(*value_)) return std::get<std::string>(*value_);
    return "";
}
std::vector<ConfigValue> ConfigNode::as_array() const {
    if (std::holds_alternative<std::vector<int>>(*value_)) {
        std::vector<ConfigValue> r;
        for (auto v : std::get<std::vector<int>>(*value_)) r.push_back(v);
        return r;
    }
    if (std::holds_alternative<std::vector<int64_t>>(*value_)) {
        std::vector<ConfigValue> r;
        for (auto v : std::get<std::vector<int64_t>>(*value_)) r.push_back(v);
        return r;
    }
    if (std::holds_alternative<std::vector<double>>(*value_)) {
        std::vector<ConfigValue> r;
        for (auto v : std::get<std::vector<double>>(*value_)) r.push_back(v);
        return r;
    }
    if (std::holds_alternative<std::vector<std::string>>(*value_)) {
        std::vector<ConfigValue> r;
        for (auto v : std::get<std::vector<std::string>>(*value_)) r.push_back(v);
        return r;
    }
    return {};
}

bool ConfigNode::has_key(const std::string& key) const {
    return children_.find(key) != children_.end();
}
const ConfigNode* ConfigNode::get_key(const std::string& key) const {
    auto it = children_.find(key);
    return it != children_.end() ? &it->second : nullptr;
}
ConfigNode* ConfigNode::get_key(const std::string& key) {
    return const_cast<ConfigNode*>(const_cast<const ConfigNode*>(this)->get_key(key));
}
void ConfigNode::set_key(const std::string& key, ConfigValue v) {
    children_[key] = ConfigNode(v);
}

// ============ Config ============
Config::Config() = default;

bool Config::load_json(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        LOG_ERROR("Config: cannot open %s", path.c_str());
        return false;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string content = oss.str();
    last_content_ = std::vector<char>(content.begin(), content.end());
    file_path_ = path;
    ifs.close();

    std::istringstream iss(content);
    root_ = ConfigNode();
    return parse_json_stream(iss);
}

// ============ JSON 解析器（简化版） ============
namespace json_parser {
    static void skip_ws(std::istream& is) {
        char c;
        while (is.get(c)) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
            is.putback(c);
            break;
        }
    }
    static std::string parse_string(std::istream& is) {
        char c; is.get(c); // eat opening "
        std::string s;
        while (is.get(c)) {
            if (c == '\\') {
                char n; is.get(n);
                if (n == 'n') s += '\n';
                else if (n == 't') s += '\t';
                else if (n == 'r') s += '\r';
                else s += n;
            } else if (c == '"') {
                break;
            } else {
                s += c;
            }
        }
        return s;
    }
    static ConfigValue parse_value(std::istream& is) {
        skip_ws(is);
        char c; is.get(c);
        if (c == '"') {
            is.putback(c);
            std::string s = parse_string(is);
            return s;
        }
        if (c == '[') {
            std::vector<std::string> arr;
            while (true) {
                skip_ws(is);
                is.get(c);
                if (c == ']') break;
                is.putback(c);
                std::string s = parse_string(is);
                arr.push_back(s);
                skip_ws(is);
                is.get(c);
                if (c == ']') { is.putback(c); break; }
                if (c != ',') { is.putback(c); break; }
            }
            // 尝试判断类型
            bool all_int = true, all_double = true;
            for (auto& s : arr) {
                try { std::stoi(s); } catch (...) { all_int = false; }
                try { std::stod(s); } catch (...) { all_double = false; }
            }
            if (all_int) {
                std::vector<int64_t> r;
                for (auto& s : arr) r.push_back(std::stoll(s));
                return r;
            }
            if (all_double) {
                std::vector<double> r;
                for (auto& s : arr) r.push_back(std::stod(s));
                return r;
            }
            return arr;
        }
        if (c == '{') {
            is.putback(c);
            return (std::nullptr_t)nullptr; // 不支持嵌套对象
        }
        // number or bool/null
        std::string token;
        token += c;
        while (is.get(c)) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ']' || c == '}') {
                is.putback(c);
                break;
            }
            token += c;
        }
        if (token == "true")  return true;
        if (token == "false") return false;
        if (token == "null")  return (std::nullptr_t)nullptr;
        if (token.find('.') != std::string::npos) {
            return std::stod(token);
        }
        return std::stoll(token);
    }
}

bool Config::parse_json_stream(std::istream& is) {
    std::vector<ConfigNode*> stack;
    stack.push_back(&root_);

    char c;
    std::string last_key;

    while (is.get(c)) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '{') {
            if (!stack.empty() && !last_key.empty()) {
                auto* parent = stack.back();
                auto* node = &(*parent).get_key(last_key);
                if (node) { stack.push_back(const_cast<ConfigNode*>(node)); }
            }
            last_key.clear();
            continue;
        }
        if (c == '}') {
            if (!stack.empty() && stack.size() > 1) stack.pop_back();
            last_key.clear();
            continue;
        }
        if (c == '"') {
            is.putback(c);
            std::string key = json_parser::parse_string(is);
            json_parser::skip_ws(is);
            is.get(c); // :
            if (c == ':') {
                last_key = key;
                json_parser::skip_ws(is);
                is.get(c);
                is.putback(c);
                ConfigValue v = json_parser::parse_value(is);
                if (!stack.empty()) {
                    stack.back()->set_key(key, v);
                }
            }
        }
    }
    return true;
}

bool Config::load_ini(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    file_path_ = path;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    last_content_ = std::vector<char>(oss.str().begin(), oss.str().end());
    ifs.close();
    std::istringstream iss(oss.str());
    return parse_ini_stream(iss);
}

bool Config::parse_ini_stream(std::istream& is) {
    std::string section;
    std::string line;
    while (std::getline(is, line)) {
        // 去掉 ; 注释
        auto pos = line.find('#');
        if (pos != std::string::npos) line = line.substr(0, pos);
        pos = line.find(';');
        if (pos != std::string::npos) line = line.substr(0, pos);
        // 去掉首尾空白
        while (!line.empty() && (line[0] == ' ' || line[0] == '\t')) line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos) {
                section = line.substr(1, end - 1);
                continue;
            }
        }
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
            std::string full_key = section.empty() ? key : section + "." + key;
            root_.set_key(full_key, val);
        }
    }
    return true;
}

const ConfigNode* Config::get(const std::string& path) const {
    const ConfigNode* cur = &root_;
    size_t start = 0;
    while (start < path.size()) {
        auto dot = path.find('.', start);
        std::string key = (dot == std::string::npos) ? path.substr(start) : path.substr(start, dot - start);
        cur = cur->get_key(key);
        if (!cur) return nullptr;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

void Config::watch(const std::string& path, ChangeCallback cb) {
    watch_paths_.push_back(path);
    watchers_.push_back(std::move(cb));
}

void Config::unwatch_all() {
    watchers_.clear();
    watch_paths_.clear();
}

void Config::poll_changes() {
    if (file_path_.empty()) return;
    std::ifstream ifs(file_path_, std::ios::binary);
    if (!ifs) return;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string content = oss.str();
    std::vector<char> cur(content.begin(), content.end());
    ifs.close();

    if (cur.size() == last_content_.size()) return; // 简单判断
    last_content_ = std::move(cur);

    // 重新加载配置
    root_ = ConfigNode();
    std::istringstream iss(content);
    parse_json_stream(iss);

    for (auto& cb : watchers_) cb(file_path_);
    LOG_INFO("Config: hot-reloaded %s", file_path_.c_str());
}

void Config::notify_watchers(const std::string& path) {
    for (auto& cb : watchers_) cb(path);
}

std::string Config::to_json(bool pretty) const {
    std::ostringstream oss;
    oss << "{\n";
    size_t count = 0;
    for (auto& kv : root_.keys()) {
        if (count++) oss << ",\n";
        oss << "  \"" << kv.first << "\": ";
        if (kv.second.is_string()) oss << "\"" << kv.second.as_string() << "\"";
        else if (kv.second.is_int()) oss << kv.second.as_int();
        else if (kv.second.is_double()) oss << kv.second.as_double();
        else if (kv.second.is_bool()) oss << (kv.second.as_bool() ? "true" : "false");
        else oss << "null";
    }
    oss << "\n}";
    return oss.str();
}

// ============ 命令行解析 ============
void CmdLineParser::add(const std::string& name, char short_name, bool has_val,
                         const std::string& desc, const std::string& default_val) {
    CmdLineArg arg;
    arg.name = name;
    arg.short_name = short_name;
    arg.has_value = has_val;
    arg.description = desc;
    arg.value = default_val;
    args_[name] = arg;
}

bool CmdLineParser::parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string token = argv[i];
        if (token == "--help" || token == "-h") continue;
        if (token[0] == '-') {
            std::string name;
            bool is_long = false;
            if (token[1] == '-') {
                is_long = true;
                name = token.substr(2);
            } else {
                name = token.substr(1);
            }
            // 查找
            bool found = false;
            for (auto& kv : args_) {
                if ((is_long && kv.first == name) ||
                    (!is_long && kv.second.short_name == name[0])) {
                    kv.second.found = true;
                    if (kv.second.has_value && i + 1 < argc) {
                        kv.second.value = argv[++i];
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                rest_.push_back(token);
            }
        } else {
            rest_.push_back(token);
        }
    }
    return true;
}

bool CmdLineParser::has(const std::string& name) const {
    auto it = args_.find(name);
    return it != args_.end() && it->second.found;
}

std::string CmdLineParser::get(const std::string& name) const {
    auto it = args_.find(name);
    return (it != args_.end()) ? it->second.value : "";
}

std::string CmdLineParser::get(const std::string& name, const std::string& default_val) const {
    auto it = args_.find(name);
    return (it != args_.end() && it->second.found) ? it->second.value : default_val;
}

std::string CmdLineParser::help() const {
    std::ostringstream oss;
    oss << "Usage:\n";
    for (auto& kv : args_) {
        const CmdLineArg& a = kv.second;
        oss << "  ";
        if (a.short_name) oss << "-" << a.short_name << ", ";
        else oss << "    ";
        oss << "--" << a.name;
        if (a.has_value) oss << " <value>";
        oss << "  " << a.description;
        if (!a.value.empty()) oss << " (default: " << a.value << ")";
        oss << "\n";
    }
    return oss.str();
}

} // namespace framework