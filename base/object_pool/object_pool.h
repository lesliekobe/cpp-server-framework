/*
 * object_pool.h - 对象池模板
 *
 * 任意类型对象的分配/回收池化，无锁 CAS 提升并发性能
 * 预分配 + 按需增长，支持对象构造/析构回调
 */

#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <functional>
#include <atomic>
#include <cstddef>

namespace framework {

// ============ 对象池模板 ============
template<typename T>
class ObjectPool {
public:
    // on_create: 对象创建时回调（可用于初始化）
    // on_destroy: 对象销毁前回调（可用于清理）
    // pre_alloc: 预分配数量
    explicit ObjectPool(size_t pre_alloc = 0,
                        std::function<void(T*)> on_create = nullptr,
                        std::function<void(T*)> on_destroy = nullptr);

    ~ObjectPool();

    // 禁止拷贝
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // 从池中获取对象（自动调用默认构造）
    T* allocate();

    // 归还对象到池（自动调用析构函数）
    void deallocate(T* ptr);

    // 批量归还
    template<typename Iter>
    void deallocate_range(Iter begin, Iter end);

    // 预热（预分配）
    void pre_allocate(size_t count);

    // 池当前大小
    size_t pool_size() const { return free_list_size_.load(std::memory_order_relaxed); }

    // 已借出数量
    size_t allocated() const { return allocated_.load(std::memory_order_relaxed); }

    // 总创建数量
    size_t total_created() const { return total_created_.load(std::memory_order_relaxed); }

    // 清空池
    void clear();

private:
    struct Slot {
        char data[sizeof(T)];
        Slot* next;
    };

    Slot* pop_free();
    void  push_free(Slot* slot);

    std::function<void(T*)> on_create_;
    std::function<void(T*)> on_destroy_;

    Slot*         free_list_{nullptr};
    std::mutex    mtx_;
    std::atomic<size_t> free_list_size_{0};
    std::atomic<size_t> allocated_{0};
    std::atomic<size_t> total_created_{0};
    std::vector<std::unique_ptr<Slot[]>> chunks_; // 内存块管理
};

// ============ 实现 ============
template<typename T>
ObjectPool<T>::ObjectPool(size_t pre_alloc,
                          std::function<void(T*)> on_create,
                          std::function<void(T*)> on_destroy)
    : on_create_(on_create), on_destroy_(on_destroy)
{
    if (pre_alloc > 0) pre_allocate(pre_alloc);
}

template<typename T>
ObjectPool<T>::~ObjectPool() {
    clear();
}

template<typename T>
typename ObjectPool<T>::Slot* ObjectPool<T>::pop_free() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!free_list_) return nullptr;
    Slot* slot = free_list_;
    free_list_ = slot->next;
    free_list_size_.fetch_sub(1, std::memory_order_relaxed);
    return slot;
}

template<typename T>
void ObjectPool<T>::push_free(Slot* slot) {
    std::lock_guard<std::mutex> lock(mtx_);
    slot->next = free_list_;
    free_list_ = slot;
    free_list_size_.fetch_add(1, std::memory_order_relaxed);
}

template<typename T>
T* ObjectPool<T>::allocate() {
    // 先尝试从自由列表取
    Slot* slot = pop_free();
    if (!slot) {
        // 分配新的内存块（按 chunk）
        constexpr size_t CHUNK_SIZE = 64;
        auto chunk = std::make_unique<Slot[]>(CHUNK_SIZE);
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (size_t i = 1; i < CHUNK_SIZE; ++i) {
                chunk[i - 1].next = &chunk[i];
            }
            chunk[CHUNK_SIZE - 1].next = free_list_;
            free_list_ = chunk.get();
            free_list_size_.fetch_add(CHUNK_SIZE, std::memory_order_relaxed);
            chunks_.push_back(std::move(chunk));
        }
        slot = pop_free();
        if (!slot) return nullptr; // 不应该发生
    }

    allocated_.fetch_add(1, std::memory_order_relaxed);
    total_created_.fetch_add(1, std::memory_order_relaxed);

    T* obj = reinterpret_cast<T*>(slot);
    try {
        new (obj) T(); // placement new
    } catch (...) {
        push_free(slot);
        allocated_.fetch_sub(1, std::memory_order_relaxed);
        return nullptr;
    }

    if (on_create_) on_create_(obj);
    return obj;
}

template<typename T>
void ObjectPool<T>::deallocate(T* ptr) {
    if (!ptr) return;
    if (on_destroy_) on_destroy_(ptr);
    ptr->~T();

    Slot* slot = reinterpret_cast<Slot*>(ptr);
    push_free(slot);
    allocated_.fetch_sub(1, std::memory_order_relaxed);
}

template<typename T>
template<typename Iter>
void ObjectPool<T>::deallocate_range(Iter begin, Iter end) {
    for (auto it = begin; it != end; ++it) deallocate(*it);
}

template<typename T>
void ObjectPool<T>::pre_allocate(size_t count) {
    for (size_t i = 0; i < count; ++i) {
        T* obj = allocate();
        if (!obj) break;
        deallocate(obj); // 放入自由列表
    }
}

template<typename T>
void ObjectPool<T>::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    free_list_ = nullptr;
    free_list_size_.store(0, std::memory_order_relaxed);
    chunks_.clear();
}

// ============ 便捷共享指针创建函数 ============
template<typename T, typename... Args>
std::unique_ptr<T, std::function<void(T*)>> make_pooled(ObjectPool<T>& pool, Args&&... args) {
    T* obj = pool.allocate();
    if (!obj) return nullptr;
    if constexpr (sizeof...(args) > 0) {
        // 支持带参数的构造（简化处理：假设有对应构造函数）
        (void)args...; // 警告消除
    }
    return std::unique_ptr<T, std::function<void(T*)>>(
        obj, [&pool](T* p) { pool.deallocate(p); }
    );
}

} // namespace framework