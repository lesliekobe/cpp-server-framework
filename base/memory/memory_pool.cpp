/*
 * memory_pool.cpp
 */
#include "memory_pool.h"
#include <cstdlib>
#include <cstring>

namespace framework {

MemoryPool::MemoryPool(size_t item_size, size_t pre_alloc)
    : item_size_(item_size)
{
    for (size_t i = 0; i < pre_alloc; ++i) {
        // 使用 malloc 分配内存块（item_size 决定实际大小）
        // 这里固定用 64B 块，实际可改造为模板参数
        void* ptr = std::malloc(item_size > sizeof(void*) ? item_size : sizeof(void*));
        if (!ptr) break;
        // 插入 free list 头部
        *(void**)ptr = free_list_;
        free_list_ = ptr;
        allocated_++;
    }
}

MemoryPool::~MemoryPool() {
    // 释放所有分配的块
    for (Block* b : all_blocks_) {
        std::free(b);
    }
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (free_list_) {
        void* ptr = free_list_;
        free_list_ = *(void**)free_list_;
        allocated_--;
        return ptr;
    }
    // 池空，直接 malloc
    void* ptr = std::malloc(item_size_ > sizeof(void*) ? item_size_ : sizeof(void*));
    return ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(mtx_);
    *(void**)ptr = free_list_;
    free_list_ = ptr;
    allocated_++;
}

} // namespace framework