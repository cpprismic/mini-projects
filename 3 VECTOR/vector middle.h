#include <cassert>
#include <cstring>
#include <algorithm>

template <typename T>
class Vector {
public:
    Vector() = default;

    explicit Vector(size_t size)
        : data_(new T[size])
        , size_(size)
        , capacity_(size) {}

    Vector(const Vector& other)
        : data_(new T[other.size_])
        , size_(other.size_)
        , capacity_(other.size_) {
        std::copy(other.data_, other.data_ + other.size_, data_);
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            delete[] data_;
            data_     = new T[other.size_];
            size_     = other.size_;
            capacity_ = other.size_;
            std::copy(other.data_, other.data_ + other.size_, data_);
        }
        return *this;
    }

    Vector(Vector&& other) noexcept
        : data_(std::exchange(other.data_, nullptr))
        , size_(std::exchange(other.size_, 0))
        , capacity_(std::exchange(other.capacity_, 0)) {}

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            delete[] data_;
            data_     = std::exchange(other.data_, nullptr);
            size_     = std::exchange(other.size_, 0);
            capacity_ = std::exchange(other.capacity_, 0);
        }
        return *this;
    }

    ~Vector() {
        delete[] data_;
    }

    void push_back(const T& value) {
        if (size_ == capacity_) {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            T* new_data = new T[new_capacity];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_     = new_data;
            capacity_ = new_capacity;
        }
        data_[size_++] = value;
    }

    T& operator[](size_t index) {
        assert(index < size_);
        return data_[index];
    }

    const T& operator[](size_t index) const {
        assert(index < size_);
        return data_[index];
    }

    size_t size()     const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }
    bool   empty()    const noexcept { return size_ == 0; }

    T* begin() noexcept { return data_; }
    T* end()   noexcept { return data_ + size_; }

private:
    T*     data_     = nullptr;
    size_t size_     = 0;
    size_t capacity_ = 0;
};