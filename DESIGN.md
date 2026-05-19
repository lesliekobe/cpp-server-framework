# C++ Industrial Server Framework - 架构设计文档

> 本文档描述 cpp-server-framework 的整体架构设计，包括模块划分、数据流、关键设计决策与部署模型。

---

## 1. 整体架构

### 1.1 八层架构总览

框架采用分层架构，从底层到顶层依次为：

```
┌─────────────────────────────────────────────────────────────┐
│                     应用层 (example/)                        │
│         gateway.cpp / server/main.cpp                       │
├─────────────────────────────────────────────────────────────┤
│                  业务调度层 (business/)                     │
│              SessionMgr / Router                            │
├─────────────────────────────────────────────────────────────┤
│                    数据层 (db/)                             │
│            ConnectionPool (MySQL/SQLite)                    │
├─────────────────────────────────────────────────────────────┤
│                   网络层 (network/)                          │
│       TcpServer / TcpClient / TcpConnection                │
│                   Codec / Protocol                          │
├─────────────────────────────────────────────────────────────┤
│                  并发调度层 (core/)                          │
│            ThreadPool / Task / Monitor                      │
├─────────────────────────────────────────────────────────────┤
│              消息队列层 (base/message_queue/)               │
│        MessageQueue / SubscribeHub (MPMC + Topic)           │
├─────────────────────────────────────────────────────────────┤
│                 事件驱动层 (base/event/, timer/)            │
│               EventLoop / Timer (最小堆)                    │
├─────────────────────────────────────────────────────────────┤
│                    基础工具层 (base/)                        │
│   Logger / Config / Timestamp / Lock / Memory / ObjectPool │
├─────────────────────────────────────────────────────────────┤
│                   系统适配层 (adapter/)                     │
│          platform.h (epoll vs IOCP 抽象)                   │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 每层职责

| 层级 | 模块 | 核心职责 |
|------|------|----------|
| **系统适配层** | `adapter/` | 封装 Linux/Windows 平台差异：Socket API、线程、文件、时间、原子操作、信号。提供统一接口，消除 `#ifdef PLATFORM_LINUX` 散落在业务代码中。 |
| **基础工具层** | `base/logger/` | 六级异步日志，格式化输出，GZip 归档，控制台染色 |
| | `base/config/` | JSON/INI 解析，热加载监视，分层配置合并 |
| | `base/time/` | Unix 时间戳，毫秒/微秒精度，格式化输出，sleep |
| | `base/lock/` | Mutex RAII 封装（LockGuard），ConditionVariable |
| **内存池 & 对象池** | `base/memory/` | 固定大小内存块池，预分配，无锁链表批量分配 |
| | `base/object_pool/` | 模板泛型对象池，构造/析构回调，无锁 CAS 并发 |
| **事件驱动** | `base/event/` | epoll/IOCP 封装，单线程 EventLoop，fd 事件，定时器，异步任务 |
| | `base/timer/` | 最小堆定时器，一次性/周期任务，支持取消 |
| **消息队列** | `base/message_queue/` | MPMC 无锁队列，Topic 订阅/广播，std::variant 多类型消息体 |
| **并发调度** | `core/threadpool/` | 固定线程数，阻塞有界队列，std::future 返回值，优雅停止 |
| | `core/task/` | 优先级任务（LOW/NORMAL/HIGH），cancel 支持 |
| | `core/monitor/` | 连接数/队列长度/内存 RSS 统计，HTTP 状态页，定时日志打点 |
| **网络层** | `network/tcp/` | TcpServer（epoll 事件驱动），TcpClient（自动重连），TcpConnection（读写缓冲，拆包粘包） |
| | `network/codec/` | 二进制协议（Length+Version+CRC32+Seq+Cmd+Data），CRC32 校验，字节序转换 |
| | `network/protocol/` | Packet 结构，encode/decode 基础包格式 |
| **业务调度** | `business/session/` | 连接上下文存储，超时清理，广播/单发，踢人 |
| | `business/router/` | 命令字 → Handler 路由映射 |
| **数据层** | `db/` | MySQL + SQLite 双支持，连接池管理，异步 SQL 执行（通过 ThreadPool） |

### 1.3 模块依赖关系

```
adapter/ (平台抽象)
  └─ base/ 所有模块依赖它

adapter
  ├── base/lock (Mutex, ConditionVariable)
  ├── base/time (时间函数)
  ├── base/memory (原子操作模板)
  ├── base/event (EventLoop epoll/IOCP)
  └── base/process (信号处理)

base/
  ├── logger ← config (写日志路径配置)
  ├── event ← timer (EventLoop 内嵌定时器驱动)
  ├── message_queue ← logger (可以打日志)
  └── object_pool ← memory (内存块分配)

core/
  ├── threadpool ← base/lock, base/event
  ├── task ← (无外部依赖)
  └── monitor ← logger, threadpool

network/
  ├── tcp/tcp_connection ← adapter (socket), protocol, codec
  ├── tcp/tcp_server ← adapter, tcp_connection, session_mgr
  ├── tcp/tcp_client ← adapter, codec
  ├── codec ← crc.h (CRC32), protocol
  └── protocol ← (Packet struct)

business/
  ├── session ← adapter (socket fd close), monitor
  └── router ← protocol (Packet)

db/
  └── connection_pool ← threadpool (异步 SQL 通过线程池执行)
```

### 1.4 数据流

完整的客户端请求 → 响应数据流：

```
[客户端]
    │ TCP 连接
    ▼
[TcpServer.accept_loop()]
    │ accept() 新连接
    ▼
[TcpConnection.start() → read_loop() / write_loop()]
    │ epoll 读事件触发
    ▼
[read() → protocol::decode_from() 拆包]
    │ 粘包处理（按 length 字段分包）
    ▼
[Codec.decode() → CRC32 校验]
    │ 校验失败丢弃，校验成功继续
    ▼
[Packet(cmd, data) 生成]
    ▼
[SessionMgr.get(session_id) 查找上下文]
    ▼
[Router.route(cmd, session_id, packet)]
    │ 路由到业务 Handler
    ▼
[业务 Handler 处理 (在线程池执行)]
    │ 访问 DB / 业务逻辑
    ▼
[Response Packet 编码]
    ▼
[TcpConnection.send_packet(cmd, data)]
    │ 写入 write_queue_
    ▼
[write_loop() 通过 socket 发送]
    │
    ▼
[客户端收到响应]
```

---

## 2. 系统适配层 (adapter/)

### 2.1 Linux epoll vs Windows IOCP 抽象

适配层将两个平台的差异统一封装在 `platform.h` 中，定义以下平台检测宏：

```cpp
#ifdef _WIN32
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_LINUX 0
#else
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 1
#endif
```

### 2.2 统一接口

| 功能 | Linux | Windows |
|------|-------|---------|
| Socket 类型 | `int` | `SOCKET` |
| 无效 socket | `-1` | `(SOCKET)(~0)` |
| 错误码 | `errno` | `WSAGetLastError()` |
| 非阻塞设置 | `fcntl(O_NONBLOCK)` | `ioctlsocket(FIONBIO)` |
| 事件监听 | `epoll_create/epoll_ctl/epoll_wait` | `CreateIoCompletionPort/GetQueuedCompletionStatus` |
| 定时器 | `timerfd_create/timerfd_settime` | `CreateWaitableTimer/SetWaitableTimer` |
| 信号 | `signal()` / `pthread_sigmask()` | `SetConsoleCtrlHandler()` |
| 线程 ID | `pthread_self()` | `GetCurrentThreadId()` |

### 2.3 关键 API

- `get_current_thread_id()` — 获取当前线程 ID
- `get_tick_ms()` — monotonic clock 毫秒数（不受系统时间调整影响）
- `sleep_ms(int ms)` — 线程 sleep
- `close_socket(SocketHandle sock)` — 跨平台关闭 socket
- `set_nonblocking(SocketHandle sock)` — 设置非阻塞模式
- `bind_and_listen(SocketHandle sock, int port)` — 绑定并监听端口
- `accept_connection(SocketHandle server_sock)` — 接受连接
- `platform_init()` / `platform_shutdown()` — Windows WSA 初始化/清理

---

## 3. 基础工具层 (base/)

### 3.1 Logger: 六级异步日志

**设计目标**：日志输出不阻塞业务线程。

**架构**：
- 业务线程调用 `LOG_INFO()` 等宏，立即将 `LogEntry` 入队到内存队列（`queue_`）
- 独立日志线程 `log_thread_` 从队列消费，写入文件/控制台
- 异步队列满时（`queue_max_=8192`）丢弃最旧的 TRACE/DEBUG 日志，统计 `dropped_`

**日志格式**（默认）：
```
2025-05-19 10:30:45.123 [INFO] [thread:1234] [server] Server started on port 8080
```

**滚动策略**：
- 按天滚动（检查 `today_yday_`）
- 文件超过 `max_file_size_mb`（默认 200MB）时创建新文件
- 旧文件 GZip 压缩归档（`compress_old_logs()`）
- 保留 `max_history_days` 天（默认 7 天）

**日志级别**：`TRACE(0) → DEBUG(1) → INFO(2) → WARN(3) → ERROR(4) → FATAL(5) → NONE(6)`

**示例**：
```cpp
framework::Logger::instance()->init("./logs", "gateway",
    framework::LogLevel::INFO,
    framework::LogLevel::DEBUG);
LOG_INFO("Gateway listening on port %d", 8080);
```

### 3.2 Config: JSON/INI 解析 + 热加载

**支持格式**：
- JSON（类 nlohmann/json 风格）
- INI（段+键值对）
- 命令行参数（getopt 风格）

**热加载**：监视配置文件变化，文件内容 MD5 变化时触发回调。支持多个文件同时监视。

**分层配置**：通过 `Config::merge()` 将默认配置与用户覆盖配置合并。

**路径访问**：支持嵌套路径如 `"server.network.tcp.port"`，路径不存在返回 `nullptr`。

### 3.3 Timestamp: 时间工具

- `now_seconds()` / `now_milliseconds()` / `now_microseconds()` — Unix 时间戳
- `format_now(fmt)` — 格式化当前时间（默认 `"%Y-%m-%d %H:%M:%S"`）
- `sleep_ms(int64_t ms)` — 线程阻塞sleep

### 3.4 Lock: 互斥锁 RAII

```cpp
framework::LockGuard<framework::Mutex> guard(mtx_);
// 构造时加锁，析构时自动解锁
```

---

## 4. 内存池 & 对象池 (base/memory/, base/object_pool/)

### 4.1 MemoryPool: 固定大小块

**原理**：预先分配 `item_size` 大小的内存块，用链表管理空闲块。分配时从链表头部取，释放时归还到链表头部。无 `new/delete` 开销。

**数据结构**：
```
free_list_ → [Block1] → [Block2] → [Block3] → nullptr
             data[]     data[]     data[]
```

**并发**：分配/释放各自加锁（`std::mutex`），无 CAS 无锁优化（简单稳健）。

**扩容**：当 free_list 为空时，一次性分配 `block_size`（默认 32）个新块。

**典型使用场景**：网络包缓冲区、固定大小对象（如协议头、Session 上下文）。

```cpp
MemoryPool pool(512, 64); // 每块 512 字节，预分配 64 块
void* buf = pool.allocate();   // O(1)
pool.deallocate(buf);
```

### 4.2 ObjectPool<T>: 模板泛型对象池

**原理**：预分配 `Slot[]` 数组，每个 Slot 可存放一个 `T` 对象。使用时 `allocate()` 执行 placement new 构造对象，`deallocate()` 调用析构函数后归还 Slot。

**无锁链表**：`free_list_` 链表管理空闲 Slot，allocate 时从链表 pop，deallocate 时 push。无 CAS 原子操作（依赖 mutex）。

**构造/析构回调**：
```cpp
ObjectPool<Session> pool(100,
    [](Session* s){ s->reset(); },  // on_create
    [](Session* s){ s->cleanup(); } // on_destroy
);
```

**典型使用场景**：需要频繁创建/销毁的业务对象（如 Session、Request）。

**与 MemoryPool 的区别**：
- `MemoryPool`：固定字节大小，底层内存管理
- `ObjectPool<T>`：泛型模板，面向对象，构造/析构自动化

---

## 5. 事件驱动 & 定时器 (base/event/, base/timer/)

### 5.1 EventLoop: 单例 per 线程

每个线程拥有独立的 `EventLoop` 实例（不能跨线程使用）。

**Linux 实现**（`epoll_fd`）：
```cpp
epoll_create() → epoll_ctl(ADD/MOD/DEL) → epoll_wait()
```

**事件类型**：
```cpp
enum class EventType : int {
    READ   = 1 << 0,   // fd 可读
    WRITE  = 1 << 1,   // fd 可写
    ERROR  = 1 << 2,   // fd 出错
    CLOSED = 1 << 3,   // fd 关闭
    TIMER  = 1 << 4,   // 定时器触发
    SIGNAL = 1 << 5,   // Unix 信号
};
```

**异步任务**：其他线程可调用 `push_task()` 投递任务到 EventLoop 线程执行（线程安全）。

**事件处理**：
```cpp
// 注册
loop->add_fd(sockfd, EventType::READ, &handler);
// 或定时器
loop->add_timer(5000, []{ ... }, 5000); // 5秒后首次触发，周期5秒
// 驱动
loop->run(); // 阻塞
```

### 5.2 Timer: 最小堆实现

**数据结构**：二叉堆（完全二叉树，最小堆），按 `expire_at` 排序。

**操作复杂度**：
- `add`：O(log n)
- `cancel`：O(log n)（标记 `cancelled=true`，poll 时跳过）
- `poll(now_ms)`：O(1) 弹出所有到期定时器，O(log n) 调整堆

**一次性 vs 周期**：
- `interval_ms == 0`：一次性，到期后自动移除
- `interval_ms > 0`：周期，到期后重新计算 `expire_at` 再插入堆

**与 EventLoop 集成**：定时器回调执行在 EventLoop 所在线程（无锁安全）。

---

## 6. 消息队列 (base/message_queue/)

### 6.1 MessageQueue: MPMC 无锁队列

**多生产者多消费者**：每个线程可 `send()` 发送消息，也可 `recv()` 接收消息。

**实现**：使用 `std::mutex + std::condition_variable` 的标准实现，简单高效。

**超时接收**：
```cpp
// 阻塞等待，无消息则一直等
queue.recv<MyMsg>(msg);
// 等待最多 100ms
queue.recv<MyMsg>(msg, 100);
// 非阻塞尝试
queue.try_recv<MyMsg>(msg);
```

### 6.2 Topic 订阅/广播机制

`SubscribeHub` 支持基于类型的 topic 订阅：

```cpp
auto id = global_hub()->subscribe<LoginMsg>([](const LoginMsg& msg) {
    LOG_INFO("login: %s", msg.username.c_str());
});

// 发布
global_hub()->publish<LoginMsg>({"alice", "pass123"});

// 取消订阅
global_hub()->unsubscribe<LoginMsg>(id);
```

**内部实现**：`std::unordered_map<std::type_index, vector<callback>>`，通过 `std::any` + `std::any_cast` 实现类型安全。

### 6.3 std::variant 支持多类型消息体

配合 `std::variant` 实现多类型联合消息：

```cpp
using GameMsg = std::variant<LoginMsg, LogoutMsg, ChatMsg>;

MessageQueue q;
q.send(GameMsg{LoginMsg{"alice"}});
q.send(GameMsg{ChatMsg{"hello"}});

// 接收时判断类型
GameMsg msg;
if (q.recv<GameMsg>(msg)) {
    if (auto* login = std::get_if<LoginMsg>(&msg)) { ... }
    else if (auto* chat = std::get_if<ChatMsg>(&msg)) { ... }
}
```

---

## 7. 并发调度层 (core/)

### 7.1 ThreadPool: 固定线程数

**架构**：
```
┌─────────────────────────────────────────┐
│  主线程 append(task)                    │
│    └─ 任务入队 tasks_                   │
│         └─ task_cv_.notify_one()       │
│                                           │
│  [Worker0] [Worker1] [Worker2] [Worker3] │
│    └─ while(!stop_) {                  │
│         pop task → execute()            │
│       }                                 │
└─────────────────────────────────────────┘
```

**队列**：`std::queue<TaskPtr>`，有界队列（`queue_size=0` 表示无界）。

**返回值**：`std::future<RetType>`，业务可获取任务执行结果：
```cpp
auto fut = pool.append([](){ return compute(); });
auto result = fut.get(); // 阻塞等待
```

**优雅停止**：
- `shutdown()`：等待队列中所有任务执行完毕，再停止 worker 线程
- `shutdown_now()`：立即停止，正在执行的任务不受影响（依靠任务自身取消）

**优先级任务**：`append_priority(TaskPtr)` 将任务插入优先级队列（按 priority 比较，高优先级先执行）。

### 7.2 Task: 优先级任务

```cpp
auto task = std::make_shared<Task>(
    [](){ LOG_INFO("done"); },
    TaskPriority::HIGH
);
pool.append_priority(task);
// 可取消
task->cancel();
```

### 7.3 Monitor: 运行时统计

**监控指标**：
- 连接数（alive / total / peak）
- 任务队列长度
- 线程池活跃/总线程数
- 消息收发计数、字节数
- 内存 RSS（千字节）
- 日志总条数

**HTTP 状态页**：可选启动内置 HTTP 服务器（`:8080/monitor`），返回 JSON 格式监控数据。

**定时打点**：后台线程每 `interval_seconds` 秒将统计快照写入日志。

---

## 8. 网络层 (network/)

### 8.1 TcpServer: epoll/Select 事件驱动

**架构**：
```
┌──────────────────────────────────────────────────┐
│  Acceptor Thread                                 │
│    └─ accept() → 新 socket → 新 TcpConnection    │
│                          └─ conn.start()         │
│                                └─ read_loop()    │
│                                    write_loop() │
│                                                  │
│  心跳线程（定期扫描 idle 连接）                   │
└──────────────────────────────────────────────────┘
```

**连接管理**：`std::unordered_map<int64_t, TcpConnectionPtr>`，`session_id` 自增唯一。

**心跳检测**：后台线程定时扫描 `last_recv_time_`，超过 `idle_timeout_ms` 断开连接。

**广播**：
```cpp
tcp_server.broadcast(CMD_MSG, "hello all");
tcp_server.send_to(sid, CMD_MSG, "hello one");
```

### 8.2 TcpClient: 自动重连

**架构**：
- `read_thread_`：独立线程循环读取 socket 数据
- `write_thread_`：独立线程从 `write_queue_` 取数据发送
- `reconnect_thread_`：检测到断开后，自动重连

**自动重连**：指数退避（待实现），最大重连间隔 30s。

### 8.3 TcpConnection: 读写缓冲区，拆包粘包

**读缓冲区** `read_buf_`：累计字节，直到攒够一个完整包才处理。

**拆包逻辑**（基于长度域）：
1. `peek_packet_length()` 先读取 4 字节 length
2. 若 `buf.size() >= length + 4`，则说明一个完整包已到达
3. 截取 `length` 字节数据，交给 codec.decode 处理
4. 剩余数据留在 buffer 继续等待下一包

**写队列** `write_queue_`：`send_packet()` 将编码后的数据入队，`write_loop()` 异步发送。队列满时 `send_packet()` 阻塞（使用 `write_cv_` 条件变量等待）。

### 8.4 Packet Protocol: Length+Version+CRC32+Seq+Cmd+Data

**二进制格式**：
```
Offset  Size  Field
0       4     Length（整个包长度，不含 length 自身，大端序）
4       1     Version（协议版本 = 1）
5       4     CRC32（从 Version 到 Data 末尾的校验）
9       4     Seq（序列号，防重放）
13      2     Cmd（命令字，大端序）
15      N     Data（N = Length - 15）
```

**协议头结构**（`PacketHeader`）：
```cpp
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t length;  // 4B
    uint8_t  version; // 1B
    uint32_t crc32;   // 4B
    uint32_t seq;     // 4B
    uint16_t cmd;     // 2B
};                    // Total = 15 bytes
#pragma pack(pop)
```

### 8.5 Codec: CRC32 校验，大小端，ByteOrder 转换

**CRC32**：使用标准 CRC-32 (IEEE) 多项式，校验范围从 Version 到 Data 末尾。

**字节序**：
- `ByteOrder::htonll()` / `ntohll()` — 64位转换
- `ByteOrder::htonl()` / `ntohl()` — 32位转换
- `ByteOrder::htons()` / `ntohs()` — 16位转换
- 自动检测小端机，必要时字节交换

**解码状态机**：
```
decode(buf, len)
  ├─ state=READ_HEADER: 读取 15 字节 header
  │     ├─ buffer 不足 15B → 返回空，继续累积
  │     └─ buffer ≥ 15B → 解析 header
  │           ├─ version 不匹配 → 报错
  │           ├─ CRC32 校验失败 → 丢弃包
  │           └─ CRC32 校验成功 → 进入 READ_BODY
  └─ state=READ_BODY: 根据 length 读取 Data
        ├─ buffer 不足 length 字节 → 返回空
        └─ buffer ≥ length → 组装 Packet，状态重置 READ_HEADER
```

---

## 9. 数据层 (db/)

### 9.1 ConnectionPool: MySQL + SQLite 双支持

**MySQL**：`libmysqlclient` 连接池，预分配 `pool_size` 个连接。

**SQLite**：同步接口，通过 `thread_pool_` 异步执行，避免阻塞网络主线程。

**初始化**：
```cpp
ConnectionPool db;
db.init_mysql("localhost", 3306, "user", "pass", "mydb", 4);
// 或
db.init_sqlite("./data.db", 4);
db.set_thread_pool(thread_pool); // 关联线程池用于异步 SQL
```

### 9.2 异步 SQL 执行

```cpp
db.execute_sql_async("INSERT INTO logs VALUES(...)",
    [](const DBResult& r) {
        if (r.success) LOG_DEBUG("insert ok");
        else LOG_ERROR("insert failed: %s", r.error_msg.c_str());
    });
```

**执行流程**：
1. SQL 任务封装为 `QueryTask`
2. `QueryTask::run_async()` 将任务丢进 ThreadPool
3. ThreadPool worker 执行 `QueryTask::run()` → 调用 `ConnectionPool::execute_sql()`
4. 执行完成后回调 `DBCallback`

**同步执行**：`DBResult r = db.execute_sql("SELECT * FROM users WHERE id=1");`（会阻塞调用线程，建议仅用于测试/管理场景）

---

## 10. 业务调度层 (business/)

### 10.1 SessionMgr: 连接上下文管理

**存储结构**：`std::unordered_map<int64_t, ConnectionContext>`，线程安全（`std::mutex` 保护）。

**ConnectionContext 字段**：
| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | int64_t | 全局唯一会话 ID |
| `socket_fd` | int | socket 文件描述符 |
| `remote_ip` | string | 客户端 IP |
| `remote_port` | int | 客户端端口 |
| `connect_time` | uint64_t | 连接建立时间（ms） |
| `last_msg_time` | uint64_t | 最后消息时间（ms） |
| `heartbeat_count` | uint32_t | 心跳计数 |
| `is_alive` | bool | 是否存活 |
| `user_data` | void* | 业务扩展指针 |

**超时清理**：
```cpp
// 每 30 秒调用一次
size_t removed = mgr->cleanup_timeout(get_tick_ms(), 30000);
LOG_INFO("清理 %zu 个超时连接", removed);
```

**广播**：
```cpp
mgr->broadcast(data, [](int64_t sid, const uint8_t* d, size_t len) {
    // 通过 TcpServer 发送
});
```

### 10.2 Router: 命令字路由

**注册**：
```cpp
Router router;
router.reg(CMD_LOGIN, [](int64_t sid, const std::string& data) {
    LOG_INFO("login from session %ld", sid);
    // 处理登录逻辑
});
router.reg(CMD_DATA, [](int64_t sid, const std::string& data) {
    // 处理业务数据
});
```

**路由**：
```cpp
// 当 TcpConnection 收到一个完整包时：
router.route(session_id, packet);
```

**未注册命令字**：静默忽略（不崩溃），可记录日志。

---

## 11. 信号处理 & 优雅退出 (base/process/)

### 11.1 信号捕获链

捕获的信号：`SIGINT`（Ctrl+C）、`SIGTERM`（kill）、`SIGHUP`（配置重载）。

### 11.2 三阶段关闭

```
┌──────────────────────────────────────────────────────────┐
│ Phase 1: 停止接收新连接                                 │
│   - TcpServer 不再 accept()                             │
│   - 已有连接继续处理                                     │
│                                                          │
│ Phase 2: 等待队列清空                                   │
│   - ThreadPool shutdown()，等待所有任务完成             │
│   - 数据库操作完成                                       │
│                                                          │
│ Phase 3: 关闭所有连接                                   │
│   - 所有 TcpConnection close()                          │
│   - 写内存日志到磁盘                                     │
│   - 调用用户保存数据回调                                 │
└──────────────────────────────────────────────────────────┘
```

**退出回调注册**：
```cpp
SignalHandler sig;
sig.on_shutdown([](){ save_data(); });
sig.on_shutdown([](){ LOG_INFO("cleanup done"); });
sig.start();
```

---

## 12. 部署模型

### 12.1 多线程模型

**推荐配置**（8 核机器）：
```
1 个 Acceptor Thread（接受新连接）
+ 4 个 EventLoop Threads（每个线程一个 EventLoop，处理已连接 socket 的 IO）
+ 4 个 ThreadPool Workers（业务逻辑/数据库操作）
━━━━━━━━━━━━━━━━━━━━━━━━━━
共 8 个线程（与 CPU 核心数对齐）
```

**SessionMgr / Router 跨线程共享**：通过锁保护，由于线程数固定且不多，开销可控。

**设计原则**：
- 网络 IO 线程（EventLoop）不处理业务逻辑，只做收发
- 业务逻辑（数据库操作、计算）全部丢 ThreadPool
- 避免跨线程锁竞争热点

### 12.2 部署架构图

```
                ┌─────────────┐
                │   Clients   │
                └──────┬──────┘
                       │ TCP
                ┌──────▼──────┐
                │ TcpServer   │  Port 8080
                │ (epoll)     │
                └──────┬──────┘
                       │
          ┌────────────┼────────────┐
          │            │            │
    ┌─────▼─────┐ ┌────▼────┐ ┌─────▼─────┐
    │ Conn 1    │ │ Conn 2  │ │ Conn N    │
    │ (session)│ │ (session)│ │ (session) │
    └─────┬─────┘ └────┬────┘ └─────┬─────┘
          │            │            │
          └────────────┼────────────┘
                       │ dispatch
          ┌────────────▼────────────┐
          │      Router             │
          │  (cmd → Handler)        │
          └────────────┬────────────┘
                       │ async
          ┌────────────▼────────────┐
          │    ThreadPool (4)      │
          │  - DB queries          │
          │  - Business logic      │
          └────────────┬────────────┘
                       │
          ┌────────────▼────────────┐
          │   ConnectionPool        │
          │   MySQL / SQLite        │
          └─────────────────────────┘
```

### 12.3 高可用要点

1. **内存池化**：减少 `new/delete`，降低延迟抖动
2. **异步 IO + 线程池**：不阻塞网络线程
3. **心跳检测**：及时发现死连接
4. **信号优雅退出**：不停发数据，不丢日志
5. **Monitor 监控**：提前发现瓶颈

---

*文档版本: 1.0 | 更新日期: 2026-05-19*