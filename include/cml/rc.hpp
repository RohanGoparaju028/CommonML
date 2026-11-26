#pragma once

#include <atomic>
#include <cstddef>
#include <utility>

namespace cml {

struct RcStats {
    static std::atomic<size_t>& live_objects_counter() {
        static std::atomic<size_t> c{0};
        return c;
    }
    static std::atomic<size_t>& total_allocs_counter() {
        static std::atomic<size_t> c{0};
        return c;
    }
    static std::atomic<size_t>& total_frees_counter() {
        static std::atomic<size_t> c{0};
        return c;
    }

    static size_t live_objects() { return live_objects_counter().load(std::memory_order_relaxed); }
    static size_t total_allocs() { return total_allocs_counter().load(std::memory_order_relaxed); }
    static size_t total_frees() { return total_frees_counter().load(std::memory_order_relaxed); }
};

class RcObject {
public:
    RcObject() noexcept {
        RcStats::live_objects_counter().fetch_add(1, std::memory_order_relaxed);
        RcStats::total_allocs_counter().fetch_add(1, std::memory_order_relaxed);
    }
    virtual ~RcObject() noexcept {
        RcStats::live_objects_counter().fetch_sub(1, std::memory_order_relaxed);
        RcStats::total_frees_counter().fetch_add(1, std::memory_order_relaxed);
    }

    RcObject(const RcObject&) = delete;
    RcObject& operator=(const RcObject&) = delete;

    void retain() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void release() const noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
    size_t ref_count() const noexcept { return ref_count_.load(std::memory_order_relaxed); }

private:
    mutable std::atomic<size_t> ref_count_{0};
};

template <typename T>
class Rc {
public:
    Rc() noexcept = default;
    Rc(std::nullptr_t) noexcept {}
    explicit Rc(T* p) noexcept : ptr_(p) {
        if (ptr_) ptr_->retain();
    }

    Rc(const Rc& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->retain();
    }

    Rc(Rc&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    ~Rc() {
        if (ptr_) ptr_->release();
    }

    Rc& operator=(const Rc& other) noexcept {
        if (this != &other) {
            if (other.ptr_) other.ptr_->retain();
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
        }
        return *this;
    }

    Rc& operator=(Rc&& other) noexcept {
        if (this != &other) {
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    size_t use_count() const noexcept { return ptr_ ? ptr_->ref_count() : 0; }

    void reset(T* p = nullptr) noexcept {
        if (p) p->retain();
        if (ptr_) ptr_->release();
        ptr_ = p;
    }

    bool operator==(const Rc& other) const noexcept { return ptr_ == other.ptr_; }
    bool operator!=(const Rc& other) const noexcept { return ptr_ != other.ptr_; }

private:
    T* ptr_ = nullptr;
};

template <typename T, typename... Args>
Rc<T> make_rc(Args&&... args) {
    return Rc<T>(new T(std::forward<Args>(args)...));
}

}  // namespace cml
