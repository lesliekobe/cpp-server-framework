/*
 * memory_pool.h - 固定大小内存池
 *
 * 原理：预先分配固定大小内存块链表，用时直接从链表取，
 *       避免频繁 new/delete 造成的内存碎片。
 * 线程安全，支持并发分配/释放。
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>
#include <atomic>

namespace framework {

// ============ 内存块 ============
struct MemBlock {
    MemBlock* next;
    char      data[1]; // 实际按 item_size 分配
};

// ============ 固定大小内存池 ============
class MemoryPool {
public:
    // item_size: 每个内存块大小（字节）
    // pre_alloc: 预分配块数
    // block_size: 每次扩容块数
    explicit MemoryPool(size_t item_size, size_t pre_alloc = 64, size_t block_size = 32);
    ~MemoryPool();

    // 禁止拷贝
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // 分配（从池中取）
    void* allocate();

    // 释放（归还到池）
    void  deallocate(void* ptr);

    // 预热池
    void  pre_allocate(size_t count);

    // 池信息
    size_t item_size()   const { return item_size_; }
    size_t total_blocks() const { return total_blocks_.load(std::memory_order_relaxed); }
    size_t free_blocks()  const { return free_blocks_.load(std::memory_order_relaxed); }
    size_t used_blocks()  const { return total_blocks_.load(std::memory_order_relaxed) - free_blocks_.load(std::memory_order_relaxed); }

private:
    size_t       item_size_;
    size_t       block_size_;
    void*        free_list_{nullptr};        // 链表头
    std::mutex   mtx_;
    std::atomic<size_t> total_blocks_{0};
    std::atomic<size_t> free_blocks_{0};
    std::vector<void*>  all_chunks_;         // 用于析构释放
};

// ============ 多规格内存池管理器 ============
class MemoryPoolManager {
public:
    static MemoryPoolManager& instance();

    // 获取指定大小的池（按 2^n 对齐）
    MemoryPool* get_pool(size_t size);

    // 预分配多个规格的池
    void pre_alloc();

    // 统计
    struct Stats {
        size_t total_blocks{0};
        size_t free_blocks{0};
        size_t used_blocks{0};
    };
    Stats stats() const;

private:
    MemoryPoolManager() = default;
    struct PoolEntry {
        size_t               size;
        std::unique_ptr<MemoryPool> pool;
    };
    std::vector<PoolEntry> pools_;
    std::mutex             mtx_;
};

} // namespace framework