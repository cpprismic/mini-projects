#include <utility>

template <typename T>
class UniquePtr {
public:
    // ---- Конструкторы ----

    UniquePtr() = default;

    explicit UniquePtr(T* ptr) noexcept
        : ptr_(ptr) {}

    // Копирование запрещено
    UniquePtr(const UniquePtr&)            = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    // Move
    UniquePtr(UniquePtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)) {}

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    // ---- Деструктор ----

    ~UniquePtr() {
        reset();
    }

    // ---- Observers ----

    T* get() const noexcept { return ptr_; }

    // Explicit чтобы не было неявного приведения к int
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T& operator*()  const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    // ---- Modifiers ----

    // release — отдаём владение, сами обнуляемся
    [[nodiscard]] T* release() noexcept {
        return std::exchange(ptr_, nullptr);
    }

    // reset — удаляем текущий объект, захватываем новый
    void reset(T* ptr = nullptr) noexcept {
        T* old = std::exchange(ptr_, ptr);
        delete old;
    }

    void swap(UniquePtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }

private:
    T* ptr_ = nullptr;
};

// make_unique — чтобы не писать new явно
template <typename T, typename... Args>
UniquePtr<T> make_unique(Args&&... args) {
    return UniquePtr<T>(new T(std::forward<Args>(args)...));
}