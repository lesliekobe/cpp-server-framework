# C++ Industrial Server Framework - 测试文档

> 本文档描述 cpp-server-framework 各模块的单元测试、集成测试用例、运行步骤及结果记录。

---

## Part 1: 单元测试

### 1. Logger 测试

#### case 1.1: 初始化日志文件，调用各级别日志，检查输出

**测试目的**：验证 Logger 初始化成功，各级别日志正确输出到控制台和文件。

**运行命令**：
```bash
cd build
./logger_test --case=init_and_levels
```

**预期输出**：
```
[TRACE] test trace message
[DEBUG] test debug message
[INFO]  test info message
[WARN]  test warn message
[ERROR] test error message
[FATAL] test fatal message
```

**通过标准**：各级别日志均出现，无崩溃，文件 `logs/server_YYYYMMDD.log` 存在且包含上述内容。

---

#### case 1.2: 日志滚动（构造大日志内容超过 max_size）

**测试目的**：验证单文件超过 `max_file_size_mb` 时创建新文件。

**运行命令**：
```bash
cd build
./logger_test --case=log_rotation
```

**测试步骤**：
1. 配置 `max_file_size_mb = 1`（1MB）
2. 循环写入单条 100KB 日志，触发滚动
3. 检查 `server_YYYYMMDD_001.log`, `server_YYYYMMDD_002.log` 等多文件存在

**预期输出**：
```
Rotating log file (size exceeded 1 MB)
New log file: logs/server_20260519_001.log
```

**通过标准**：产生至少 2 个分卷文件，文件内容正常可读。

---

#### case 1.3: 日志异步落盘不阻塞主线程（大量日志）

**测试目的**：验证日志异步写入不阻塞业务线程。

**运行命令**：
```bash
cd build
./logger_test --case=async_nonblocking
```

**测试步骤**：
1. 主线程在 1 秒内写入 10000 条日志
2. 测量写入耗时（应远小于 1 秒）
3. 等待 2 秒后检查文件落盘完整性

**预期输出**：
```
Wrote 10000 logs in 50ms (async)
File contains 10000 entries
```

**通过标准**：主线程写入不阻塞（异步队列吸收），最终文件完整记录所有日志。

---

### 2. MemoryPool / ObjectPool 测试

#### case 2.1: MemoryPool allocate/deallocate 多次，检查指针有效

**测试目的**：验证 MemoryPool 分配/归还多次，指针有效且无内存错误。

**运行命令**：
```bash
cd build
./memory_pool_test --case=basic_alloc
```

**测试步骤**：
```cpp
MemoryPool pool(64, 10);
std::vector<void*> ptrs;
for (int i = 0; i < 1000; ++i) {
    void* p = pool.allocate();
    memset(p, 0xAA, 64); // 写入验证
    ptrs.push_back(p);
}
for (auto p : ptrs) {
    pool.deallocate(p);
}
```

**预期输出**：
```
Allocated 1000 blocks
All blocks valid (wrote/read pattern)
Deallocated 1000 blocks
free_blocks=1000, used_blocks=0
```

**通过标准**：无崩溃，内存模式验证通过，池统计正确。

---

#### case 2.2: ObjectPool<int> 多次 acquire/release

**测试目的**：验证泛型对象池的正确构造/析构。

**运行命令**：
```bash
cd build
./object_pool_test --case=basic_pool
```

**测试步骤**：
```cpp
int create_count = 0, destroy_count = 0;
ObjectPool<MyClass> pool(10,
    [&](MyClass* p){ create_count++; p->init(); },
    [&](MyClass* p){ destroy_count++; }
);

auto* obj1 = pool.allocate();
auto* obj2 = pool.allocate();
pool.deallocate(obj1);
pool.deallocate(obj2);

EXPECT_EQ(create_count, 2);
EXPECT_EQ(destroy_count, 0); // 归还到池，未真正销毁
EXPECT_EQ(pool.pool_size(), 2);
```

**预期输出**：
```
allocate: create_count=1, pool_size=0
allocate: create_count=2, pool_size=1
deallocate: destroy_count=0 (still in pool)
pool_size=2, allocated=0
```

**通过标准**：回调调用次数正确，池 size 正确，无内存泄漏。

---

#### case 2.3: 并发 acquire/release（多线程竞争同一个池）

**测试目的**：验证 ObjectPool 的线程安全性。

**运行命令**：
```bash
cd build
./object_pool_test --case=concurrent
```

**测试步骤**：
```cpp
ObjectPool<MyClass> pool;
std::vector<std::thread> threads;
std::atomic<int> success{0}, fail{0};

for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&](){
        for (int i = 0; i < 1000; ++i) {
            auto* p = pool.allocate();
            if (p) { success++; pool.deallocate(p); }
            else { fail++; }
        }
    });
}
for (auto& t : threads) t.join();

EXPECT_EQ(fail, 0);
EXPECT_EQ(success, 4000);
```

**预期输出**：
```
4 threads, 1000 iterations each
success=4000, fail=0
pool_size=... allocated=0
```

**通过标准**：无失败，total created = 4000，全部正确归还。

---

### 3. ThreadPool 测试

#### case 3.1: 提交 100 个任务，全部完成，检查 counter

**测试目的**：验证 ThreadPool 基本任务提交与执行。

**运行命令**：
```bash
cd build
./threadpool_test --case=basic_tasks
```

**测试步骤**：
```cpp
ThreadPool pool(4);
std::atomic<int> counter{0};
std::vector<std::future<void>> futures;

for (int i = 0; i < 100; ++i) {
    futures.push_back(pool.append([&counter](){
        counter.fetch_add(1, std::memory_order_relaxed);
    }));
}

// 等待所有任务完成
for (auto& f : futures) f.wait();

EXPECT_EQ(counter.load(), 100);
```

**预期输出**：
```
Submitted 100 tasks
All completed
counter=100
```

**通过标准**：`counter == 100`，无任务丢失。

---

#### case 3.2: 优先级任务，高优先级先执行

**测试目的**：验证 `append_priority` 高优先级任务优先被执行。

**运行命令**：
```bash
cd build
./threadpool_test --case=priority
```

**测试步骤**：
```cpp
ThreadPool pool(1); // 单线程避免乱序
std::vector<int> order;

pool.append_priority(std::make_shared<Task>([&](){ order.push_back(0); }, TaskPriority::LOW));
pool.append_priority(std::make_shared<Task>([&](){ order.push_back(1); }, TaskPriority::HIGH));
pool.append_priority(std::make_shared<Task>([&](){ order.push_back(2); }, TaskPriority::NORMAL));
pool.append_priority(std::make_shared<Task>([&](){ order.push_back(3); }, TaskPriority::HIGH));

pool.shutdown(); // 等待完成

// HIGH 应该在 LOW/NORMAL 之前执行，但同优先级顺序不保证
// 先收集执行顺序，验证 HIGH 组的优先级
```

**预期输出**：
```
Execution order recorded
HIGH tasks ran before LOW/NORMAL tasks: true
```

**通过标准**：高优先级任务先完成。

---

#### case 3.3: shutdown() 等待队列清空

**测试目的**：验证 `shutdown()` 优雅停止，所有队列任务被执行。

**运行命令**：
```bash
cd build
./threadpool_test --case=graceful_shutdown
```

**测试步骤**：
```cpp
ThreadPool pool(4);
std::atomic<int> completed{0};

for (int i = 0; i < 50; ++i) {
    pool.append([&completed](){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        completed++;
    });
}

pool.shutdown(); // 应该等待所有 50 个任务完成

EXPECT_EQ(completed.load(), 50);
```

**预期输出**：
```
Submitted 50 tasks
shutdown() called
All tasks completed: 50/50
```

**通过标准**：`completed == 50`，无任务被丢弃。

---

#### case 3.4: shutdown_now() 立即停止

**测试目的**：验证 `shutdown_now()` 立即停止，不等待队列。

**运行命令**：
```bash
cd build
./threadpool_test --case=force_shutdown
```

**测试步骤**：
```cpp
ThreadPool pool(4);
std::atomic<int> started{0}, completed{0};

for (int i = 0; i < 100; ++i) {
    pool.append([&](){
        started++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed++;
    });
}

std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 让部分任务开始
pool.shutdown_now();

// completed < started（部分任务未完成）
```

**预期输出**：
```
started=N, completed=M (M < N)
shutdown_now returned immediately
```

**通过标准**：`shutdown_now()` 在调用后立即返回，部分任务被丢弃（符合预期）。

---

#### case 3.5: std::future 返回值正确

**测试目的**：验证任务返回值通过 future 正确获取。

**运行命令**：
```bash
cd build
./threadpool_test --case=future_return
```

**测试步骤**：
```cpp
ThreadPool pool(2);
auto fut1 = pool.append([]{ return 42; });
auto fut2 = pool.append([]{ return std::string("hello"); });
auto fut3 = pool.append([]{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return 3.14;
});

EXPECT_EQ(fut1.get(), 42);
EXPECT_EQ(fut2.get(), "hello");
EXPECT_EQ(fut3.get(), Approx(3.14).margin(0.001));
```

**预期输出**：
```
fut1.get() = 42
fut2.get() = hello
fut3.get() = 3.14 (elapsed 50ms)
```

**通过标准**：所有 future 返回值与预期一致。

---

### 4. Timer 测试

#### case 4.1: 一次性定时器，1秒后回调执行

**测试目的**：验证一次性定时器在指定时间后触发。

**运行命令**：
```bash
cd build
./timer_test --case=one_shot
```

**测试步骤**：
```cpp
Timer timer;
std::atomic<bool> fired{false};

int64_t id = timer.add(1000, [&](){ fired = true; });
EXPECT_GT(id, 0);

int64_t start = now_ms();
while (!fired) {
    int64_t now = now_ms();
    timer.poll(now);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int64_t elapsed = now_ms() - start;
EXPECT_GE(elapsed, 990); // 至少 990ms
EXPECT_LE(elapsed, 1100); // 至多 1100ms
```

**预期输出**：
```
Timer ID=1 added (one-shot 1000ms)
Timer fired after 1003ms
timer.size()=0 after fire
```

**通过标准**：回调在 ~1000ms 后触发，timer 已清空（一次性）。

---

#### case 4.2: 周期定时器，多次触发

**测试目的**：验证周期定时器以固定间隔多次触发。

**运行命令**：
```bash
cd build
./timer_test --case=repeating
```

**测试步骤**：
```cpp
Timer timer;
std::atomic<int> count{0};

int64_t id = timer.add_repeating(100, [&](){ count++; });

int64_t start = now_ms();
while (now_ms() - start < 550) { // 运行约 550ms
    timer.poll(now_ms());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// 应触发 5 次（0, 100, 200, 300, 400, 500）
EXPECT_GE(count.load(), 4);
EXPECT_LE(count.load(), 6);
```

**预期输出**：
```
Repeating timer started (interval=100ms)
count after 550ms: 5
```

**通过标准**：`count` 在 550ms 内达到 4-6 次。

---

#### case 4.3: 取消定时器，被取消的不应执行

**测试目的**：验证 `cancel()` 能正确取消定时器。

**运行命令**：
```bash
cd build
./timer_test --case=cancel
```

**测试步骤**：
```cpp
Timer timer;
std::atomic<int> count{0};

int64_t id1 = timer.add(500, [&](){ count++; });  // 会被取消
int64_t id2 = timer.add(1000, [&](){ count++; }); // 不会被取消

std::this_thread::sleep_for(std::chrono::milliseconds(200));
timer.cancel(id1); // 取消 id1

std::this_thread::sleep_for(std::chrono::milliseconds(900));
timer.poll(now_ms());

// id2 在 1000ms 时触发，id1 应已被取消
EXPECT_EQ(count.load(), 1);
```

**预期输出**：
```
id1 cancelled at 200ms
id2 fired at 1000ms
count=1 (id1 did NOT fire)
```

**通过标准**：`count == 1`，被取消的定时器未触发。

---

### 5. MessageQueue 测试

#### case 5.1: 发布/订阅基本消息

**测试目的**：验证 MessageQueue 的发送/接收基本功能。

**运行命令**：
```bash
cd build
./message_queue_test --case=basic_send_recv
```

**测试步骤**：
```cpp
MessageQueue q;
std::atomic<bool> received{false};
std::string received_data;

std::thread sender([&](){
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    q.send(std::string("hello"));
});

std::thread receiver([&](){
    std::string val;
    if (q.recv<std::string>(val, 1000)) {
        received_data = val;
        received = true;
    }
});

sender.join();
receiver.join();

EXPECT_TRUE(received);
EXPECT_EQ(received_data, "hello");
```

**预期输出**：
```
Sent: "hello"
Received: "hello"
```

**通过标准**：消息正确传递，内容一致。

---

#### case 5.2: 广播到多个订阅者

**测试目的**：验证 `SubscribeHub` 多订阅者广播。

**运行命令**：
```bash
cd build
./message_queue_test --case=hub_broadcast
```

**测试步骤**：
```cpp
auto* hub = global_hub();
std::atomic<int> count1{0}, count2{0};

auto id1 = hub->subscribe<LoginMsg>([&](const LoginMsg& msg){
    count1++;
});
auto id2 = hub->subscribe<LoginMsg>([&](const LoginMsg& msg){
    count2++;
});

hub->publish<LoginMsg>({"alice", "pass"});
hub->publish<LoginMsg>({"bob", "pass"});

EXPECT_EQ(count1.load(), 2);
EXPECT_EQ(count2.load(), 2);

hub->unsubscribe<LoginMsg>(id1);
hub->publish<LoginMsg>({"charlie", "pass"});
// id1 已取消订阅，只有 id2 收到第 3 条
EXPECT_EQ(count2.load(), 3);
```

**预期输出**：
```
id1=1, id2=2 subscribed
After 2 publishes: count1=2, count2=2
Unsubscribed id1
After 3rd publish: count1=2, count2=3
```

**通过标准**：订阅/取消订阅正常，广播触发所有存活订阅。

---

#### case 5.3: std::variant 多类型消息

**测试目的**：验证 `std::variant` 多类型消息体在 MessageQueue 中的使用。

**运行命令**：
```bash
cd build
./message_queue_test --case=variant_message
```

**测试步骤**：
```cpp
MessageQueue q;
using Msg = std::variant<int, std::string, std::vector<double>>;

q.send(Msg{42});
q.send(Msg{std::string("text")});
q.send(Msg{std::vector<double>{1.1, 2.2, 3.3}});

Msg m;
int received = 0;
while (q.try_recv<Msg>(m)) {
    if (auto* v = std::get_if<int>(&m)) {
        EXPECT_EQ(*v, 42);
        received++;
    } else if (auto* s = std::get_if<std::string>(&m)) {
        EXPECT_EQ(*s, "text");
        received++;
    } else if (auto* d = std::get_if<std::vector<double>>(&m)) {
        EXPECT_EQ(d->size(), 3);
        received++;
    }
}

EXPECT_EQ(received, 3);
```

**预期输出**：
```
Received 3 variant messages (int, string, vector<double>)
All types correctly extracted
```

**通过标准**：三种类型均正确提取。

---

### 6. EventLoop 测试

#### case 6.1: 注册 fd handler，触发事件回调

**测试目的**：验证 EventLoop 注册 fd 读事件，socket 可读时触发回调。

**运行命令**：
```bash
cd build
./event_loop_test --case=fd_event
```

**测试步骤**：
```cpp
// 创建一对 connected socket
int pipefd[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);

auto loop = create_event_loop();
loop->init();

std::atomic<bool> read_called{false};

class TestHandler : public EventHandler {
public:
    void on_read(int fd) override {
        char buf[64];
        int n = read(fd, buf, sizeof(buf));
        if (n > 0) read_called = true;
    }
};

TestHandler handler;
loop->add_fd(pipefd[0], EventType::READ, &handler);

std::thread writer([&](){
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    write(pipefd[1], "hello", 5);
});

loop->run_once(100); // 等待 100ms
writer.join();

EXPECT_TRUE(read_called.load());
```

**预期输出**：
```
fd registered for READ event
Writer sent "hello"
Handler on_read() called
```

**通过标准**：`read_called == true`。

---

#### case 6.2: 定时器在 EventLoop 中触发

**测试目的**：验证 EventLoop 内嵌定时器正确触发。

**运行命令**：
```bash
cd build
./event_loop_test --case=timer_event
```

**测试步骤**：
```cpp
auto loop = create_event_loop();
loop->init();

std::atomic<int> timer_count{0};

int64_t tid = loop->add_timer(50, [&](){
    timer_count++;
}, 0); // 一次性

std::thread runner([&](){
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop->stop();
});

loop->run();
runner.join();

EXPECT_EQ(timer_count.load(), 1);
```

**预期输出**：
```
Timer added (50ms one-shot)
Timer fired after 52ms
loop stopped
```

**通过标准**：`timer_count == 1`。

---

### 7. Config 测试

#### case 7.1: JSON 配置解析

**测试目的**：验证 JSON 配置文件正确解析。

**运行命令**：
```bash
cd build
./config_test --case=json_parse
```

**测试步骤**：
```json
// config.json
{
    "server": {
        "host": "0.0.0.0",
        "port": 8080,
        "max_connections": 1000
    },
    "database": {
        "enabled": true,
        "pool_size": 8.5
    }
}
```

```cpp
Config cfg;
ASSERT_TRUE(cfg.load_json("config.json"));

EXPECT_EQ(cfg.get("server.host")->as_string(), "0.0.0.0");
EXPECT_EQ(cfg.get("server.port")->as_int(), 8080);
EXPECT_EQ(cfg.get("server.max_connections")->as_int(), 1000);
EXPECT_EQ(cfg.get("database.enabled")->as_bool(), true);
EXPECT_DOUBLE_EQ(cfg.get("database.pool_size")->as_double(), 8.5);
```

**预期输出**：
```
Loaded JSON config.json
server.host = 0.0.0.0
server.port = 8080
database.enabled = true
```

**通过标准**：所有字段正确解析，无遗漏。

---

#### case 7.2: INI 配置解析

**测试目的**：验证 INI 文件正确解析。

**运行命令**：
```bash
cd build
./config_test --case=ini_parse
```

**测试步骤**：
```ini
; config.ini
[server]
host=localhost
port=9000

[database]
driver=sqlite
path=./data.db
```

```cpp
Config cfg;
ASSERT_TRUE(cfg.load_ini("config.ini"));

EXPECT_EQ(cfg.get("server.host")->as_string(), "localhost");
EXPECT_EQ(cfg.get("server.port")->as_int(), 9000);
EXPECT_EQ(cfg.get("database.driver")->as_string(), "sqlite");
EXPECT_EQ(cfg.get("database.path")->as_string(), "./data.db");
```

**预期输出**：
```
Loaded INI config.ini
[server] host=localhost port=9000
[database] driver=sqlite path=./data.db
```

**通过标准**：所有节和键值正确解析。

---

#### case 7.3: get_string/int/float/bool 带默认值

**测试目的**：验证 `get_value` 带默认值的正确行为。

**运行命令**：
```bash
cd build
./config_test --case=get_with_default
```

**测试步骤**：
```cpp
Config cfg;
cfg.load_json("config.json");

// 存在的键
EXPECT_EQ(cfg.get_value("server.port", 0), 8080);
EXPECT_EQ(cfg.get_value("server.host", ""), "0.0.0.0");

// 不存在的键，使用默认值
EXPECT_EQ(cfg.get_value("server.missing", 999), 999);
EXPECT_EQ(cfg.get_value("server.missing", "default"), "default");
EXPECT_DOUBLE_EQ(cfg.get_value("nonexistent", 3.14), 3.14);
EXPECT_FALSE(cfg.get_value("nonexistent", true));
```

**预期输出**：
```
server.port (exists) = 8080
server.missing (not found) = 999 (default)
```

**通过标准**：存在键返回值正确，不存在键返回默认值。

---

### 8. TcpServer 测试

#### case 8.1: 启动服务，客户端连接，收发数据

**测试目的**：验证 TcpServer 启动、接受连接、收发数据。

**运行命令**：
```bash
cd build
./tcp_test --case=server_basic
```

**测试步骤**：
```cpp
TcpServer server(9876, 2);
std::atomic<int> connected_count{0};
std::atomic<int> received_count{0};

server.set_connected_handler([&](int64_t sid){
    connected_count++;
});

server.set_message_handler([&](int64_t sid, const Packet& pkt){
    received_count++;
    // Echo back
    server.send_to(sid, CMD_DATA, "echo: " + pkt.data);
});

ASSERT_TRUE(server.start());
EXPECT_TRUE(server.is_running());
EXPECT_EQ(server.session_count(), 0);

// 用 tcp_client_test 连接并发送数据（见 TcpClient 测试）
// ...

server.stop();
```

**预期输出**：
```
TcpServer started on port 9876
Client connected: session_id=1
Message received (len=X)
Echo sent back
Server stopped
```

**通过标准**：连接建立，消息收发正常，shutdown 后无泄漏。

---

#### case 8.2: 广播消息给所有客户端

**测试目的**：验证 `broadcast()` 将消息发送给所有已连接客户端。

**运行命令**：
```bash
cd build
./tcp_test --case=broadcast
```

**测试步骤**：
1. 启动服务器
2. 用 3 个客户端连接
3. 服务器调用 `broadcast(CMD_DATA, "hello all")`
4. 验证 3 个客户端均收到 "hello all"

**预期输出**：
```
3 clients connected
broadcast sent
All 3 clients received "hello all"
```

**通过标准**：所有客户端均收到广播消息。

---

#### case 8.3: 心跳超时断开（客户端静默）

**测试目的**：验证服务器在客户端长时间无消息时自动断开。

**运行命令**：
```bash
cd build
./tcp_test --case=heartbeat_timeout
```

**测试步骤**：
```cpp
TcpServer server(9877, 1);
server.set_idle_timeout(2000); // 2秒超时
server.start();

// 连接后静默（不发任何数据）
auto client = TcpClient::connect("127.0.0.1", 9877);
std::this_thread::sleep_for(std::chrono::seconds(3));

// 服务器应已断开该客户端
EXPECT_EQ(server.session_count(), 0);
```

**预期输出**：
```
Client connected (silent)
Waited 3s (timeout=2s)
Server disconnected idle client
session_count=0
```

**通过标准**：静默客户端在超时后被断开。

---

#### case 8.4: 拆包（一次收多个包）

**测试目的**：验证 TCP 粘包处理能力，一次收到多个包能正确拆分。

**运行命令**：
```bash
cd build
./tcp_test --case=multi_packet
```

**测试步骤**：
1. 客户端连续发送 5 个数据包（无延迟）
2. 服务端一次性读取，验证收到 5 个独立包

**预期输出**：
```
Sent 5 packets from client (no delay)
Received 5 packets on server
cmd=0x0004, seq=1
cmd=0x0004, seq=2
cmd=0x0004, seq=3
cmd=0x0004, seq=4
cmd=0x0004, seq=5
All 5 packets correctly de-multiplexed
```

**通过标准**：5 个包全部正确解析，无混淆、无丢失。

---

### 9. TcpClient 测试

#### case 9.1: 连接服务器，发送数据包，接收响应

**测试目的**：验证 TcpClient 连接服务器、发送请求、接收响应的完整流程。

**运行命令**：
```bash
cd build
./tcp_test --case=client_basic
```

**测试步骤**：
```cpp
// 先启动 TcpServer（见 8.1）
TcpClient client("127.0.0.1", 9876);
std::atomic<bool> connected{false};
std::atomic<bool> got_response{false};

client.set_on_connected([&](){ connected = true; });
client.set_on_message([&](const std::string& data){
    got_response = true;
    LOG_INFO("Client received: %s", data.c_str());
});

ASSERT_TRUE(client.connect());
EXPECT_TRUE(client.is_connected());

// 等待连接回调
std::this_thread::sleep_for(std::chrono::milliseconds(100));
EXPECT_TRUE(connected.load());

// 发送数据
client.send(CMD_DATA, "hello server");

// 等待响应
std::this_thread::sleep_for(std::chrono::milliseconds(500));
EXPECT_TRUE(got_response);

client.disconnect();
EXPECT_FALSE(client.is_connected());
```

**预期输出**：
```
Connected to 127.0.0.1:9876
on_connected() called
Sent: CMD_DATA "hello server"
Received: "echo: hello server"
on_message() called
Disconnected
```

**通过标准**：连接/发送/接收/断开均正常。

---

#### case 9.2: 断开服务器，自动重连

**测试目的**：验证 TcpClient 检测到断线后自动重连。

**运行命令**：
```bash
cd build
./tcp_test --case=auto_reconnect
```

**测试步骤**：
```cpp
TcpClient client("127.0.0.1", 9876);
client.set_on_connected([&](){ LOG_INFO("Reconnected!"); });

ASSERT_TRUE(client.connect());
EXPECT_TRUE(client.is_connected());

// 服务器主动断开（模拟 server.stop() 5秒后重启）
// 注意：当前实现的重连为后台线程检测断线后自动重连
// 这里模拟：close socket，触发重连
close_socket(client.sock()); // 模拟断线
std::this_thread::sleep_for(std::chrono::seconds(2));

// client 应已触发重连
EXPECT_TRUE(client.is_connected()); // 重连成功后恢复
```

**预期输出**：
```
Connected to server
Connection lost (simulated)
Reconnect thread running...
Reconnected to 127.0.0.1:9876
```

**通过标准**：检测到断线后自动重连成功，`is_connected()` 恢复 true。

---

### 10. SessionMgr 测试

#### case 10.1: 添加/删除会话，计数正确

**测试目的**：验证 SessionMgr 添加/删除会话。

**运行命令**：
```bash
cd build
./session_mgr_test --case=basic_crud
```

**测试步骤**：
```cpp
SessionMgr mgr;
mgr.init();

int64_t sid1 = mgr.add(10, "192.168.1.1", 12345);
int64_t sid2 = mgr.add(11, "192.168.1.2", 12346);
int64_t sid3 = mgr.add(12, "192.168.1.3", 12347);

EXPECT_EQ(mgr.count(), 3);
EXPECT_TRUE(mgr.exists(sid1));
EXPECT_TRUE(mgr.exists(sid2));

mgr.remove(sid2);
EXPECT_EQ(mgr.count(), 2);
EXPECT_FALSE(mgr.exists(sid2));

mgr.shutdown();
```

**预期输出**：
```
Added 3 sessions: sid=1,2,3
count()=3
remove(sid=2)
count()=2
```

**通过标准**：计数与实际操作一致，无泄漏。

---

#### case 10.2: 会话超时自动清理

**测试目的**：验证 `cleanup_timeout` 正确清理超时会话。

**运行命令**：
```bash
cd build
./session_mgr_test --case=timeout_cleanup
```

**测试步骤**：
```cpp
SessionMgr mgr;
mgr.init();

int64_t sid1 = mgr.add(10, "192.168.1.1", 12345);
// 手动设置 last_msg_time 为过去
mgr.get(sid1)->last_msg_time = get_tick_ms() - 40000; // 40秒前

int64_t sid2 = mgr.add(11, "192.168.1.2", 12346);
// 最近的消息
mgr.get(sid2)->last_msg_time = get_tick_ms(); // 现在

size_t removed = mgr.cleanup_timeout(get_tick_ms(), 30000);
// sid1 超过 30s，sid2 没有

EXPECT_EQ(removed, 1);
EXPECT_EQ(mgr.count(), 1);
EXPECT_TRUE(mgr.exists(sid2));

mgr.shutdown();
```

**预期输出**：
```
Added 2 sessions
cleanup_timeout(now, 30000): removed=1
remaining sessions: sid2 only
```

**通过标准**：`removed == 1`，sid2 保留。

---

#### case 10.3: 广播消息

**测试目的**：验证 `broadcast` 将消息分发给所有存活会话。

**运行命令**：
```bash
cd build
./session_mgr_test --case=broadcast
```

**测试步骤**：
```cpp
SessionMgr mgr;
mgr.init();

int64_t sid1 = mgr.add(10, "192.168.1.1", 12345);
int64_t sid2 = mgr.add(11, "192.168.1.2", 12346);
int64_t sid3 = mgr.add(12, "192.168.1.3", 12347);

std::vector<int64_t> received;
auto broadcaster = [&](int64_t sid, const uint8_t* data, size_t len){
    received.push_back(sid);
};

std::vector<uint8_t> msg = {'h','i'};
mgr.broadcast(msg, broadcaster);

EXPECT_EQ(received.size(), 3);
EXPECT_TRUE(std::find(received.begin(), received.end(), sid1) != received.end());
EXPECT_TRUE(std::find(received.begin(), received.end(), sid2) != received.end());
EXPECT_TRUE(std::find(received.begin(), received.end(), sid3) != received.end());

mgr.shutdown();
```

**预期输出**：
```
Broadcast to 3 sessions
All 3 sessions received the broadcast
```

**通过标准**：3 个会话均收到广播。

---

### 11. Router 测试

#### case 11.1: 注册命令字路由，调用正确 handler

**测试目的**：验证 Router 将消息路由到对应命令字的 Handler。

**运行命令**：
```bash
cd build
./router_test --case=basic_route
```

**测试步骤**：
```cpp
Router router;
std::vector<std::string> called;

router.reg(CMD_LOGIN, [&](int64_t sid, const std::string& data){
    called.push_back("login:" + data);
});

router.reg(CMD_LOGOUT, [&](int64_t sid, const std::string& data){
    called.push_back("logout:" + data);
});

router.reg(CMD_DATA, [&](int64_t sid, const std::string& data){
    called.push_back("data:" + data);
});

// 路由消息
router.route(100, Packet{CMD_LOGIN, "alice"});
router.route(100, Packet{CMD_DATA, "hello"});
router.route(100, Packet{CMD_LOGOUT, ""});

ASSERT_EQ(called.size(), 3);
EXPECT_EQ(called[0], "login:alice");
EXPECT_EQ(called[1], "data:hello");
EXPECT_EQ(called[2], "logout:");
```

**预期输出**：
```
Registered handlers for: 0x02, 0x03, 0x04
Routed 3 messages
handler[login] called with "alice"
handler[data] called with "hello"
handler[logout] called with ""
```

**通过标准**：所有注册的命令字正确路由，未注册的静默忽略。

---

#### case 11.2: 未注册命令字不崩溃

**测试目的**：验证对未注册命令字调用 `route` 不会崩溃。

**运行命令**：
```bash
cd build
./router_test --case=unregistered_no_crash
```

**测试步骤**：
```cpp
Router router;
router.reg(CMD_LOGIN, [](int64_t, const std::string&){});

// 发送未注册命令字
router.route(1, Packet{0x9999, "test"}); // 无 handler
router.route(1, Packet{0x0001, "test"}); // 已注册

// 不应崩溃，程序继续执行
LOG_INFO("Router handled unregistered command gracefully");
```

**预期输出**：
```
Router route() called with unregistered cmd=0x9999
No crash, gracefully ignored
```

**通过标准**：程序不崩溃，未注册命令字不触发任何 handler。

---

### 12. Codec 测试

#### case 12.1: 编码后解码，数据一致

**测试目的**：验证 Codec 编码/解码后数据一致。

**运行命令**：
```bash
cd build
./codec_test --case=basic_encode_decode
```

**测试步骤**：
```cpp
Codec codec;
std::string payload = "Hello, World!";

auto encoded = codec.encode(CMD_DATA, payload.data(), payload.size(), 123);

ASSERT_GE(encoded.size(), HEADER_SIZE + payload.size());
EXPECT_EQ(encoded[13], CMD_DATA & 0xFF);      // cmd 低字节
EXPECT_EQ(encoded[14], (CMD_DATA >> 8) & 0xFF); // cmd 高字节

auto decoded = codec.decode_packet(encoded);
ASSERT_TRUE(decoded.has_value());
EXPECT_EQ(decoded->seq, 123);
EXPECT_EQ(decoded->cmd, CMD_DATA);
EXPECT_EQ(decoded->data, payload);
```

**预期输出**：
```
Encoded payload (len=14) -> buffer (size=29)
Decoded: seq=123, cmd=0x0004, data="Hello, World!"
Data matches original
```

**通过标准**：编解码后数据完全一致。

---

#### case 12.2: CRC32 校验，错误数据检测

**测试目的**：验证 CRC32 校验能检测数据损坏。

**运行命令**：
```bash
cd build
./codec_test --case=crc32_verify
```

**测试步骤**：
```cpp
Codec codec;
std::vector<uint8_t> data = {1,2,3,4,5};
uint32_t crc = CRC32::calculate(data.data(), data.size());

// 正确数据通过校验
EXPECT_TRUE(CRC32::verify(data.data(), data.size(), crc));

// 篡改数据
data[2] = 99;
EXPECT_FALSE(CRC32::verify(data.data(), data.size(), crc));

// 编码后篡改
auto encoded = codec.encode(CMD_DATA, data.data(), data.size(), 1);
encoded[20] ^= 0xFF; // 篡改数据区域某字节
auto decoded = codec.decode_packet(encoded);
EXPECT_FALSE(decoded.has_value()); // CRC 校验失败
```

**预期输出**：
```
CRC32(data) = 0x...
verify(correct data) = true
verify(tampered data) = false
Decoded tampered packet = std::nullopt (CRC check failed)
```

**通过标准**：正常数据通过，篡改数据被检测到。

---

#### case 12.3: 字节序转换（大小端）

**测试目的**：验证 ByteOrder 正确处理大小端转换。

**运行命令**：
```bash
cd build
./codec_test --case=byteorder
```

**测试步骤**：
```cpp
ByteOrder::init();

// 32 位
uint32_t v32 = 0x12345678;
uint32_t h32 = ByteOrder::htonl(v32);
uint32_t n32 = ByteOrder::ntohl(h32);
EXPECT_EQ(n32, v32); // 往返一致

// 64 位
uint64_t v64 = 0x123456789ABCDEF0ULL;
uint64_t h64 = ByteOrder::htonll(v64);
uint64_t n64 = ByteOrder::ntohll(h64);
EXPECT_EQ(n64, v64); // 往返一致

// 16 位
uint16_t v16 = 0x1234;
uint16_t h16 = ByteOrder::htons(v16);
uint16_t n16 = ByteOrder::ntohs(h16);
EXPECT_EQ(n16, v16); // 往返一致

// 小端机字节序
if (ByteOrder::is_little_endian()) {
    EXPECT_EQ(h32, 0x78563412); // 字节反转
}
```

**预期输出**：
```
htonl(0x12345678) = 0x78563412 (little-endian machine)
ntohll(0x123456789ABCDEF0) = 0xEFCDAB8967452301 (little-endian)
ByteOrder round-trip: 32bit OK, 64bit OK, 16bit OK
```

**通过标准**：往返转换后值不变，大小端机字节序正确。

---

### 13. SignalHandler 测试

#### case 13.1: 触发 SIGINT，平滑关闭回调被调用

**测试目的**：验证 SignalHandler 捕获 SIGINT 并按顺序调用 shutdown 回调。

**运行命令**：
```bash
cd build
./signal_handler_test --case=sigint_shutdown
```

**测试步骤**：
```cpp
SignalHandler sig;
std::vector<int> order;

sig.on_shutdown([&](){ order.push_back(1); });
sig.on_shutdown([&](){ order.push_back(2); });
sig.on_shutdown([&](){ order.push_back(3); });

sig.start();

// 在另一个线程模拟发送 SIGINT
raise(SIGINT);

std::this_thread::sleep_for(std::chrono::milliseconds(100));

EXPECT_TRUE(sig.is_shutting_down());
EXPECT_EQ(order.size(), 3);
EXPECT_EQ(order[0], 1);
EXPECT_EQ(order[1], 2);
EXPECT_EQ(order[2], 3);
```

**预期输出**：
```
SignalHandler started
SIGINT received
Shutdown callbacks invoked in order:
  1) first callback
  2) second callback
  3) third callback
is_shutting_down() = true
```

**通过标准**：所有回调按注册顺序执行。

---

#### case 13.2: 三阶段关闭顺序正确

**测试目的**：验证三阶段关闭（停止接受→等队列清空→关闭连接）的正确顺序。

**运行命令**：
```bash
cd build
./signal_handler_test --case=three_phase_shutdown
```

**测试步骤**：
```cpp
SignalHandler sig;
std::vector<std::string> phases;

sig.on_shutdown([&](){
    // Phase 1: 停止接受新连接
    server.stop_accept();
    phases.push_back("stop_accept");
});

sig.on_shutdown([&](){
    // Phase 2: 等待队列清空
    thread_pool->shutdown();
    phases.push_back("queue_drain");
});

sig.on_shutdown([&](){
    // Phase 3: 关闭所有连接
    server.close_all_connections();
    session_mgr->shutdown();
    phases.push_back("close_connections");
});

sig.start();
raise(SIGTERM);
std::this_thread::sleep_for(std::chrono::milliseconds(200));

ASSERT_EQ(phases.size(), 3);
EXPECT_EQ(phases[0], "stop_accept");
EXPECT_EQ(phases[1], "queue_drain");
EXPECT_EQ(phases[2], "close_connections");
```

**预期输出**：
```
Phase 1: stop_accept
Phase 2: queue_drain
Phase 3: close_connections
Order: correct
```

**通过标准**：三个阶段按顺序执行，无跨越。

---

## Part 2: 集成测试

### 2.1 Gateway 集成测试

**测试目的**：验证从服务器启动到客户端断开整个链路正常。

**测试场景**：启动 gateway → 客户端连接 → 登录 → 收发业务数据 → 断开

**运行命令**：
```bash
cd build
./gateway_test --integration
```

**测试步骤**：
```python
# 伪代码描述测试流程
1. 启动 gateway (./gateway_example --port 8080)
2. 等待 2 秒确认启动
3. 创建 TcpClient 连接到 127.0.0.1:8080
4. 发送登录包: CMD_LOGIN, username+password
5. 验证收到登录响应
6. 发送 100 条业务数据包 CMD_DATA
7. 验证收到 100 条响应
8. 发送登出包 CMD_LOGOUT
9. 验证收到登出响应
10. 关闭客户端连接
11. 等待 1 秒
12. 验证 gateway session_count == 0（会话清理）
```

**预期输出**：
```
[Gateway] Server started on port 8080
[Client] Connected to 127.0.0.1:8080
[Client] Sent LOGIN (username=alice)
[Gateway] Session 1 login: alice
[Gateway] Sent LOGIN_ACK
[Client] Received LOGIN_ACK
[Client] Sent 100 DATA packets
[Gateway] Received 100 packets, processed in ThreadPool
[Client] Received 100 ACK packets (latency avg < 10ms)
[Client] Sent LOGOUT
[Gateway] Session 1 logout
[Gateway] Session cleaned up
[Client] Disconnected
session_count after cleanup: 0
All integration checks PASSED
```

**通过标准**：
- 服务器正常启动
- 登录成功
- 100 条数据全部收到响应
- 登出成功
- 会话完全清理（session_count=0）
- 无内存泄漏

---

### 2.2 高并发测试

**测试目的**：验证 100 个并发客户端同时连接并通信，服务器无崩溃、无内存泄漏。

**运行命令**：
```bash
cd build
./gateway_test --stress --clients=100 --duration=30
```

**测试步骤**：
1. 启动 gateway（监听端口 8080）
2. 启动 100 个 TcpClient 连接
3. 每个客户端每秒发送 10 条消息（CMD_DATA, 随机 payload）
4. 持续 30 秒
5. 监控：
   - 服务器 CPU 使用率 < 80%
   - 服务器内存 RSS 稳定（不持续增长）
   - session_count == 100
   - 无崩溃、无断言失败
6. 30 秒后所有客户端断开
7. 验证 server.session_count == 0

**预期输出**：
```
[Stress Test] Starting 100 concurrent clients
[Stress Test] Running for 30 seconds...
[Stress Test] At 10s: connections=100, avg_latency=5ms, memory_rss=20480KB
[Stress Test] At 20s: connections=100, avg_latency=6ms, memory_rss=20960KB
[Stress Test] At 30s: connections=100, avg_latency=5ms, memory_rss=21280KB
[Stress Test] Memory stable, no leak detected
[Stress Test] All clients disconnecting...
[Stress Test] session_count=0 after cleanup
STRESS TEST PASSED
```

**通过标准**：
- 无崩溃（crash）
- 无内存持续增长（30秒内 RSS 增长 < 10MB）
- 所有消息收发计数匹配
- Session 清理完整

---

## Part 3: 测试运行步骤

### 环境准备

```bash
# 1. 进入项目目录
cd C:\Users\UEFR\.openclaw\workspace\cpp-server-framework

# 2. 创建并进入 build 目录
mkdir build
cd build

# 3. CMake 配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 4. 编译所有测试
make -j4
```

### 运行所有单元测试

```bash
cd build
ctest --output-on-failure
```

### 运行指定模块测试

```bash
cd build

# Logger 测试
./logger_test

# ThreadPool 测试
./threadpool_test

# TcpServer/TcpClient 测试
./tcp_test

# 所有测试（verbose）
ctest -V
```

### 运行集成测试

```bash
cd build

# 先在后台启动 gateway
./gateway_example --port 8080 &
GATEWAY_PID=$!

sleep 2

# 运行集成测试
./gateway_test --integration

# 结束 gateway
kill $GATEWAY_PID
```

---

## Part 4: 测试结果记录表

| 测试用例 | 模块 | 运行命令 | 状态 | 日期 | 备注 |
|----------|------|----------|------|------|------|
| case 1.1: 初始化日志文件 | logger | `./logger_test --case=init_and_levels` | - | - | - |
| case 1.2: 日志滚动 | logger | `./logger_test --case=log_rotation` | - | - | - |
| case 1.3: 异步落盘不阻塞 | logger | `./logger_test --case=async_nonblocking` | - | - | - |
| case 2.1: MemoryPool allocate/deallocate | memory | `./memory_pool_test --case=basic_alloc` | - | - | - |
| case 2.2: ObjectPool<int> acquire/release | object_pool | `./object_pool_test --case=basic_pool` | - | - | - |
| case 2.3: 并发 acquire/release | object_pool | `./object_pool_test --case=concurrent` | - | - | - |
| case 3.1: 100 任务全部完成 | threadpool | `./threadpool_test --case=basic_tasks` | - | - | - |
| case 3.2: 优先级任务 | threadpool | `./threadpool_test --case=priority` | - | - | - |
| case 3.3: shutdown() 等待队列清空 | threadpool | `./threadpool_test --case=graceful_shutdown` | - | - | - |
| case 3.4: shutdown_now() 立即停止 | threadpool | `./threadpool_test --case=force_shutdown` | - | - | - |
| case 3.5: std::future 返回值 | threadpool | `./threadpool_test --case=future_return` | - | - | - |
| case 4.1: 一次性定时器 | timer | `./timer_test --case=one_shot` | - | - | - |
| case 4.2: 周期定时器 | timer | `./timer_test --case=repeating` | - | - | - |
| case 4.3: 取消定时器 | timer | `./timer_test --case=cancel` | - | - | - |
| case 5.1: 发布/订阅基本消息 | message_queue | `./message_queue_test --case=basic_send_recv` | - | - | - |
| case 5.2: 广播到多个订阅者 | message_queue | `./message_queue_test --case=hub_broadcast` | - | - | - |
| case 5.3: std::variant 多类型消息 | message_queue | `./message_queue_test --case=variant_message` | - | - | - |
| case 6.1: 注册 fd handler 事件回调 | event | `./event_loop_test --case=fd_event` | - | - | - |
| case 6.2: 定时器在 EventLoop 中触发 | event | `./event_loop_test --case=timer_event` | - | - | - |
| case 7.1: JSON 配置解析 | config | `./config_test --case=json_parse` | - | - | - |
| case 7.2: INI 配置解析 | config | `./config_test --case=ini_parse` | - | - | - |
| case 7.3: get 带默认值 | config | `./config_test --case=get_with_default` | - | - | - |
| case 8.1: TCP 服务收发数据 | tcp | `./tcp_test --case=server_basic` | - | - | - |
| case 8.2: 广播消息 | tcp | `./tcp_test --case=broadcast` | - | - | - |
| case 8.3: 心跳超时断开 | tcp | `./tcp_test --case=heartbeat_timeout` | - | - | - |
| case 8.4: 拆包多个包 | tcp | `./tcp_test --case=multi_packet` | - | - | - |
| case 9.1: TCP 客户端连接收发 | tcp | `./tcp_test --case=client_basic` | - | - | - |
| case 9.2: 断开重连 | tcp | `./tcp_test --case=auto_reconnect` | - | - | - |
| case 10.1: SessionMgr CRUD | session | `./session_mgr_test --case=basic_crud` | - | - | - |
| case 10.2: 超时自动清理 | session | `./session_mgr_test --case=timeout_cleanup` | - | - | - |
| case 10.3: 广播消息 | session | `./session_mgr_test --case=broadcast` | - | - | - |
| case 11.1: 注册命令字路由 | router | `./router_test --case=basic_route` | - | - | - |
| case 11.2: 未注册命令字不崩溃 | router | `./router_test --case=unregistered_no_crash` | - | - | - |
| case 12.1: 编码后解码 | codec | `./codec_test --case=basic_encode_decode` | - | - | - |
| case 12.2: CRC32 校验 | codec | `./codec_test --case=crc32_verify` | - | - | - |
| case 12.3: 字节序转换 | codec | `./codec_test --case=byteorder` | - | - | - |
| case 13.1: SIGINT 平滑关闭 | signal | `./signal_handler_test --case=sigint_shutdown` | - | - | - |
| case 13.2: 三阶段关闭顺序 | signal | `./signal_handler_test --case=three_phase_shutdown` | - | - | - |
| Gateway 集成测试 | integration | `./gateway_test --integration` | - | - | - |
| 高并发压力测试 | stress | `./gateway_test --stress --clients=100` | - | - | - |

---

*文档版本: 1.0 | 更新日期: 2026-05-19*