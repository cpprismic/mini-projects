#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <new>
#include <memory>
#include <utility>

template <typename T>
class RawMemory {
public:
    using value_type = T;

    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory& other) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};


template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;
    explicit Vector(size_t size) 
            : data_(size)
            , size_(size) {

        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other) 
            : data_(other.size_)
            , size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector copy_rhs(rhs);
                Swap(copy_rhs);
            } else {
                AssignWithinCapacity(rhs);
            }
        }
        return *this;
    }

    Vector(Vector&& other) {
        Swap(other);
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    template <typename U>
    iterator Insert(const_iterator pos, U&& value) {
        return Emplace(pos, std::forward<U>(value));
    }

    template <typename U>
    void PushBack(U&& value) {
        EmplaceBack(std::forward<U>(value));
    }

    template <typename ...Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ < data_.Capacity()) {
            std::construct_at(data_.GetAddress() + size_, std::forward<Args>(args)...);
        } else {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            std::construct_at(new_data.GetAddress() + size_, std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            } else {
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
        ++size_;
        return *std::prev(data_.GetAddress() + size_);
    }

    template <typename... Args>
    iterator Emplace(const_iterator iter, Args&&... args) {
        assert(iter >= begin() && iter <= end());
        int position = iter - begin();
        if (size_ < data_.Capacity()) {
            try {
                if (iter != end()) {
                    T temp_object(std::forward<Args>(args)...);                   
                    std::construct_at(data_.GetAddress() + size_, std::forward<T>(data_[size_ - 1]));
                    std::move_backward(data_.GetAddress() + position, std::prev(data_.GetAddress() + size_), data_.GetAddress() + size_);
                    *(begin() + position) = std::forward<T>(temp_object);
                } else {
                    std::construct_at(data_.GetAddress() + size_, std::forward<Args>(args)...);
                }
            } catch(...) {
                std::destroy_at(data_.GetAddress() + size_);
            }
        } else {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            std::construct_at(new_data.GetAddress() + position, std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), position, new_data.GetAddress());
                std::uninitialized_move_n(data_.GetAddress() + position, size_ - position, std::next(new_data.GetAddress() + position));
            } else {
                std::uninitialized_copy_n(data_.GetAddress(), position, new_data.GetAddress());
                std::uninitialized_copy_n(data_.GetAddress() + position, size_ - position, std::next(new_data.GetAddress() + position));
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
        ++size_;
        return data_.GetAddress() + position;
    }

    void PopBack() {
        if (size_ > 0) {
            std::destroy_at(std::prev(data_.GetAddress() + size_));
            --size_;
        }
    }

    iterator Erase(const_iterator iter) {
        assert(iter >= begin() && iter <= end());
        int position = iter - begin();
        std::move(std::next(data_.GetAddress() + position), data_.GetAddress() + size_, data_.GetAddress() + position);
        std::destroy_at(std::prev(data_.GetAddress() + size_));
        --size_;
        return data_.GetAddress() + position;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        } else {
            if (new_size > data_.Capacity()) {
                size_t new_capacity = std::max(data_.Capacity() * 2, new_size);
                Reserve(new_capacity);
            }
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Swap(Vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void AssignWithinCapacity(const Vector& rhs) {
        std::copy(rhs.data_.GetAddress(), 
                  rhs.data_.GetAddress() + std::min(rhs.size_, size_), 
                  data_.GetAddress());
        if (rhs.size_ < size_) {
            std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
        } 
        else {
            std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, 
                                      rhs.size_ - size_, 
                                      data_.GetAddress() + size_);
        }
        size_ = rhs.size_;
    }
};