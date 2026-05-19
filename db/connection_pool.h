/*
 * connection_pool.h - 数据库连接池
 *
 * 支持：
 *   - MySQL 连接池（libmysqlclient）
 *   - SQLite 同步接口（通过线程池异步执行）
 *   - 连接池管理（预分配、按需增长）
 *   - SQL 任务异步执行
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace framework {

// ============ 数据库类型 ============
enum class DBType {
    MYSQL,
    SQLITE
};

// ============ 查询结果 ============
struct DBResult {
    bool        success{false};
    int         affected_rows{0};
    int64_t     insert_id{0};
    std::string error_msg;
    std::vector<std::vector<std::string>> rows; // 行数据
    std::vector<std::string>             cols; // 列名
};

// ============ SQL 任务 ============
using DBCallback = std::function<void(const DBResult&)>;

struct SQLTask {
    std::string              sql;
    DBCallback               callback;
    bool                     async{true}; // false=同步等待
};

// ============ 连接句柄（不透明） ============
struct DBConnection {
    virtual ~DBConnection() = default;
    virtual bool execute(const std::string& sql, DBResult& out) = 0;
    virtual std::string error() const = 0;
};

// ============ 连接池接口 ============
class ConnectionPool {
public:
    ConnectionPool();
    ~ConnectionPool();

    // 禁止拷贝
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // 初始化 MySQL 连接池
    bool init_mysql(const std::string& host, int port,
                    const std::string& user, const std::string& password,
                    const std::string& db, int pool_size = 4);

    // 初始化 SQLite（文件路径）
    bool init_sqlite(const std::string& db_path, int pool_size = 2);

    // 关闭连接池
    void close();

    // 同步执行 SQL（调用者阻塞）
    DBResult execute_sql(const std::string& sql);

    // 异步执行 SQL（通过线程池，不阻塞）
    void execute_sql_async(const std::string& sql, DBCallback cb);

    // 原始连接获取（高级用户）
    std::shared_ptr<DBConnection> get_connection();

    // 线程池引用（用于 SQL 的异步执行）
    class ThreadPool* thread_pool() const { return thread_pool_; }
    void set_thread_pool(class ThreadPool* tp) { thread_pool_ = tp; }

    bool is_initialized() const { return initialized_.load(std::memory_order_acquire); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool>    initialized_{false};
    class ThreadPool*    thread_pool_{nullptr};
};

// ============ 查询任务（丢进线程池执行） ============
class QueryTask {
public:
    QueryTask(ConnectionPool* pool, std::string sql, DBCallback cb);

    // 执行（同步）
    void run();

    // 丢到线程池
    void run_async();

private:
    ConnectionPool* pool_;
    std::string     sql_;
    DBCallback      cb_;
};

} // namespace framework