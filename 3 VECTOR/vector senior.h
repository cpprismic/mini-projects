#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>

template <typename T>
class Vector {
public:
    // ==================== CONSTRUCTORS / DESTRUCTOR ====================

    // Default constructor — пустой вектор, никакой памяти не выделяем
    Vector() = default;

    // Конструктор от размера — создаёт size элементов, инициализированных значением
    // explicit запрещает неявное преобразование: Vector v = 5; — ошибка
    explicit Vector(size_t size)
        : data_(allocate(size))           // выделяем сырую память
        , size_(size)                     // устанавливаем размер
        , capacity_(size) {               // capacity = size (без запаса при инициализации)
        // Конструируем объекты в сырой памяти через value-инициализацию
        // Для int -> 0, для std::string -> пустая строка
        std::uninitialized_value_construct_n(data_, size_);
    }

    // Copy constructor — создаём глубокую копию другого вектора
    Vector(const Vector& other)
        : data_(allocate(other.size_))    // выделяем память под копию
        , size_(other.size_)
        , capacity_(other.size_) {
        // Копируем элементы из other в новую память через copy-конструктор
        std::uninitialized_copy_n(other.data_, other.size_, data_);
    }

    // Copy assignment — через copy-and-swap идиому (сильная гарантия исключений)
    Vector& operator=(const Vector& other) {
        if (this != &other) {             // защита от самоприсваивания
            Vector tmp(other);            // создаём временную копию (может бросить исключение)
            swap(tmp);                    // обмениваемся с временным — noexcept
        }                                 // tmp уничтожается, забирая с собой старые данные
        return *this;
    }

    // Move constructor — крадём ресурсы у другого вектора
    Vector(Vector&& other) noexcept
        : data_(std::exchange(other.data_, nullptr))      // забираем указатель, у other ставим nullptr
        , size_(std::exchange(other.size_, 0))            // забираем размер, other обнуляем
        , capacity_(std::exchange(other.capacity_, 0)) {} // забираем capacity, other обнуляем

    // Move assignment — обмениваемся с перемещаемым объектом
    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            Vector tmp(std::move(other)); // перемещаем other во временный
            swap(tmp);                    // обмениваемся
        }                                 // tmp уничтожается
        return *this;
    }

    // Destructor — уничтожаем объекты и освобождаем память
    ~Vector() {
        std::destroy_n(data_, size_);     // вызываем деструкторы для всех живых элементов
        ::operator delete(data_);         // освобождаем сырую память (не ::delete[], т.к. выделяли через operator new)
    }

    // ==================== MODIFIERS ====================

    // Добавление элемента в конец (копированием)
    void push_back(const T& value) {
        emplace_back(value);              // делегируем emplace_back
    }

    // Добавление элемента в конец (перемещением)
    void push_back(T&& value) {
        emplace_back(std::move(value));   // делегируем emplace_back с перемещением
    }

    // Универсальный метод добавления элемента — конструирует элемент прямо в буфере
    template <typename... Args>
    void emplace_back(Args&&... args) {
        // Если места нет — увеличиваем capacity
        if (size_ == capacity_) {
            // Стратегия роста: ×2, но начинаем с 1 (стандартное поведение GCC/Clang)
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            reallocate(new_capacity);
        }
        // Конструируем объект по адресу data_ + size_ с переданными аргументами
        std::construct_at(data_ + size_, std::forward<Args>(args)...);
        ++size_;
    }

    // Ручное резервирование памяти — уменьшает количество реаллокаций
    void reserve(size_t new_capacity) {
        if (new_capacity > capacity_) {
            reallocate(new_capacity);     // только увеличиваем, никогда не уменьшаем
        }
    }

    // ==================== ELEMENT ACCESS ====================

    // Непроверяемый доступ — максимальная производительность
    T& operator[](size_t index) {
        return data_[index];
    }

    const T& operator[](size_t index) const {
        return data_[index];
    }

    // Проверяемый доступ — бросает исключение при выходе за границы
    T& at(size_t index) {
        if (index >= size_) throw std::out_of_range("Vector::at");
        return data_[index];
    }

    const T& at(size_t index) const {
        if (index >= size_) throw std::out_of_range("Vector::at");
        return data_[index];
    }

    // ==================== ITERATORS ====================

    T* begin() noexcept { return data_; }
    T* end()   noexcept { return data_ + size_; }
    const T* begin() const noexcept { return data_; }
    const T* end()   const noexcept { return data_ + size_; }

    // ==================== CAPACITY ====================

    size_t size()     const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }
    bool   empty()    const noexcept { return size_ == 0; }

    // ==================== UTILITIES ====================

    // Обмен содержимым с другим вектором — noexcept для безопасного использования в move-операциях
    void swap(Vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

private:
    // ==================== PRIVATE HELPERS ====================

    // Выделение сырой памяти под n объектов без конструирования
    static T* allocate(size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    // Реаллокация: перемещаем элементы в новую память большего размера
    void reallocate(size_t new_capacity) {
        T* new_data = allocate(new_capacity);                // 1. выделяем новую память

        if (data_) {
            // 2. перемещаем существующие элементы в новую память
            //    uninitialized_move_n — конструирует объекты через move-конструктор
            std::uninitialized_move_n(data_, size_, new_data);

            // 3. уничтожаем старые объекты (не освобождая память)
            std::destroy_n(data_, size_);

            // 4. освобождаем старую память
            ::operator delete(data_);
        }

        data_     = new_data;
        capacity_ = new_capacity;
    }

    // ==================== MEMBERS ====================

    T*     data_     = nullptr;   // указатель на буфер в куче
    size_t size_     = 0;         // количество живых элементов
    size_t capacity_ = 0;         // максимальное количество элементов без реаллокации
};