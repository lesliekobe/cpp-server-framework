/*
 * query_task.h - SQL 任务（丢线程池执行）
 */
#pragma once
#include "db/connection_pool.h"

namespace framework {

class QueryTask {
public:
    QueryTask(class ConnectionPool* pool, std::string sql, DBCallback cb);

    // 同步执行
    void run();

    // 异步（丢进连接池的线程池）
    void run_async();

private:
    class ConnectionPool* pool_{nullptr};
    std::string           sql_;
    DBCallback            cb_;
};

} // namespace framework