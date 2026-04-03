#include <atomic>
#include <utility>
#include <type_traits>
#include <cstddef>

// ---- Control block ----

// Базовый класс control block — позволяет хранить
// произвольный делетер без шаблонного параметра в SharedPtr
struct ControlBlockBase {
    std::atomic<size_t> shared_count{1};
    std::atomic<size_t> weak_count{1}; // +1 пока shared_count > 0

    virtual void destroy_object() noexcept = 0; // удаляет T
    virtual void destroy_self()   noexcept = 0; // удаляет сам блок
    virtual ~ControlBlockBase() = default;
};

// Control block с указателем и кастомным делетером
template <typename T, typename Deleter>
struct ControlBlockPtr : ControlBlockBase {
    T*      ptr;
    Deleter deleter;

    ControlBlockPtr(T* p, Deleter d)
        : ptr(p), deleter(std::move(d)) {}

    void destroy_object() noexcept override { deleter(ptr); }
    void destroy_self()   noexcept override { delete this; }
};

// Control block с инлайн-хранением объекта — используется в make_shared
// Одна аллокация на объект + control block вместо двух
template <typename T>
struct ControlBlockInplace : ControlBlockBase {
    alignas(T) unsigned char storage[sizeof(T)];

    template <typename... Args>
    explicit ControlBlockInplace(Args&&... args) {
        new (storage) T(std::forward<Args>(args)...);
    }

    T* get() noexcept { return reinterpret_cast<T*>(storage); }

    void destroy_object() noexcept override { get()->~T(); }
    void destroy_self()   noexcept override { delete this; }
};

// ---- WeakPtr forward declaration ----
template <typename T>
class WeakPtr;

// ---- SharedPtr ----

template <typename T>
class SharedPtr {
public:
    using element_type = T;

    // ---- Конструкторы ----

    constexpr SharedPtr() noexcept = default;
    constexpr SharedPtr(std::nullptr_t) noexcept {}

    template <typename Y,
              typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
    explicit SharedPtr(Y* ptr)
        : ptr_(ptr)
        , block_(new ControlBlockPtr<Y, void(*)(Y*)>(ptr, [](Y* p){ delete p; })) {}

    // С кастомным делетером
    template <typename Y, typename Deleter>
    SharedPtr(Y* ptr, Deleter d)
        : ptr_(ptr)
        , block_(new ControlBlockPtr<Y, Deleter>(ptr, std::move(d))) {}

    // Копирование
    SharedPtr(const SharedPtr& other) noexcept
        : ptr_(other.ptr_)
        , block_(other.block_) {
        inc_shared();
    }

    // Конвертирующее копирование Derived -> Base
    template <typename Y,
              typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
    SharedPtr(const SharedPtr<Y>& other) noexcept
        : ptr_(other.ptr_)
        , block_(other.block_) {
        inc_shared();
    }

    SharedPtr& operator=(const SharedPtr& other) noexcept {
        SharedPtr tmp(other);
        swap(tmp);
        return *this;
    }

    // Move
    SharedPtr(SharedPtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , block_(std::exchange(other.block_, nullptr)) {}

    template <typename Y,
              typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
    SharedPtr(SharedPtr<Y>&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , block_(std::exchange(other.block_, nullptr)) {}

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        SharedPtr tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    // Конструктор из WeakPtr — бросает если объект уже умер
    explicit SharedPtr(const WeakPtr<T>& weak);

    ~SharedPtr() { dec_shared(); }

    // ---- Observers ----

    T*      get()                 const noexcept { return ptr_; }
    T&      operator*()           const noexcept { return *ptr_; }
    T*      operator->()          const noexcept { return ptr_; }
    size_t  use_count()           const noexcept {
        return block_ ? block_->shared_count.load(std::memory_order_relaxed) : 0;
    }
    bool    unique()              const noexcept { return use_count() == 1; }
    explicit operator bool()      const noexcept { return ptr_ != nullptr; }

    void swap(SharedPtr& other) noexcept {
        std::swap(ptr_,   other.ptr_);
        std::swap(block_, other.block_);
    }

    void reset() noexcept {
        SharedPtr().swap(*this);
    }

    template <typename Y>
    void reset(Y* ptr) {
        SharedPtr(ptr).swap(*this);
    }

private:
    void inc_shared() noexcept {
        if (block_)
            block_->shared_count.fetch_add(1, std::memory_order_relaxed);
    }

    void dec_shared() noexcept {
        if (!block_) return;
        if (block_->shared_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            block_->destroy_object();
            // Уменьшаем weak_count — он держался пока shared_count > 0
            if (block_->weak_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                block_->destroy_self();
            }
        }
    }

    T*                ptr_   = nullptr;
    ControlBlockBase* block_ = nullptr;

    template <typename U> friend class SharedPtr;
    template <typename U> friend class WeakPtr;
    template <typename U, typename... Args>
    friend SharedPtr<U> make_shared(Args&&...);
};

// ---- WeakPtr ----

template <typename T>
class WeakPtr {
public:
    WeakPtr() = default;

    WeakPtr(const SharedPtr<T>& shared) noexcept
        : ptr_(shared.ptr_)
        , block_(shared.block_) {
        inc_weak();
    }

    WeakPtr(const WeakPtr& other) noexcept
        : ptr_(other.ptr_)
        , block_(other.block_) {
        inc_weak();
    }

    WeakPtr(WeakPtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , block_(std::exchange(other.block_, nullptr)) {}

    WeakPtr& operator=(const WeakPtr& other) noexcept {
        WeakPtr tmp(other);
        swap(tmp);
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& other) noexcept {
        WeakPtr tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    ~WeakPtr() { dec_weak(); }

    // lock — безопасно получаем SharedPtr, или пустой если объект умер
    SharedPtr<T> lock() const noexcept {
        if (!block_) return {};
        // Атомарно проверяем что shared_count > 0 и увеличиваем
        size_t count = block_->shared_count.load(std::memory_order_relaxed);
        while (count > 0) {
            if (block_->shared_count.compare_exchange_weak(
                    count, count + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                SharedPtr<T> result;
                result.ptr_   = ptr_;
                result.block_ = block_;
                return result;
            }
        }
        return {};
    }

    bool    expired()   const noexcept { return use_count() == 0; }
    size_t  use_count() const noexcept {
        return block_ ? block_->shared_count.load(std::memory_order_relaxed) : 0;
    }

    void swap(WeakPtr& other) noexcept {
        std::swap(ptr_,   other.ptr_);
        std::swap(block_, other.block_);
    }

private:
    void inc_weak() noexcept {
        if (block_)
            block_->weak_count.fetch_add(1, std::memory_order_relaxed);
    }

    void dec_weak() noexcept {
        if (!block_) return;
        if (block_->weak_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            block_->destroy_self();
        }
    }

    T*                ptr_   = nullptr;
    ControlBlockBase* block_ = nullptr;

    friend class SharedPtr<T>;
};

// SharedPtr(WeakPtr) — реализация после определения WeakPtr
template <typename T>
SharedPtr<T>::SharedPtr(const WeakPtr<T>& weak) {
    *this = weak.lock();
    if (!ptr_) throw std::bad_weak_ptr{};
}

// ---- make_shared — одна аллокация ----
template <typename T, typename... Args>
SharedPtr<T> make_shared(Args&&... args) {
    auto* block = new ControlBlockInplace<T>(std::forward<Args>(args)...);
    SharedPtr<T> result;
    result.ptr_   = block->get();
    result.block_ = block;
    return result;
}