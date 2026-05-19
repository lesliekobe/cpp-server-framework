# C++ Industrial Server Framework - API 参考文档

> 本文档列出框架所有公开 API，包括函数签名、参数说明、返回值、线程安全特性及使用示例。

---

## adapter/ — 跨平台适配层

### platform.h

#### 平台初始化与清理

```cpp
int platform_init();
```
- **说明**：初始化平台资源（Windows 下启动 WSA）。应在所有网络操作前调用。
- **返回值**：0 成功，-1 失败。
- **线程安全**：主线程调用一次。
- **示例**：
```cpp
platform_init();
```

---

```cpp
int platform_shutdown();
```
- **说明**：清理平台资源（Windows 下关闭 WSA）。程序退出前调用。
- **返回值**：0 成功。
- **线程安全**：主线程调用一次。

---

#### 线程与时间

```cpp
int get_process_id();
```
- **说明**：获取当前进程 ID。
- **返回值**：进程 ID（int）。

```cpp
int get_thread_id();
```
- **说明**：获取当前线程 ID（跨平台实现）。
- **返回值**：线程 ID。

```cpp
inline ThreadId get_current_thread_id();  // 见 platform.h
```
- **说明**：获取当前线程 ID（等同于 get_thread_id()）。
- **返回值**：`ThreadId` 类型。

---

```cpp
uint64_t get_tick_ms();
```
- **说明**：获取启动以来经过的毫秒数（monotonic clock，不受系统时间调整影响）。
- **返回值**：毫秒数（64位无符号）。
- **线程安全**：是。
- **示例**：
```cpp
uint64_t now = get_tick_ms();
```

---

```cpp
uint64_t get_tick_us();
```
- **说明**：获取微秒级 monotonic 时间。
- **返回值**：微秒数。

---

```cpp
int64_t get_real_time_ms();
```
- **说明**：获取真实 wall clock 毫秒数（可被 NTP 调整）。
- **返回值**：毫秒数。

---

```cpp
void sleep_ms(int ms);
void sleep_us(int us);
```
- **说明**：线程 sleep 指定毫秒/微秒。
- **参数**：`ms` 毫秒数，`us` 微秒数。

---

#### Socket 操作

```cpp
int close_socket(SocketHandle sock);
```
- **说明**：跨平台安全关闭 socket。
- **参数**：`sock` - socket 句柄。
- **返回值**：0 成功，-1 失败。
- **线程安全**：是。

---

```cpp
int set_nonblocking(SocketHandle sock);
```
- **说明**：设置 socket 为非阻塞模式。
- **返回值**：0 成功，-1 失败。

---

```cpp
int set_reuseaddr(SocketHandle sock);
```
- **说明**：设置 SO_REUSEADDR（允许重启后快速绑定同一端口）。
- **返回值**：0 成功，-1 失败。

---

```cpp
int set_nodelay(SocketHandle sock);
```
- **说明**：设置 TCP_NODELAY（禁用 Nagle 算法，降低延迟）。
- **返回值**：0 成功，-1 失败。

---

```cpp
int bind_and_listen(SocketHandle sock, int port);
```
- **说明**：绑定端口并开始监听。
- **参数**：`sock` - 已创建并配置好的 socket；`port` - 监听端口。
- **返回值**：0 成功，-1 失败。

---

```cpp
SocketHandle accept_connection(SocketHandle server_sock);
```
- **说明**：接受客户端连接。
- **参数**：`server_sock` - 监听 socket。
- **返回值**：新客户端 socket，失败返回 `INVALID_SOCKET`。

---

#### 文件操作

```cpp
int file_open(const char* path, int flags);
int file_close(int fd);
int file_read(int fd, void* buf, size_t len);
int file_write(int fd, const void* buf, size_t len);
int64_t file_seek(int fd, int64_t off, int whence);
int64_t file_tell(int fd);
```
- **说明**：跨平台文件操作（封装标准 POSIX/Windows API）。
- **返回值**：`file_read/file_write` 返回实际读/写字节数；`file_seek` 返回新位置；`file_tell` 返回当前偏移。

---

#### 原子操作

```cpp
template<typename T>
T atomic_load(std::atomic<T>* v);

template<typename T>
void atomic_store(std::atomic<T>* v, T val);

template<typename T>
T atomic_exchange(std::atomic<T>* v, T val);

template<typename T>
bool atomic_compare_exchange(std::atomic<T>* v, T* expected, T desired);
```
- **说明**：原子操作封装模板（使用 `memory_order_acquire/release`）。
- **参数**：`v` - 原子变量指针；`expected` - 比较值（CAS）；`desired` - 新值。
- **返回值**：`atomic_exchange` 返回旧值；`atomic_compare_exchange` 成功返回 true。

---

#### 其他工具

```cpp
bool set_thread_name(const std::string& name);
```
- **说明**：设置线程名称（Linux 通过 `prctl(PR_SET_NAME)`，Windows 通过命名thread）。
- **返回值**：成功 true。

```cpp
int create_eventfd();     // Linux eventfd，用于唤醒 EventLoop
int create_timerfd();      // Linux timerfd，用于精准定时
int close_fd(int fd);      // 关闭任意 fd
```

---

## base/logger/ — 日志模块

### Logger

#### 单例 & 初始化

```cpp
static Logger* instance();
```
- **说明**：获取 Logger 单例。
- **返回值**：Logger 全局实例指针。

---

```cpp
bool init(const std::vector<LogSink>& sinks,
          LogLevel console_level = LogLevel::INFO,
          LogLevel file_level = LogLevel::DEBUG,
          const LogFormatter& fmt = LogFormatter{});
```
- **说明**：多 sink 初始化（最完整接口）。
- **参数**：
  - `sinks` - 日志输出目标列表（可多个文件分别配置）
  - `console_level` - 控制台输出最低级别
  - `file_level` - 文件输出最低级别
  - `fmt` - 格式化配置
- **返回值**：true 成功。
- **示例**：
```cpp
std::vector<LogSink> sinks = {
    {"./logs", "server", 200, 7, true, true}
};
Logger::instance()->init(sinks, LogLevel::INFO, LogLevel::DEBUG);
```

---

```cpp
bool init(const std::string& log_dir,
          const std::string& filename_prefix,
          LogLevel console_level = LogLevel::INFO,
          LogLevel file_level = LogLevel::DEBUG);
```
- **说明**：简化初始化（单一文件 sink）。
- **示例**：
```cpp
Logger::instance()->init("./logs", "gateway",
    LogLevel::INFO, LogLevel::DEBUG);
```

---

#### 级别设置

```cpp
void set_console_level(LogLevel lvl);
void set_file_level(LogLevel lvl);
void set_level(LogLevel lvl);
```
- **说明**：设置日志输出级别。低于设置级别的日志不会被输出。

---

#### 日志输出

```cpp
void trace(const char* fmt, ...);
void debug(const char* fmt, ...);
void info (const char* fmt, ...);
void warn (const char* fmt, ...);
void error(const char* fmt, ...);
void fatal(const char* fmt, ...);
```
- **说明**：输出对应级别日志（printf 风格格式化）。
- **参数**：`fmt` - printf 格式字符串，后接对应参数。
- **线程安全**：是（异步入队）。
- **示例**：
```cpp
LOG_INFO("Server started on port %d", 8080);
LOG_ERROR("Connection failed: %s", strerror(errno));
```

---

```cpp
void log(LogLevel lvl, const char* tag, const char* fmt, ...);
```
- **说明**：带 tag 的日志（用于分类，如 `[DB]`, `[NET]`）。
- **示例**：
```cpp
LOG_TAG(LogLevel::INFO, "DB", "Query executed in %dms", elapsed_ms);
```

---

#### 控制方法

```cpp
void flush();
```
- **说明**：同步刷新所有待落盘日志（阻塞直到全部写入）。
- **线程安全**：是。

---

```cpp
void shutdown();
```
- **说明**：安全关闭日志系统（自动 flush，关闭日志线程）。
- **线程安全**：应在主线程析构前调用。

---

```cpp
using Hook = std::function<void(LogEntry&)>;
void set_hook(Hook h);
```
- **说明**：设置日志写入前回调（可用于自定义染色、敏感信息脱敏）。
- **示例**：
```cpp
Logger::instance()->set_hook([](LogEntry& e) {
    if (e.level >= LogLevel::ERROR) {
        e.tag = "[" + e.tag + "]!!!";
    }
});
```

---

### 日志宏

```cpp
#define LOG_TRACE(...)  framework::Logger::instance()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)  framework::Logger::instance()->debug(__VA_ARGS__)
#define LOG_INFO(...)   framework::Logger::instance()->info(__VA_ARGS__)
#define LOG_WARN(...)   framework::Logger::instance()->warn(__VA_ARGS__)
#define LOG_ERROR(...)  framework::Logger::instance()->error(__VA_ARGS__)
#define LOG_FATAL(...)  framework::Logger::instance()->fatal(__VA_ARGS__)
#define LOG_TAG(lvl, tag, ...)  framework::Logger::instance()->log(lvl, tag, __VA_ARGS__)
```
- **说明**：便捷日志宏（推荐使用）。
- **线程安全**：是（通过单例）。

---

### LogSink 结构

```cpp
struct LogSink {
    std::string path;             // 日志目录（空=不写文件）
    std::string filename_prefix;  // 文件名前缀
    size_t      max_file_size_mb{200};    // 单文件最大MB
    int         max_history_days{7};       // 保留天数
    bool        compress{true};   // GZip 压缩归档
    bool        async{true};      // 异步写盘
};
```

### LogFormatter 结构

```cpp
struct LogFormatter {
    bool show_timestamp{true};
    bool show_thread_id{true};
    bool show_level{true};
    bool show_tag{true};
    bool colorful{true};          // 控制台颜色
    std::string time_format{"%Y-%m-%d %H:%M:%S"};
};
```

---

## base/config/ — 配置模块

### Config

#### 加载配置

```cpp
bool load_json(const std::string& path);
bool load_ini(const std::string& path);
```
- **说明**：从文件加载 JSON / INI 格式配置。
- **返回值**：true 成功。
- **线程安全**：否（应在启动阶段调用）。

---

```cpp
void merge(const Config& other);
```
- **说明**：合并另一个 Config（后者同名键覆盖前者）。用于分层配置。
- **示例**：
```cpp
Config default_cfg;
default_cfg.load_json("default.json");
Config user_cfg;
user_cfg.load_json("user.json");
default_cfg.merge(user_cfg); // user 覆盖 default
```

---

#### 查询配置

```cpp
const ConfigNode* get(const std::string& path) const;
```
- **说明**：按路径获取配置节点（路径用 `.` 分隔，如 `"server.network.port"`）。
- **返回值**：节点指针，不存在返回 `nullptr`。
- **示例**：
```cpp
auto node = cfg.get("server.port");
if (node) port = node->as_int();
```

---

```cpp
template<typename T>
T get_value(const std::string& path, T default_val) const;
```
- **说明**：获取配置值，带默认值（路径不存在时返回默认值）。
- **示例**：
```cpp
int port = cfg.get_value("server.port", 8080);
std::string host = cfg.get_value("server.host", "localhost");
```

---

#### 热加载监视

```cpp
using ChangeCallback = std::function<void(const std::string& path)>;
void watch(const std::string& path, ChangeCallback cb);
```
- **说明**：监视配置文件变化，变化时触发回调。
- **示例**：
```cpp
cfg.watch("server.json", [](const std::string& path) {
    LOG_INFO("Config changed: %s", path.c_str());
    cfg.load_json(path); // 重新加载
});
```

---

```cpp
void poll_changes();
```
- **说明**：轮询检查文件变化（需在主循环中调用，或定时调用）。
- **线程安全**：是。

---

### ConfigNode

```cpp
bool has_key(const std::string& key) const;
const ConfigNode* get_key(const std::string& key) const;
ConfigNode* get_key(const std::string& key);
```
- **说明**：访问子节点（键值访问）。

```cpp
bool is_null()    const;
bool is_bool()    const;
bool is_int()     const;
bool is_int64()   const;
bool is_double()  const;
bool is_string()   const;
bool is_array()   const;
```

```cpp
bool               as_bool()    const;
int                as_int()     const;
int64_t            as_int64()   const;
double             as_double()  const;
std::string        as_string()  const;
```

---

### CmdLineParser

```cpp
void add(const std::string& name, char short_name, bool has_val,
         const std::string& desc, const std::string& default_val = "");
```
- **说明**：添加命令行参数定义。
- **参数**：`name` 长名称，`short_name` 短名称（如 `'p'`），`has_val` 是否有值，`desc` 帮助描述。

```cpp
bool parse(int argc, char** argv);
```
- **说明**：解析命令行参数。

```cpp
bool has(const std::string& name) const;
std::string get(const std::string& name) const;
std::string get(const std::string& name, const std::string& default_val) const;
```
- **说明**：查询参数值。

```cpp
std::string help() const;
```
- **说明**：生成帮助信息字符串。

---

## base/time/ — 时间工具

### namespace time

```cpp
int64_t now_seconds();          // Unix 秒
int64_t now_milliseconds();     // Unix 毫秒
int64_t now_microseconds();     // Unix 微秒
```
- **说明**：获取当前时间戳。
- **返回值**：时间戳值。

```cpp
std::string format_now(const char* fmt = "%Y-%m-%d %H:%M:%S");
std::string format_timestamp(int64_t ts, const char* fmt = "%Y-%m-%d %H:%M:%S");
```
- **说明**：格式化时间。
- **参数**：`fmt` - strftime 格式字符串。
- **返回值**：格式化后的字符串。

```cpp
void sleep_ms(int64_t ms);
```
- **说明**：线程 sleep。
- **线程安全**：是。

---

## base/lock/ — 锁模块

### LockGuard

```cpp
template<typename Mutex>
class LockGuard {
public:
    explicit LockGuard(Mutex& mtx);  // 构造时加锁
    ~LockGuard();                     // 析构时解锁
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};
```
- **说明**：RAII 互斥锁封装，构造即加锁，析构自动解锁。
- **线程安全**：是（取决于 Mutex）。
- **示例**：
```cpp
framework::Mutex mtx;
{
    framework::LockGuard<framework::Mutex> guard(mtx);
    // 临界区
} // 自动解锁
```

---

### ConditionVariable

```cpp
class ConditionVariable {
public:
    void wait(std::unique_lock<std::mutex>& lock);
    template<typename Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate pred);
    void notify_one();
    void notify_all();
};
```
- **说明**：条件变量封装（标准库 wrapper）。
- **示例**：
```cpp
std::mutex mtx;
framework::ConditionVariable cv;
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, []{ return data_ready; });
```

---

## base/memory/ — 内存池

### MemoryPool

```cpp
explicit MemoryPool(size_t item_size, size_t pre_alloc = 64, size_t block_size = 32);
```
- **说明**：构造内存池。
- **参数**：
  - `item_size` - 每个内存块大小（字节）
  - `pre_alloc` - 预分配块数
  - `block_size` - 每次扩容块数

---

```cpp
void* allocate();
void  deallocate(void* ptr);
```
- **说明**：从池中分配/归还内存块。
- **线程安全**：是（内部加锁）。
- **返回值**：`allocate()` 返回内存块指针，永不为空（扩容保证）。

---

```cpp
void pre_allocate(size_t count);
```
- **说明**：预热池，提前分配指定数量的内存块。

---

```cpp
size_t item_size()    const;
size_t total_blocks() const;
size_t free_blocks()  const;
size_t used_blocks()  const;
```
- **说明**：获取池统计信息。

---

### MemoryPoolManager

```cpp
static MemoryPoolManager& instance();
MemoryPool* get_pool(size_t size);
void pre_alloc();
Stats stats() const;
```
- **说明**：多规格内存池管理器（单例）。
- **示例**：
```cpp
auto* mgr = &MemoryPoolManager::instance();
auto* pool = mgr->get_pool(512); // 获取 512 字节池
```

---

## base/object_pool/ — 对象池

### ObjectPool<T>

```cpp
explicit ObjectPool(size_t pre_alloc = 0,
                    std::function<void(T*)> on_create = nullptr,
                    std::function<void(T*)> on_destroy = nullptr);
```
- **说明**：构造对象池。
- **参数**：
  - `pre_alloc` - 预分配数量
  - `on_create` - 对象创建回调（可用于初始化）
  - `on_destroy` - 对象销毁回调（可用于清理）

---

```cpp
T* allocate();
void deallocate(T* ptr);
```
- **说明**：分配/归还对象。
- **线程安全**：是（内部加锁）。
- **返回值**：`allocate()` 返回对象指针，永不为空（扩容保证）。

---

```cpp
template<typename Iter>
void deallocate_range(Iter begin, Iter end);
```
- **说明**：批量归还对象。

---

```cpp
void pre_allocate(size_t count);
void clear();
size_t pool_size()    const;
size_t allocated()    const;
size_t total_created() const;
```
- **说明**：池管理接口。

---

```cpp
template<typename T, typename... Args>
std::unique_ptr<T, std::function<void(T*)>> make_pooled(ObjectPool<T>& pool, Args&&... args);
```
- **说明**：创建池化对象（自动归还）。
- **示例**：
```cpp
auto ptr = make_pooled(pool, arg1, arg2);
// 离开作用域自动归还到池
```

---

## base/event/ — 事件驱动

### EventLoop

```cpp
explicit EventLoop(bool external = false);
```
- **说明**：构造 EventLoop。
- **参数**：`external` - true 表示外部管理生命周期（不由本类析构清理）。

---

```cpp
bool init();
```
- **说明**：初始化（创建 epoll fd 等）。
- **返回值**：true 成功。

---

```cpp
void run();
void run_once(int timeout_ms);
void stop();
```
- **说明**：事件循环控制。
- **线程安全**：`stop()` 可以在其他线程调用。

---

```cpp
bool add_fd(int fd, EventType type, EventHandler* handler);
bool del_fd(int fd);
bool mod_fd(int fd, EventType type);
```
- **说明**：注册/删除/修改 fd 事件。
- **返回值**：true 成功。
- **线程安全**：`add_fd` 可从其他线程调用（内部加锁）。

---

```cpp
int64_t add_timer(int64_t after_ms, std::function<void()> cb, int64_t interval_ms = 0);
bool cancel_timer(int64_t id);
```
- **说明**：添加定时器。
- **参数**：`after_ms` - 首次触发延迟（毫秒），`interval_ms` - 周期（0=一次性）。
- **返回值**：`add_timer` 返回定时器 ID（>0），可传 `cancel_timer()` 取消。
- **线程安全**：是。

---

```cpp
void push_task(std::function<void()> task);
```
- **说明**：投递异步任务到 EventLoop 线程执行。
- **线程安全**：是（其他线程调用）。

---

```cpp
bool is_running() const;
int poll_fd() const; // 用于外部 poll 集成
```
- **说明**：状态查询。

---

### EventHandler

```cpp
class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual void on_read(int fd)    {}
    virtual void on_write(int fd)   {}
    virtual void on_timeout(int fd) {}
    virtual void on_signal(int sig) {}
    virtual void on_error(int fd)   {}
    virtual void on_close(int fd)   {}
    virtual void on_event(int fd, EventType type);
    virtual const void* tag() const { return nullptr; }
};
```
- **说明**：事件处理基类（虚接口）。子类可选择性重写需要的方法。
- **示例**：
```cpp
class MyHandler : public framework::EventHandler {
public:
    void on_read(int fd) override {
        char buf[1024];
        int n = read(fd, buf, sizeof(buf));
        if (n > 0) { /* 处理数据 */ }
    }
    void on_error(int fd) override {
        LOG_ERROR("fd %d error", fd);
    }
};
```

---

### 工厂函数

```cpp
std::shared_ptr<EventLoop> create_event_loop();
```
- **说明**：创建共享 EventLoop 实例。

---

## base/timer/ — 定时器

### Timer

```cpp
Timer();
~Timer();
```
- **说明**：构造/析构定时器管理器。

---

```cpp
int64_t add(int64_t after_ms, TimerCallback cb);
int64_t add_repeating(int64_t interval_ms, TimerCallback cb);
```
- **说明**：添加一次性/周期定时器。
- **参数**：`after_ms` - 毫秒后触发；`interval_ms` - 周期触发间隔。
- **返回值**：定时器 ID（>0），可传 `cancel()` 取消。
- **线程安全**：否（应在单线程使用）。

---

```cpp
bool cancel(int64_t timer_id);
void clear();
```
- **说明**：取消/清除定时器。

---

```cpp
int64_t poll(int64_t now_ms);
```
- **说明**：驱动定时器（从 EventLoop 或主循环定期调用）。
- **参数**：`now_ms` - 当前时间（毫秒）。
- **返回值**：距下一次到期的时间（毫秒），-1 表示无定时器。

---

```cpp
int64_t nearest_expire() const;
size_t size() const;
```
- **说明**：查询状态。

---

### TimerLoop（独立后台定时器）

```cpp
TimerLoop();
~TimerLoop();

void start();
void stop();

int64_t add(int64_t after_ms, TimerCallback cb);
int64_t add_repeating(int64_t interval_ms, TimerCallback cb);
bool    cancel(int64_t id);
```
- **说明**：独立运行的定时器线程（后台线程管理自己的事件循环）。
- **线程安全**：`add` / `cancel` 可从任意线程调用。

---

## base/message_queue/ — 消息队列

### MessageQueue

```cpp
MessageQueue();
~MessageQueue();
```
- **说明**：构造/析构消息队列。

---

```cpp
template<typename T>
void send(T&& msg);
```
- **说明**：发送消息（入队）。
- **线程安全**：是（多生产者安全）。
- **示例**：
```cpp
MessageQueue q;
q.send(42);               // 发送 int
q.send(std::string{"hi"}); // 发送 string
```

---

```cpp
template<typename T>
bool recv(T& out, int timeout_ms = -1);
```
- **说明**：接收消息（阻塞）。
- **参数**：`timeout_ms` - 超时毫秒，-1 表示无限等待。
- **返回值**：true 收到消息，false（超时或类型不匹配）。
- **线程安全**：是（多消费者安全）。

---

```cpp
template<typename T>
bool try_recv(T& out);
```
- **说明**：非阻塞尝试接收。
- **返回值**：true 收到消息，false 无消息。

---

```cpp
size_t size() const;
bool   empty() const;
void   clear();
```

---

### SubscribeHub

```cpp
template<typename T>
int subscribe(SubscribeCallback<T> cb);
```
- **说明**：订阅指定类型的消息。
- **返回值**：订阅 ID（用于取消订阅）。
- **线程安全**：是。
- **示例**：
```cpp
int id = hub.subscribe<LoginMsg>([](const LoginMsg& msg) {
    LOG_INFO("user login: %s", msg.username.c_str());
});
```

---

```cpp
template<typename T>
bool unsubscribe(int sub_id);
```
- **说明**：取消订阅。
- **返回值**：true 成功取消。

---

```cpp
template<typename T>
void publish(T&& msg);
```
- **说明**：发布消息（广播给所有订阅者）。
- **线程安全**：是。
- **示例**：
```cpp
hub.publish<LoginMsg>({"alice", "password123"});
```

---

```cpp
SubscribeHub* global_hub();
```
- **说明**：获取全局 SubscribeHub 实例。

---

## core/threadpool/ — 线程池

### ThreadPool

```cpp
explicit ThreadPool(int threads = 4, size_t queue_size = 0);
~ThreadPool();
```
- **说明**：构造线程池。
- **参数**：
  - `threads` - 工作线程数
  - `queue_size` - 队列最大长度（0=无界）

---

```cpp
template<typename F>
auto append(F&& f) -> std::future<decltype(f())>;
```
- **说明**：投递普通任务。
- **返回值**：`std::future` 可获取任务返回值。
- **线程安全**：是。
- **示例**：
```cpp
ThreadPool pool(4);
auto fut = pool.append([]{ return 42; });
int result = fut.get(); // result == 42

pool.append([]{
    LOG_INFO("async task done");
});
```

---

```cpp
void append_priority(TaskPtr task);
```
- **说明**：投递带优先级的任务。
- **参数**：`task` - `std::shared_ptr<Task>`
- **线程安全**：是。

---

```cpp
void shutdown();
void shutdown_now();
```
- **说明**：
  - `shutdown()` - 优雅停止，等待队列清空
  - `shutdown_now()` - 强制立即停止

---

```cpp
size_t queued_count() const;
int    worker_count() const;
```

---

## core/task/ — 任务封装

### Task

```cpp
explicit Task(Func f, TaskPriority pri = TaskPriority::NORMAL);
```
- **说明**：构造任务。
- **参数**：`f` - 任务函数，`pri` - 优先级。

---

```cpp
void execute();
void cancel();
bool is_cancelled() const;
TaskPriority priority() const;
```
- **说明**：任务操作。
- **线程安全**：`cancel()` / `is_cancelled()` 是原子的。

---

```cpp
using TaskPtr = std::shared_ptr<Task>;
```
- **说明**：任务智能指针类型。

---

### TaskPriority

```cpp
enum class TaskPriority : int {
    LOW    = 0,
    NORMAL = 1,
    HIGH   = 2
};
```

---

## core/monitor/ — 监控统计

### Monitor

```cpp
Monitor();
~Monitor();
```
- **说明**：构造/析构监控器。

---

```cpp
void on_connect();
void on_disconnect();
```
- **说明**：连接计数更新。
- **线程安全**：是。

---

```cpp
void set_task_queue_length(size_t len);
void set_threadpool_stats(int active, int total);
```
- **说明**：设置任务队列/线程池统计。

---

```cpp
void on_msg_sent(size_t bytes = 0);
void on_msg_recv(size_t bytes = 0);
void on_msg_error();
```
- **说明**：消息收发统计。

---

```cpp
void refresh();
MonitorStats snapshot() const;
```
- **说明**：刷新/获取统计快照。

---

```cpp
void start_logging(int interval_seconds = 60);
void stop_logging();
```
- **说明**：定时日志打点。

---

```cpp
void start_http_server(int port = 8080);
void stop_http_server();
```
- **说明**：HTTP 监控状态页（`:port/monitor` 返回 JSON）。

---

```cpp
std::string to_json() const;
```
- **说明**：获取 JSON 格式统计信息。

---

```cpp
Monitor* global_monitor();
```
- **说明**：获取全局 Monitor 实例。

---

### MonitorStats 结构

```cpp
struct MonitorStats {
    int64_t  timestamp{0};
    int      connections_alive{0};
    int      connections_total{0};
    int      connections_peak{0};
    size_t   task_queue_length{0};
    int      threadpool_active{0};
    int      threadpool_total{0};
    uint64_t msgs_sent{0};
    uint64_t msgs_recv{0};
    uint64_t msgs_error{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_recv{0};
    size_t   memory_rss_kb{0};
    size_t   memory_peak_rss_kb{0};
    uint64_t total_logs{0};
};
```

---

## network/tcp/ — TCP 网络

### TcpServer

```cpp
TcpServer(int port, int threads = 4, uint64_t idle_timeout_ms = 30000);
~TcpServer();
```
- **说明**：构造 TCP 服务器。
- **参数**：
  - `port` - 监听端口
  - `threads` - 工作线程数（EventLoop 线程）
  - `idle_timeout_ms` - 空闲超时（毫秒），超时断开

---

```cpp
bool start();
void stop();
```
- **说明**：启动/停止服务器。

---

```cpp
void set_message_handler(OnMessage fn);
void set_close_handler(OnClose fn);
void set_connected_handler(OnConnected fn);
```
- **说明**：设置业务回调。
- **示例**：
```cpp
server.set_message_handler([](int64_t sid, const protocol::Packet& pkt) {
    router.route(sid, pkt);
});
server.set_connected_handler([](int64_t sid) {
    LOG_INFO("client connected: %ld", sid);
});
```

---

```cpp
bool broadcast(uint16_t cmd, const std::string& data);
bool send_to(int64_t session_id, uint16_t cmd, const std::string& data);
void close_session(int64_t session_id);
size_t session_count() const;
bool is_running() const;
```
- **说明**：广播/单发/关闭会话。

---

```cpp
void set_heartbeat_interval(uint64_t ms);
void set_idle_timeout(uint64_t ms);
```
- **说明**：心跳/超时配置。

---

### TcpClient

```cpp
TcpClient(const std::string& host, int port);
~TcpClient();
```
- **说明**：构造 TCP 客户端。

---

```cpp
bool connect();
void disconnect();
```
- **说明**：连接/断开服务器。

---

```cpp
bool send(uint16_t cmd, const std::string& data);
bool send_raw(const std::string& data);
```
- **说明**：发送数据（自动编码协议头）。
- **返回值**：true 成功（数据已进入写队列）。

---

```cpp
void set_on_message(OnMessageCb fn);
void set_on_close(OnCloseCb fn);
void set_on_connected(OnConnCb fn);
```
- **说明**：设置回调。
- **回调类型**：
```cpp
using OnMessageCb = std::function<void(const std::string& data)>;
using OnCloseCb   = std::function<void()>;
using OnConnCb    = std::function<void()>;
```

---

```cpp
bool is_connected() const;
```

---

### TcpConnection

```cpp
TcpConnection(SocketHandle sock, int64_t session_id, TcpServer* server);
~TcpConnection();
```
- **说明**：构造连接（通常由 TcpServer 内部创建）。

---

```cpp
int64_t      session_id() const;
ConnStatus   status() const;
SocketHandle socket() const;
uint64_t     last_recv_time() const;
bool         is_idle(uint64_t timeout_ms) const;
```
- **说明**：查询连接状态。

---

```cpp
bool send_packet(uint16_t cmd, const std::string& data);
bool send_raw(const uint8_t* data, size_t len);
```
- **说明**：发送数据包。

---

```cpp
void start();
void close();
```
- **说明**：启动/关闭连接。

---

```cpp
void set_on_message(OnMessage fn);
void set_on_close(OnClose fn);
void set_on_connected(OnConnected fn);
```

---

### 连接回调类型

```cpp
using OnMessage   = std::function<void(int64_t session_id, const protocol::Packet&)>;
using OnClose     = std::function<void(int64_t session_id)>;
using OnConnected = std::function<void(int64_t session_id)>;
```

---

## network/codec/ — 协议编解码

### Codec

```cpp
Codec();
```
- **说明**：构造编解码器。

---

```cpp
using Handler = std::function<void(uint32_t seq, const uint8_t* data, size_t len)>;
void register_handler(uint16_t cmd, Handler h);
void unregister_handler(uint16_t cmd);
```
- **说明**：注册/注销命令字处理器。

---

```cpp
std::vector<uint8_t> encode(uint16_t cmd, const void* data, size_t len, uint32_t seq = 0);
```
- **说明**：编码（添加协议头）。
- **返回值**：编码后的字节数据（含 Length/Version/CRC32/Seq/Cmd/Data）。

---

```cpp
std::optional<Packet> decode(const uint8_t* buf, size_t len);
std::optional<Packet> decode_packet(const std::vector<uint8_t>& buf);
```
- **说明**：解码（流式，支持不完整数据累积）。
- **返回值**：解码成功返回 `Packet`，不完整返回 `std::nullopt`。

---

```cpp
const std::vector<uint8_t>& buffered() const;
void clear_buffer();
uint32_t compute_crc(const void* data, size_t len) const;
```

---

### CRC32

```cpp
class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t len);
    static bool     verify(const void* data, size_t len, uint32_t expected);
};
```

---

### ByteOrder

```cpp
class ByteOrder {
public:
    static uint16_t htons(uint16_t v);
    static uint16_t ntohs(uint16_t v);
    static uint32_t htonl(uint32_t v);
    static uint32_t ntohl(uint32_t v);
    static uint64_t htonll(uint64_t v);
    static uint64_t ntohll(uint64_t v);
    static bool     is_little_endian();
    static void     init();
};
```

---

### 常量

```cpp
constexpr uint8_t  PROTOCOL_VERSION = 1;
constexpr size_t   HEADER_SIZE = 15; // 4+1+4+4+2
constexpr size_t   MAX_PACKET_SIZE = 10 * 1024 * 1024; // 10MB
```

---

## network/protocol/ — 数据包协议

### namespace protocol

```cpp
constexpr uint16_t CMD_HEARTBEAT = 0x0001;
constexpr uint16_t CMD_LOGIN    = 0x0002;
constexpr uint16_t CMD_LOGOUT   = 0x0003;
constexpr uint16_t CMD_DATA     = 0x0004;
```

---

```cpp
struct Packet {
    uint16_t    cmd;
    std::string data;
    Packet() : cmd(0) {}
    Packet(uint16_t c, const std::string& d) : cmd(c), data(d) {}
};
```

---

```cpp
std::vector<uint8_t> encode(const Packet& pkt);
bool decode(const std::vector<uint8_t>& buf, Packet& pkt, size_t& consumed);
bool decode_from(const uint8_t* data, size_t len, Packet& pkt, size_t& consumed);
size_t peek_packet_length(const uint8_t* data, size_t len);
bool parse_header(const uint8_t* data, size_t len, uint32_t& out_length, uint16_t& out_cmd);
```

---

## business/session/ — 会话管理

### SessionMgr

```cpp
SessionMgr();
~SessionMgr();

bool init();
void shutdown();
```
- **说明**：构造/初始化/关闭会话管理器。

---

```cpp
int64_t add(int socket_fd, const std::string& ip, int port);
void    remove(int64_t sid);
bool    exists(int64_t sid) const;
```
- **说明**：添加/删除会话。

---

```cpp
size_t               count() const;
ConnectionContext*   get(int64_t sid);
const ConnectionContext* get(int64_t sid) const;
std::vector<int64_t> all_ids() const;
std::vector<ConnectionContext> alive_sessions() const;
```
- **说明**：查询会话。

---

```cpp
void heartbeat(int64_t sid);
bool kick(int64_t sid);
void mark_dead(int64_t sid);
```
- **说明**：心跳更新/踢人/标记死亡。

---

```cpp
void broadcast(const std::vector<uint8_t>& data, Broadcaster broadcaster);
bool send_to(int64_t sid, const uint8_t* data, size_t len);
size_t send_to_multiple(const std::vector<int64_t>& sids,
                        const uint8_t* data, size_t len);
```
- **说明**：广播/单发消息。

---

```cpp
size_t cleanup_timeout(uint64_t now_ms, uint64_t timeout_ms);
```
- **说明**：清理超时连接。
- **返回值**：清理的连接数量。

---

```cpp
Stats stats() const;
```
- **说明**：获取统计信息。

---

```cpp
SessionMgr* global_session_mgr();
```
- **说明**：获取全局 SessionMgr 实例。

---

### ConnectionContext

```cpp
struct ConnectionContext {
    int64_t         session_id{0};
    int             socket_fd{-1};
    std::string     remote_ip;
    int             remote_port{0};
    uint64_t        connect_time{0};
    uint64_t        last_msg_time{0};
    uint32_t        heartbeat_count{0};
    bool            is_alive{true};
    void*           user_data{nullptr};  // 业务扩展
};
```

---

### SessionMgr::Stats

```cpp
struct Stats {
    size_t   alive_count{0};
    size_t   total_count{0};
    size_t   peak_count{0};
};
```

---

### Broadcaster

```cpp
using Broadcaster = std::function<void(int64_t sid, const uint8_t* data, size_t len)>;
```

---

## business/router/ — 消息路由

### Router

```cpp
using Handler = std::function<void(int64_t session_id, const std::string& data)>;
```
- **说明**：业务处理函数类型。

---

```cpp
void reg(uint16_t cmd, Handler handler);
void unreg(uint16_t cmd);
```
- **说明**：注册/注销命令字路由。
- **线程安全**：`reg` 应在启动阶段调用（单线程）。

---

```cpp
void route(int64_t session_id, const protocol::Packet& pkt);
```
- **说明**：路由消息到对应 Handler。
- **线程安全**：是（内部加锁）。

---

```cpp
bool has(uint16_t cmd) const;
```
- **说明**：检查命令字是否有注册 Handler。

---

## base/process/ — 信号处理

### SignalHandler

```cpp
SignalHandler();
~SignalHandler();
```
- **说明**：构造/析构信号处理器。

---

```cpp
void on_signal(Signal sig, std::function<void(Signal)> cb);
```
- **说明**：注册信号回调（单个信号的处理函数）。

---

```cpp
using ShutdownCallback = std::function<void()>;
void on_shutdown(ShutdownCallback cb);
```
- **说明**：注册优雅退出回调（按注册顺序执行）。
- **示例**：
```cpp
sig.on_shutdown([](){ db.close(); });      // 先关闭数据库
sig.on_shutdown([](){ save_state(); });    // 再保存状态
sig.on_shutdown([](){ LOG_INFO("done"); }); // 最后打印日志
```

---

```cpp
void start();
void request_shutdown();
bool is_shutting_down() const;
void wait_for_shutdown();
void invoke_shutdown();
```
- **说明**：信号处理生命周期管理。

---

## db/ — 数据库连接池

### ConnectionPool

```cpp
ConnectionPool();
~ConnectionPool();
```
- **说明**：构造/析构连接池。

---

```cpp
bool init_mysql(const std::string& host, int port,
                const std::string& user, const std::string& password,
                const std::string& db, int pool_size = 4);
```
- **说明**：初始化 MySQL 连接池。
- **返回值**：true 成功。

---

```cpp
bool init_sqlite(const std::string& db_path, int pool_size = 2);
```
- **说明**：初始化 SQLite。

---

```cpp
void close();
```
- **说明**：关闭连接池。

---

```cpp
DBResult execute_sql(const std::string& sql);
```
- **说明**：同步执行 SQL（阻塞）。
- **返回值**：查询结果。

---

```cpp
void execute_sql_async(const std::string& sql, DBCallback cb);
```
- **说明**：异步执行 SQL（通过线程池）。
- **线程安全**：是。

---

```cpp
std::shared_ptr<DBConnection> get_connection();
```
- **说明**：获取原始连接（高级用户）。
- **返回值**：连接智能指针。

---

```cpp
void set_thread_pool(class ThreadPool* tp);
bool is_initialized() const;
```

---

### DBResult

```cpp
struct DBResult {
    bool        success{false};
    int         affected_rows{0};
    int64_t     insert_id{0};
    std::string error_msg;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string>             cols;
};
```

---

### DBCallback

```cpp
using DBCallback = std::function<void(const DBResult&)>;
```

---

### SQLTask / QueryTask

```cpp
class QueryTask {
public:
    QueryTask(ConnectionPool* pool, std::string sql, DBCallback cb);

    void run();        // 同步执行
    void run_async();  // 丢线程池异步执行
};
```
- **示例**：
```cpp
QueryTask task(&db, "SELECT * FROM users", [](const DBResult& r) {
    if (r.success) { /* 处理 rows */ }
});
task.run_async();
```

---

## 附录：常用类型速查

| 类型 | 定义 |
|------|------|
| `framework::Logger*` | 单例：`Logger::instance()` |
| `framework::Mutex` | `std::mutex` 的别名 |
| `framework::LockGuard<Mutex>` | RAII 锁守卫 |
| `framework::SocketHandle` | Linux: `int`, Windows: `SOCKET` |
| `framework::TaskPtr` | `std::shared_ptr<Task>` |
| `framework::OnMessage` | `std::function<void(int64_t, const protocol::Packet&)>` |
| `framework::OnClose` | `std::function<void(int64_t)>` |
| `framework::OnConnected` | `std::function<void(int64_t)>` |
| `framework::DBCallback` | `std::function<void(const DBResult&)>` |
| `framework::Broadcaster` | `std::function<void(int64_t, const uint8_t*, size_t)>` |

---

*文档版本: 1.0 | 更新日期: 2026-05-19*