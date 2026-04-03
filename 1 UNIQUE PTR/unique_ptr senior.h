#include <utility>
#include <type_traits>

// Дефолтный делетер — просто вызывает delete
// Вынесен отдельно чтобы UniquePtr поддерживал кастомные делетеры
template <typename T>
struct DefaultDeleter {
    void operator()(T* ptr) const noexcept {
        // static_assert чтобы поймать удаление неполного типа на этапе компиляции
        static_assert(sizeof(T) > 0, "Cannot delete incomplete type");
        delete ptr;
    }
};

// Специализация для массивов
template <typename T>
struct DefaultDeleter<T[]> {
    void operator()(T* ptr) const noexcept {
        static_assert(sizeof(T) > 0, "Cannot delete incomplete type");
        delete[] ptr;
    }
};

// Кастомный делетер передаётся как второй шаблонный параметр
// Это позволяет использовать UniquePtr с FILE*, mmap, и т.д.
// Пример: UniquePtr<FILE, FileDeleter> file(fopen("f.txt", "r"));
template <typename T, typename Deleter = DefaultDeleter<T>>
class UniquePtr {
public:
    using pointer      = T*;
    using element_type = T;
    using deleter_type = Deleter;

    // ---- Конструкторы ----

    constexpr UniquePtr() noexcept = default;
    constexpr UniquePtr(std::nullptr_t) noexcept {}

    explicit UniquePtr(pointer ptr) noexcept
        : ptr_(ptr) {}

    // Принимаем делетер по значению и по rvalue
    // чтобы поддержать и stateless и stateful делетеры
    UniquePtr(pointer ptr, const Deleter& d) noexcept
        : ptr_(ptr), deleter_(d) {}

    UniquePtr(pointer ptr, Deleter&& d) noexcept
        : ptr_(ptr), deleter_(std::move(d)) {}

    // ---- Копирование запрещено ----
    UniquePtr(const UniquePtr&)            = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    // ---- Move ----
    UniquePtr(UniquePtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , deleter_(std::move(other.deleter_)) {}

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            // Сначала освобождаем свой ресурс
            reset();
            ptr_     = std::exchange(other.ptr_, nullptr);
            deleter_ = std::move(other.deleter_);
        }
        return *this;
    }

    // Присваивание nullptr — удобный способ сбросить указатель
    UniquePtr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    // ---- Конвертирующий move — UniquePtr<Derived> -> UniquePtr<Base> ----
    template <typename U, typename UDeleter,
              typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    UniquePtr(UniquePtr<U, UDeleter>&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , deleter_(std::move(other.deleter_)) {}

    // ---- Деструктор ----
    ~UniquePtr() {
        reset();
    }

    // ---- Observers ----

    pointer   get()     const noexcept { return ptr_; }
    Deleter&  get_deleter()   noexcept { return deleter_; }
    const Deleter& get_deleter() const noexcept { return deleter_; }

    // Explicit bool — чтобы нельзя было случайно сравнить с int
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T& operator*()  const noexcept { return *ptr_; }
    pointer operator->() const noexcept { return ptr_; }

    // ---- Modifiers ----

    // release — отдаём владение, сами становимся пустыми
    [[nodiscard]] pointer release() noexcept {
        return std::exchange(ptr_, nullptr);
    }

    // reset — освобождаем текущий ресурс, захватываем новый (или nullptr)
    void reset(pointer ptr = nullptr) noexcept {
        pointer old = std::exchange(ptr_, ptr);
        if (old) {
            deleter_(old);
        }
    }

    void swap(UniquePtr& other) noexcept {
        std::swap(ptr_,     other.ptr_);
        std::swap(deleter_, other.deleter_);
    }

private:
    pointer ptr_     = nullptr;
    Deleter deleter_ = {};

    // Конвертирующий конструктор обращается к приватным полям
    template <typename U, typename UDeleter>
    friend class UniquePtr;
};

// ---- Специализация для массивов ----
// UniquePtr<int[]> arr(new int[10]);
template <typename T, typename Deleter>
class UniquePtr<T[], Deleter> {
public:
    using pointer      = T*;
    using element_type = T;
    using deleter_type = Deleter;

    constexpr UniquePtr() noexcept = default;
    explicit UniquePtr(pointer ptr) noexcept : ptr_(ptr) {}

    UniquePtr(const UniquePtr&)            = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)) {}

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    ~UniquePtr() { reset(); }

    // Для массивов — operator[] вместо operator* и operator->
    T& operator[](size_t index) const noexcept { return ptr_[index]; }

    pointer get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] pointer release() noexcept {
        return std::exchange(ptr_, nullptr);
    }

    void reset(pointer ptr = nullptr) noexcept {
        pointer old = std::exchange(ptr_, ptr);
        if (old) {
            Deleter{}(old); // DefaultDeleter<T[]> вызовет delete[]
        }
    }

private:
    pointer ptr_ = nullptr;
};

// ---- make_unique ----
template <typename T, typename... Args>
    requires (!std::is_array_v<T>)
UniquePtr<T> make_unique(Args&&... args) {
    return UniquePtr<T>(new T(std::forward<Args>(args)...));
}

// make_unique для массивов: make_unique<int[]>(10)
template <typename T>
    requires std::is_array_v<T>
UniquePtr<T> make_unique(size_t size) {
    return UniquePtr<T>(new std::remove_extent_t<T>[size]);
}