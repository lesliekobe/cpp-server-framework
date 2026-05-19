/*
 * memory_pool.cpp - 固定大小内存池实现
 */
#include "base/memory/memory_pool.h"
#include "logger/logger.h"
#include <cstdlib>
#include <cstring>

namespace framework {

// ============ MemoryPool ============
MemoryPool::MemoryPool(size_t item_size, size_t pre_alloc, size_t block_size)
    : item_size_(item_size), block_size_(block_size)
{
    if (item_size_ < sizeof(void*)) item_size_ = sizeof(void*);
    // 按 block_size 批量预分配
    pre_allocate(pre_alloc);
}

MemoryPool::~MemoryPool() {
    for (void* chunk : all_chunks_) {
        std::free(chunk);
    }
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mtx_);

    if (free_list_) {
        void* ptr = free_list_;
        free_list_ = *(void**)free_list_;
        free_blocks_.fetch_sub(1, std::memory_order_relaxed);
        // 清零
        std::memset(ptr, 0, item_size_);
        return ptr;
    }

    // 池空，扩容
    size_t chunk_size = item_size_ * block_size_;
    void* chunk = std::malloc(chunk_size);
    if (!chunk) {
        LOG_ERROR("MemoryPool: malloc failed size=%zu", chunk_size);
        return nullptr;
    }
    all_chunks_.push_back(chunk);
    total_blocks_.fetch_add(block_size_, std::memory_order_relaxed);
    free_blocks_.fetch_add(block_size_, std::memory_order_relaxed);

    // 第一个返回，其余放入自由链表
    char* base = (char*)chunk;
    free_list_ = base + item_size_;
    for (size_t i = 1; i < block_size_ - 1; ++i) {
        *(void**)(base + i * item_size_) = base + (i + 1) * item_size_;
    }
    *(void**)(base + (block_size_ - 1) * item_size_) = nullptr;

    // 返回第一个块
    std::memset(base, 0, item_size_);
    free_blocks_.fetch_sub(1, std::memory_order_relaxed);
    return base;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(mtx_);
    *(void**)ptr = free_list_;
    free_list_ = ptr;
    free_blocks_.fetch_add(1, std::memory_order_relaxed);
}

void MemoryPool::pre_allocate(size_t count) {
    size_t allocated = 0;
    while (allocated < count) {
        size_t batch = std::min(count - allocated, block_size_);
        size_t chunk_size = item_size_ * batch;
        void* chunk = std::malloc(chunk_size);
        if (!chunk) break;
        all_chunks_.push_back(chunk);
        total_blocks_.fetch_add(batch, std::memory_order_relaxed);
        free_blocks_.fetch_add(batch, std::memory_order_relaxed);

        char* base = (char*)chunk;
        for (size_t i = 0; i < batch - 1; ++i) {
            *(void**)(base + i * item_size_) = base + (i + 1) * item_size_;
        }
        *(void**)(base + (batch - 1) * item_size_) = free_list_;
        free_list_ = base;
        allocated += batch;
    }
}

// ============ MemoryPoolManager ============
MemoryPoolManager& MemoryPoolManager::instance() {
    static MemoryPoolManager mgr;
    return mgr;
}

MemoryPool* MemoryPoolManager::get_pool(size_t size) {
    // 向上取整到 2^n
    size_t alloc_size = 64;
    while (alloc_size < size) alloc_size *= 2;

    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& e : pools_) {
        if (e.size == alloc_size) return e.pool.get();
    }

    auto pool = std::make_unique<MemoryPool>(alloc_size, 64, 64);
    MemoryPool* ret = pool.get();
    pools_.push_back({alloc_size, std::move(pool)});
    LOG_INFO("MemoryPoolManager: created pool size=%zu", alloc_size);
    return ret;
}

void MemoryPoolManager::pre_alloc() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (size_t sz : {64, 128, 256, 512, 1024, 2048, 4096, 8192}) {
        auto pool = std::make_unique<MemoryPool>(sz, 32, 32);
        pools_.push_back({sz, std::move(pool)});
    }
    LOG_INFO("MemoryPoolManager: pre-allocated");
}

MemoryPoolManager::Stats MemoryPoolManager::stats() const {
    Stats s;
    for (auto& e : pools_) {
        s.total_blocks += e.pool->total_blocks();
        s.free_blocks  += e.pool->free_blocks();
    }
    s.used_blocks = s.total_blocks - s.free_blocks;
    return s;
}

} // namespace framework