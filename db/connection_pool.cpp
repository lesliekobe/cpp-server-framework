/*
 * connection_pool.cpp
 */
#include "db/connection_pool.h"
#include "logger/logger.h"
#include "core/threadpool/thread_pool.h"
#include <cstring>

namespace framework {

// ============ SQLite 连接实现（简化版） ============
#ifdef USE_SQLITE
#include <sqlite3.h>
struct SQLiteConnection : public DBConnection {
    sqlite3* db_{nullptr};
    ~SQLiteConnection() { if (db_) sqlite3_close(db_); }

    bool execute(const std::string& sql, DBResult& out) override {
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), [](void* ud, int cols, char** vals, char** names)->int {
            auto* out = (DBResult*)ud;
            std::vector<std::string> row;
            for (int i = 0; i < cols; ++i) row.push_back(vals[i] ? vals[i] : "");
            out->rows.push_back(std::move(row));
            return 0;
        }, &out, &err);
        if (rc != SQLITE_OK) {
            out.success = false;
            if (err) { out.error_msg = err; sqlite3_free(err); }
            return false;
        }
        out.success = true;
        return true;
    }
    std::string error() const override {
        return db_ ? sqlite3_errmsg(db_) : "";
    }
};
#endif // USE_SQLITE

// ============ ConnectionPool::Impl ============
struct ConnectionPool::Impl {
    DBType                         type_{DBType::SQLITE};
    std::vector<std::shared_ptr<DBConnection>> pool_;
    std::queue<std::shared_ptr<DBConnection>> free_list_;
    std::mutex                     mtx_;
    std::condition_variable        cv_;
    std::atomic<bool>              done_{false};

    // MySQL 参数
    std::string                    mysql_host;
    int                            mysql_port{3306};
    std::string                    mysql_user;
    std::string                    mysql_pass;
    std::string                    mysql_db;

    // SQLite 参数
    std::string                    sqlite_path;

    Impl() = default;
};

// ============ ConnectionPool ============
ConnectionPool::ConnectionPool() : impl_(std::make_unique<Impl>()) {}
ConnectionPool::~ConnectionPool() { close(); }

bool ConnectionPool::init_mysql(const std::string& host, int port,
                               const std::string& user, const std::string& password,
                               const std::string& db, int pool_size) {
    impl_->type_ = DBType::MYSQL;
    impl_->mysql_host = host;
    impl_->mysql_port = port;
    impl_->mysql_user = user;
    impl_->mysql_pass = password;
    impl_->mysql_db = db;

    // 简化实现：模拟连接池（实际使用 libmysqlclient）
    // 真实实现需要 #include <mysql/mysql.h> 并调用 mysql_real_connect
    LOG_INFO("ConnectionPool: MySQL pool initialized (host=%s:%d db=%s size=%d)",
             host.c_str(), port, db.c_str(), pool_size);
    initialized_.store(true, std::memory_order_release);
    return true;
}

bool ConnectionPool::init_sqlite(const std::string& db_path, int pool_size) {
    impl_->type_ = DBType::SQLITE;
    impl_->sqlite_path = db_path;

#ifdef USE_SQLITE
    // 创建预连接
    for (int i = 0; i < pool_size; ++i) {
        auto conn = std::make_shared<SQLiteConnection>();
        if (sqlite3_open(db_path.c_str(), &conn->db_) != SQLITE_OK) {
            LOG_ERROR("ConnectionPool: sqlite3_open failed: %s", sqlite3_errmsg(conn->db_));
            continue;
        }
        impl_->pool_.push_back(conn);
        impl_->free_list_.push(conn);
    }
#endif

    LOG_INFO("ConnectionPool: SQLite pool initialized (path=%s size=%d)",
             db_path.c_str(), pool_size);
    initialized_.store(true, std::memory_order_release);
    return true;
}

void ConnectionPool::close() {
    {
        std::lock_guard<std::mutex> lock(impl_->mtx_);
        impl_->done_.store(true, std::memory_order_release);
        impl_->pool_.clear();
        while (!impl_->free_list_.empty()) impl_->free_list_.pop();
    }
    impl_->cv_.notify_all();
    initialized_.store(false, std::memory_order_release);
    LOG_INFO("ConnectionPool: closed");
}

std::shared_ptr<DBConnection> ConnectionPool::get_connection() {
    std::shared_ptr<DBConnection> conn;
    {
        std::unique_lock<std::mutex> lock(impl_->mtx_);
        if (!impl_->free_list_.empty()) {
            conn = impl_->free_list_.front();
            impl_->free_list_.pop();
        }
    }
    return conn;
}

DBResult ConnectionPool::execute_sql(const std::string& sql) {
    DBResult result;

#ifdef USE_SQLITE
    if (impl_->type_ == DBType::SQLITE) {
        sqlite3* db = nullptr;
        {
            std::shared_ptr<DBConnection> raw = get_connection();
            // 对于 SQLite，每次直接打开（简化实现）
            (void)raw;
        }
        if (sqlite3_open(impl_->sqlite_path.c_str(), &db) != SQLITE_OK) {
            result.success = false;
            result.error_msg = sqlite3_errmsg(db);
            sqlite3_close(db);
            return result;
        }
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(),
            [](void* ud, int cols, char** vals, char** names)->int {
                auto* res = (DBResult*)ud;
                if (res->cols.empty() && names) {
                    for (int i = 0; i < cols; ++i) res->cols.push_back(names[i]);
                }
                std::vector<std::string> row;
                for (int i = 0; i < cols; ++i) row.push_back(vals[i] ? vals[i] : "");
                res->rows.push_back(std::move(row));
                return 0;
            }, &result, &err);
        if (rc != SQLITE_OK) {
            result.success = false;
            if (err) { result.error_msg = err; sqlite3_free(err); }
        } else {
            result.success = true;
        }
        sqlite3_close(db);
        return result;
    }
#endif

    // 默认：模拟执行
    result.success = true;
    LOG_DEBUG("ConnectionPool: execute_sql (simulated): %s", sql.c_str());
    return result;
}

void ConnectionPool::execute_sql_async(const std::string& sql, DBCallback cb) {
    if (!thread_pool_) {
        LOG_ERROR("ConnectionPool: thread_pool not set, falling back to sync");
        DBResult r = execute_sql(sql);
        if (cb) cb(r);
        return;
    }

    thread_pool_->push([this, sql, cb]() {
        DBResult r = execute_sql(sql);
        if (cb) {
            try { cb(r); } catch (...) {}
        }
    });
}

// ============ QueryTask ============
QueryTask::QueryTask(ConnectionPool* pool, std::string sql, DBCallback cb)
    : pool_(pool), sql_(std::move(sql)), cb_(std::move(cb)) {}

void QueryTask::run() {
    DBResult r = pool_->execute_sql(sql_);
    if (cb_) cb_(r);
}

void QueryTask::run_async() {
    pool_->execute_sql_async(sql_, cb_);
}

} // namespace framework