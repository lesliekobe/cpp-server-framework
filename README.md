# C++ Industrial Server Framework

**C++ 工业级服务端框架** — 跨平台（Linux/Windows），可直接落地用于网关、服务端、工控后台、嵌入式后台。

集成：TCP/IP 网络通信 + 多线程 + 线程池 + 并发任务调度 + 事件驱动 + 内存池

---

## 架构分层

```
┌─────────────────────────────────────┐
│  应用业务层                          │
│  业务协议解析 | 会话管理 | 消息路由    │
├─────────────────────────────────────┤
│  网络通信层                          │
│  TCP服务端/客户端 | 粘包分包 | 心跳    │
├─────────────────────────────────────┤
│  并发调度层                          │
│  线程池 | 任务队列 | 异步任务          │
├─────────────────────────────────────┤
│  基础组件层                          │
│  互斥锁 | 原子操作 | 内存池 | 日志    │
├─────────────────────────────────────┤
│  系统适配层                          │
│  Linux/Windows 跨平台封装            │
└─────────────────────────────────────┘
```

---

## 目录结构

```
cpp-server-framework/
├── base/                    # 基础组件层
│   ├── logger/              # 日志系统（分级+滚动）
│   ├── time/                # 时间工具
│   ├── lock/                # 互斥锁、条件变量
│   └── memory/              # 内存池
├── core/                    # 并发调度层
│   ├── threadpool/          # 线程池
│   └── task/                # 任务封装
├── network/                 # 网络通信层
│   ├── tcp/                 # TCP 服务端/客户端/连接
│   └── protocol/            # 数据包协议
├── business/                # 应用业务层
│   ├── session/             # 会话管理
│   └── router/              # 消息路由
├── adapter/                 # 系统适配层
├── example/                 # 使用示例
│   ├── server/              # 服务端示例
│   └── gateway/             # 网关示例
├── test/                    # 单元测试
└── CMakeLists.txt
```

---

## 核心模块

### 1. 基础工具模块

| 模块 | 说明 |
|------|------|
| **logger** | 分级日志（DEBUG/INFO/WARN/ERROR），按大小滚动 |
| **timestamp** | 时间戳获取、格式化、时区转换 |
| **mutex** | Mutex 互斥锁封装（RAII 风格） |
| **condition** | 条件变量封装 |
| **memory_pool** | 固定大小内存池，减少 new/delete 碎片 |

### 2. 线程池模块

- 固定大小工作线程
- 阻塞任务队列（无界/有界可选）
- 支持 `std::function<void()>` 任意任务
- 支持优雅停止（等队列清空）
- 有锁/无锁双模式可选（默认有锁）

### 3. 网络通信模块

- **TcpServer**: Epoll（Linux）/ Select（Windows）事件驱动
- **TcpClient**: 自动重连
- **TcpConnection**: 每连接独立读写，线程安全
- **Packet**: 自定义协议头（4字节长度 + 2字节命令 + 数据）
- **拆包/防粘包**: 基于长度域的完整分包
- **心跳检测**: 空闲超时断开

### 4. 会话管理

- 在线客户端列表
- SessionID → Connection 映射
- 消息广播、单点下发

---

## 快速开始

### 编译

```bash
mkdir build && cd build
cmake .. && make -j4
```

### 运行示例

```bash
# TCP 服务端
./tcp_server_example 8080

# 网关示例
./gateway_example 8888
```

### 编写业务

```cpp
#include "network/tcp/tcp_server.h"
#include "core/threadpool/thread_pool.h"
#include "base/logger/logger.h"

int main() {
    // 初始化日志
    Logger::instance()->init("server.log", LogLevel::INFO);

    // 创建线程池
    ThreadPool pool(4);

    // 启动 TCP 服务端
    TcpServer server(8080);
    server.set_message_handler([&pool](int64_t session_id, const std::string& data) {
        // 业务逻辑投递到线程池异步执行
        pool.append([session_id, data]() {
            LOG_INFO("处理会话 %d, 数据长度: %zu", session_id, data.size());
            // ... 业务处理
        });
    });
    server.start();

    std::this_thread::sleep_for(std::chrono::seconds(3600));
    return 0;
}
```

---

## 数据包协议

```
+-------------+-----------+-------------+
| Length(4B)  | Cmd(2B)   | Data(N B)   |
+-------------+-----------+-------------+
```

- `Length`: 整个包字节数（包含头部，4字节大端序）
- `Cmd`: 命令字（2字节）
- `Data`: 业务数据

---

## 依赖

- C++17 编译器
- CMake 3.20+
- Linux: glibc / macOS: libpthread
- Windows: WinSock2（已封装，跨平台透明）