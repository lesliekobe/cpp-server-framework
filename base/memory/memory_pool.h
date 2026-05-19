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

namespace framework {

class MemoryPool {
public:
    // item_size: 每个内存块大小，pre_alloc: 预分配块数
    explicit MemoryPool(size_t item_size, size_t pre_alloc = 64);
    ~MemoryPool();

    // 分配 / 释放
    void* allocate();
    void  deallocate(void* ptr);

    size_t item_size() const { return item_size_; }
    size_t total_allocated() const { return free_list_.size() + allocated_; }
    size_t allocated() const { return allocated_; }

private:
    struct Block {
        Block* next;
        char   data[64]; // 固定 64 字节，可按需调整
    };

    size_t       item_size_;
    size_t       allocated_{ 0 };
    void*        free_list_{ nullptr };  // 链表头
    std::mutex   mtx_;
    std::vector<Block*> all_blocks_;     // 用于析构释放
};

} // namespace framework