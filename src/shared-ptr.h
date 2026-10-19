#pragma once

#include <cstddef>
#include <memory>

namespace detail {
    template <typename T>
    void call_destructor(void* obj_ptr, void*) {
        static_cast<T*>(obj_ptr)->~T();
    }

    template <typename T>
    void call_delete(void* obj_ptr, void*) {
        delete static_cast<T*>(obj_ptr);
    }

    template <typename T, typename Deleter>
    void call_deleter(void* obj_ptr, void* deleter) {
        (*static_cast<Deleter*>(deleter))(static_cast<T*>(obj_ptr));
    }

    class control_block {
    private:
        using _destroy_obj_t = void (*)(void*, void*);

        void _delete_if_no_refs_remained() {
            if (_shared_counter + _weak_counter > 0) {
                return;
            }
            if (_from_make_shared) {
                this->~control_block();
                operator delete(_obj_ptr);
            } else {
                delete this;
            }
        }

    public:
        template <typename T>
        explicit control_block(T* obj_ptr, bool from_make_shared = false) noexcept
            : _obj_ptr(obj_ptr)
            , _destroy_obj(from_make_shared ? &call_destructor<T> : call_delete<T>)
            , _from_make_shared(from_make_shared) {}

        template <typename T, typename Deleter>
        control_block(T* obj_ptr, Deleter d)
            : _obj_ptr(obj_ptr)
            , _deleter(new Deleter(std::move(d)))
            , _destroy_obj(&call_deleter<T, Deleter>)
            , _destroy_deleter(&call_delete<Deleter>) {}

        void inc_shared() {
            _shared_counter++;
        }

        void inc_weak() {
            _weak_counter++;
        }

        void dec_shared() {
            _shared_counter--;
            if (_shared_counter == 0) {
                _destroy_obj(_obj_ptr, _deleter);
            }
            _delete_if_no_refs_remained();
        }

        void dec_weak() {
            _weak_counter--;
            _delete_if_no_refs_remained();
        }

        std::size_t shared_amount() {
            return _shared_counter;
        }

        ~control_block() {
            if (_deleter) {
                _destroy_deleter(_deleter, nullptr);
            }
        }

    private:
        void* _obj_ptr = nullptr;
        void* _deleter = nullptr;
        _destroy_obj_t _destroy_obj = nullptr;
        _destroy_obj_t _destroy_deleter = nullptr;
        bool _from_make_shared = false;

        std::size_t _shared_counter = 0;
        std::size_t _weak_counter = 0;
    };
} // namespace detail

template <typename T>
class shared_ptr {
    template <typename U>
    friend class shared_ptr;

    template <typename U>
    friend class weak_ptr;

private:
    using control_block = detail::control_block;

    void _inc_shared_amount() const {
        if (_cb_ptr != nullptr) {
            _cb_ptr->inc_shared();
        }
    }

    void _dec_shared_amount() const {
        if (_cb_ptr != nullptr) {
            _cb_ptr->dec_shared();
        }
    }

public:
    shared_ptr() noexcept = default;

    shared_ptr(std::nullptr_t) noexcept {}

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    explicit shared_ptr(Y* ptr)
        : _ptr(ptr) {
        try {
            _cb_ptr = new control_block(ptr);
        } catch (...) {
            delete ptr;
            throw;
        }
        _inc_shared_amount();
    }

    template <typename Y, typename Deleter, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr(Y* ptr, Deleter deleter)
        : _ptr(ptr) {
        try {
            _cb_ptr = new control_block(ptr, std::move(deleter));
        } catch (...) {
            deleter(ptr);
            throw;
        }
        _inc_shared_amount();
    }

    template <typename Y>
    shared_ptr(const shared_ptr<Y>& other, T* ptr) noexcept
        : _ptr(ptr)
        , _cb_ptr(other._cb_ptr) {
        _inc_shared_amount();
    }

    template <typename Y>
    shared_ptr(shared_ptr<Y>&& other, T* ptr) noexcept // What is the purpose of move ctor in terms of efficiency?
        : _ptr(ptr)
        , _cb_ptr(other._cb_ptr) {
        other._ptr = nullptr;
        other._cb_ptr = nullptr;
    }

    shared_ptr(const shared_ptr& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        _inc_shared_amount();
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr(const shared_ptr<Y>& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        _inc_shared_amount();
    }

    shared_ptr(shared_ptr&& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        other._ptr = nullptr;
        other._cb_ptr = nullptr;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr(shared_ptr<Y>&& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        other._ptr = nullptr;
        other._cb_ptr = nullptr;
    }

    shared_ptr& operator=(const shared_ptr& other) noexcept {
        shared_ptr(other).swap(*this);
        return *this;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr& operator=(const shared_ptr<Y>& other) noexcept {
        shared_ptr<T>(other).swap(*this);
        return *this;
    }

    shared_ptr& operator=(shared_ptr&& other) noexcept {
        shared_ptr(std::move(other)).swap(*this);
        return *this;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr& operator=(shared_ptr<Y>&& other) noexcept {
        shared_ptr(std::move(other)).swap(*this);
        return *this;
    }

    void swap(shared_ptr& other) noexcept {
        std::swap(this->_ptr, other._ptr);
        std::swap(this->_cb_ptr, other._cb_ptr);
    }

    T* get() const noexcept {
        return _ptr;
    }

    operator bool() const noexcept {
        return this->get() != nullptr;
    }

    T& operator*() const noexcept {
        return *_ptr;
    }

    T* operator->() const noexcept {
        return this->get();
    }

    std::size_t use_count() const noexcept {
        return _cb_ptr == nullptr ? 0 : _cb_ptr->shared_amount();
    }

    void reset() noexcept {
        shared_ptr().swap(*this);
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    void reset(Y* new_ptr) {
        shared_ptr(new_ptr).swap(*this);
    }

    template <typename Y, typename Deleter, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    void reset(Y* new_ptr, Deleter deleter) {
        shared_ptr(new_ptr, std::move(deleter)).swap(*this);
    }

    template <typename U, typename... Args>
    friend shared_ptr<U> make_shared(Args&&... args);

    ~shared_ptr() {
        _dec_shared_amount();
    }

private:
    T* _ptr = nullptr;
    control_block* _cb_ptr = nullptr;
};

template <typename T>
class weak_ptr {
    template <typename U>
    friend class weak_ptr;

private:
    using control_block = detail::control_block;

    void _inc_weak_amount() const {
        if (_cb_ptr != nullptr) {
            _cb_ptr->inc_weak();
        }
    }

    void _dec_shared_amount() const {
        if (_cb_ptr != nullptr) {
            _cb_ptr->dec_weak();
        }
    }

public:
    weak_ptr() noexcept = default;

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr(const shared_ptr<Y>& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        _inc_weak_amount();
    }

    weak_ptr(const weak_ptr& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        _inc_weak_amount();
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr(const weak_ptr<Y>& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        _inc_weak_amount();
    }

    weak_ptr(weak_ptr&& other) noexcept // Same move ctor question
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        other._ptr = nullptr;
        other._cb_ptr = nullptr;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr(weak_ptr<Y>&& other) noexcept
        : _ptr(other._ptr)
        , _cb_ptr(other._cb_ptr) {
        other._ptr = nullptr;
        other._cb_ptr = nullptr;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr& operator=(const shared_ptr<Y>& other) noexcept {
        weak_ptr(other).swap(*this);
        return *this;
    }

    weak_ptr& operator=(const weak_ptr& other) noexcept {
        weak_ptr(other).swap(*this);
        return *this;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr& operator=(const weak_ptr<Y>& other) noexcept {
        weak_ptr(other).swap(*this);
        return *this;
    }

    weak_ptr& operator=(weak_ptr&& other) noexcept {
        weak_ptr(std::move(other)).swap(*this);
        return *this;
    }

    template <typename Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr& operator=(weak_ptr<Y>&& other) noexcept {
        weak_ptr(std::move(other)).swap(*this);
        return *this;
    }

    void swap(weak_ptr& other) noexcept {
        std::swap(this->_ptr, other._ptr);
        std::swap(this->_cb_ptr, other._cb_ptr);
    }

    shared_ptr<T> lock() const noexcept {
        if (_cb_ptr == nullptr || _cb_ptr->shared_amount() == 0) {
            return shared_ptr<T>();
        }
        shared_ptr<T> sp;
        sp._ptr = this->_ptr;
        sp._cb_ptr = this->_cb_ptr;
        sp._inc_shared_amount();
        return sp;
    }

    void reset() noexcept {
        weak_ptr().swap(*this);
    }

    ~weak_ptr() {
        _dec_shared_amount();
    }

private:
    T* _ptr = nullptr;
    control_block* _cb_ptr = nullptr;
};

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
    using control_block = detail::control_block;

    const std::size_t cb_align_bytes = alignof(control_block) - sizeof(T) % alignof(control_block);
    void* pv = operator new(sizeof(T) + cb_align_bytes + sizeof(control_block));

    T* obj_ptr;
    try {
        new (pv) T(std::forward<Args>(args)...);
        obj_ptr = static_cast<T*>(pv);
    } catch (...) {
        operator delete(pv);
        throw;
    }

    shared_ptr<T> res;
    res._ptr = obj_ptr;
    res._cb_ptr = new (static_cast<char*>(pv) + sizeof(T) + cb_align_bytes) control_block(obj_ptr, true);
    res._inc_shared_amount();

    return res;
}

template <typename A, typename B>
bool operator==(const shared_ptr<A>& lhs, const shared_ptr<B>& rhs) noexcept {
    return lhs.get() == rhs.get();
}

template <typename A>
bool operator==(const shared_ptr<A>& lhs, std::nullptr_t) noexcept {
    return !lhs.get();
}

template <typename A>
bool operator==(std::nullptr_t, const shared_ptr<A>& rhs) noexcept {
    return !rhs.get();
}

template <typename A, typename B>
bool operator!=(const shared_ptr<A>& lhs, const shared_ptr<B>& rhs) noexcept {
    return !(lhs == rhs);
}