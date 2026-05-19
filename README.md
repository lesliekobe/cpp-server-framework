# C++ Industrial Server Framework

A production-ready C++ server framework for industrial control, gateway services, and backend systems.

## Features

- **跨平台**: Linux (epoll) / Windows (IOCP) 支持
- **高性能**: 基于 epoll/IOCP 的事件驱动架构，无锁 CAS 对象池
- **模块化设计**: 每个模块独立，可单独使用
- **线程安全**: 完整的线程池 + 无锁数据结构
- **开箱即用**: 可直接作为网关 / 工业控制服务器 / 后端服务框架

## 模块列表

### 基础模块 (base/)

| 模块 | 文件 | 说明 |
|------|------|------|
| **Logger** | `base/logger/logger.h` | 六级日志 (TRACE/DEBUG/INFO/WARN/ERROR/FATAL)、异步落盘、按天切割、GZip压缩、颜色输出 |
| **Time** | `base/time/timestamp.h` | 时间戳、高精度计时器 |
| **Lock** | `base/lock/` | 互斥锁、条件变量、读写锁 |
| **Memory** | `base/memory/memory_pool.h` | 固定大小内存池，多规格按需增长 |
| **Event Loop** | `base/event/event_loop.h` | epoll/Kqueue/IOCP 封装，异步IO，TimerFd/SocketFd 事件监听 |
| **Timer** | `base/timer/timer.h` | 高精度定时器（内存时间轮），一次性/周期任务，支持取消 |
| **Message Queue** | `base/message_queue/message_queue.h` | 线程间消息队列，MPMC 模式，topic 订阅/广播 |
| **Config** | `base/config/config.h` | JSON/INI 解析，命令行参数，热加载配置 |
| **Object Pool** | `base/object_pool/object_pool.h` | 模板对象池，构造/析构回调，线程安全 |
| **Signal Handler** | `base/process/signal_handler.h` | 信号捕获，优雅退出，shutdown 回调链 |

### 核心模块 (core/)

| 模块 | 文件 | 说明 |
|------|------|------|
| **ThreadPool** | `core/threadpool/thread_pool.h` | 线程池，支持固定/动态线程数，任务队列 |
| **Task** | `core/task/task.h` | 任务封装，支持 priority/cancel |
| **Monitor** | `core/monitor/monitor.h` | 连接数、队列长度、线程负载、内存 RSS 统计，HTTP 监控页 |

### 网络模块 (network/)

| 模块 | 文件 | 说明 |
|------|------|------|
| **TCP Server** | `network/tcp/tcp_server.h` | 多线程 TCP 服务器 |
| **TCP Client** | `network/tcp/tcp_client.h` | TCP 客户端 |
| **TCP Connection** | `network/tcp/tcp_connection.h` | 连接封装 |
| **Protocol** | `network/protocol/packet.h` | 数据包封装 |
| **Codec** | `network/codec/codec.h` | 二进制协议 (Length+Version+CRC32+Seq+Cmd+Data)，CRC校验，防重放，字节序转换，Protobuf 扩展点 |

### 业务模块 (business/)

| 模块 | 文件 | 说明 |
|------|------|------|
| **Session Manager** | `business/session/session_mgr.h` | 会话上下文管理、超时清理、踢人、广播、单发 |
| **Router** | `business/router/router.h` | 消息路由 |

### 数据库模块 (db/)

| 模块 | 文件 | 说明 |
|------|------|------|
| **Connection Pool** | `db/connection_pool.h` | MySQL/SQLite 连接池，异步 SQL 执行 |
| **Query Task** | `db/query_task.h` | SQL 任务丢线程池异步执行 |

### 适配层 (adapter/)

| 模块 | 文件 | 说明 |
|------|------|------|
| **Platform** | `adapter/platform.h` | 跨平台封装：线程、Socket、文件操作、时间、原子操作 |

## 目录结构

```
cpp-server-framework/
├── adapter/           # 跨平台适配层
│   ├── platform.h/cpp
├── base/              # 基础模块
│   ├── config/
│   ├── event/
│   ├── logger/
│   ├── lock/
│   ├── memory/
│   ├── message_queue/
│   ├── object_pool/
│   ├── process/
│   ├── time/
│   └── timer/
├── business/          # 业务层
│   ├── router/
│   └── session/
├── core/              # 核心组件
│   ├── monitor/
│   ├── task/
│   └── threadpool/
├── db/                 # 数据库
│   ├── connection_pool.h/cpp
│   └── query_task.h
├── network/            # 网络层
│   ├── codec/
│   ├── protocol/
│   └── tcp/
├── example/            # 示例
│   ├── gateway/
│   └── server/
├── test/               # 单元测试
└── CMakeLists.txt
```

## 快速开始

### 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### 运行示例

```bash
# TCP 服务器示例
./tcp_server_example

# 网关示例
./gateway_example
```

## 使用示例

### Logger (六极日志 + 异步)

```cpp
#include "logger/logger.h"

// 初始化（异步写盘，按天切割，GZip压缩）
framework::Logger::instance()->init("./logs", "server",
    framework::LogLevel::INFO,  // Console 级别
    framework::LogLevel::DEBUG  // File 级别
);

// 使用
LOG_INFO("Server started on port %d", 8080);
LOG_ERROR("Connection failed: %s", strerror(errno));
```

### EventLoop (事件驱动)

```cpp
#include "base/event/event_loop.h"

auto loop = framework::create_event_loop();
// 注册 socket 事件
loop->add_fd(sockfd, framework::EventType::READ, &handler);

// 添加定时器
loop->add_timer(5000, [](){ LOG_INFO("5s timer fired"); }, 5000); // 周期

loop->run();
```

### Session Manager (会话管理)

```cpp
#include "session/session_mgr.h"
using namespace framework;

SessionMgr* mgr = global_session_mgr();
mgr->init();

// 新连接
int64_t sid = mgr->add(socket_fd, "192.168.1.100", 12345);

// 广播消息
mgr->broadcast(data, [](int64_t sid, const uint8_t* d, size_t len) {
    send_to_sid(sid, d, len);
});

// 踢人
mgr->kick(sid);

// 清理超时连接（每30秒调用）
mgr->cleanup_timeout(get_tick_ms(), 30000);
```

### Config (配置解析)

```cpp
#include "config/config.h"

Config cfg;
cfg.load_json("config.json");
cfg.load_ini("server.ini");

// 按路径获取
int port = cfg.get("server.port")->as_int();
std::string host = cfg.get("server.host")->as_string();

// 命令行参数
CmdLineParser parser;
parser.add("port", 'p', true, "server port", "8080");
parser.add("config", 'c', true, "config file");
parser.parse(argc, argv);

int p = std::stoi(parser.get("port"));
```

### Codec (协议编解码)

```cpp
#include "codec/codec.h"

Codec codec;
// 注册命令处理器
codec.register_handler(0x1001, [](uint32_t seq, const uint8_t* d, size_t len) {
    LOG_DEBUG("cmd=0x1001 seq=%u len=%zu", seq, len);
});

// 编码
auto pkt = codec.encode(0x1001, payload.data(), payload.size(), seq++);

// 解码（流式）
auto result = codec.decode(buf, buf_len);
if (result) {
    handle(result->cmd, result->seq, result->data);
}
```

### Connection Pool (数据库连接池)

```cpp
#include "db/connection_pool.h"

ConnectionPool db;
db.init_sqlite("./data.db", 4);
db.set_thread_pool(thread_pool);

db.execute_sql_async("INSERT INTO logs VALUES(...)", [](const DBResult& r) {
    if (r.success) LOG_DEBUG("insert ok");
    else LOG_ERROR("insert failed: %s", r.error_msg.c_str());
});
```

## 测试

```bash
cd build
ctest --output-on-failure
# 或
./threadpool_test
./logger_test
./tcp_test
```

## License

MIT