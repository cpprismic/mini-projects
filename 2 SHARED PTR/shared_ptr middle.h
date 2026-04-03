#include <utility>
#include <cstddef>

template <typename T>
class SharedPtr {
public:
    // ---- Конструкторы ----

    SharedPtr() = default;

    explicit SharedPtr(T* ptr)
        : ptr_(ptr)
        , block_(ptr ? new ControlBlock() : nullptr) {}

    // Копирование — разделяем владение, увеличиваем счётчик
    SharedPtr(const SharedPtr& other) noexcept
        : ptr_(other.ptr_)
        , block_(other.block_) {
        inc_ref();
    }

    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (this != &other) {
            // Сначала привязываемся к новому ресурсу,
            // потом отпускаем старый — порядок важен
            SharedPtr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // Move — забираем владение без изменения счётчика
    SharedPtr(SharedPtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , block_(std::exchange(other.block_, nullptr)) {}

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            SharedPtr tmp(std::move(other));
            swap(tmp);
        }
        return *this;
    }

    ~SharedPtr() {
        dec_ref();
    }

    // ---- Observers ----

    T* get()                    const noexcept { return ptr_; }
    T& operator*()              const noexcept { return *ptr_; }
    T* operator->()             const noexcept { return ptr_; }
    explicit operator bool()    const noexcept { return ptr_ != nullptr; }
    size_t use_count()          const noexcept { return block_ ? block_->count : 0; }

    void swap(SharedPtr& other) noexcept {
        std::swap(ptr_,   other.ptr_);
        std::swap(block_, other.block_);
    }

    void reset(T* ptr = nullptr) {
        SharedPtr tmp(ptr);
        swap(tmp);
    }

private:
    struct ControlBlock {
        size_t count = 1;
    };

    void inc_ref() noexcept {
        if (block_) ++block_->count;
    }

    void dec_ref() noexcept {
        if (!block_) return;
        if (--block_->count == 0) {
            delete ptr_;
            delete block_;
        }
    }

    T*            ptr_   = nullptr;
    ControlBlock* block_ = nullptr;
};

template <typename T, typename... Args>
SharedPtr<T> make_shared(Args&&... args) {
    return SharedPtr<T>(new T(std::forward<Args>(args)...));
}